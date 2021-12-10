/* ZX Server

Controls communication to the ZX computer by listening to signal_from and 
sending data via signal_to modules.

Works asynchronously, thus communication is done via queues

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "nvs.h"
#include "zx_serv_dialog.h"
#include "tape_signal.h"
#include "video_attr.h"


#include "zx_server.h"
#include "zx_file_img.h"

static const char *TAG = "zx_server";

static QueueHandle_t msg_queue=NULL;


#define FILFB_SIZE 48
#define FILENAME_SIZE 24

static uint8_t file_first_bytes[FILFB_SIZE]; // storage for analyzing the file name or commands etc
static uint8_t file_name_len=0; 




static bool get_zx_outlevel_inverted()
{
    esp_err_t err;
    uint8_t val=0;
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u8(my_handle, "OUTLEVEL_INV", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
    nvs_close(my_handle);
    return val!=0;
}

static void set_zx_outlevel_inverted(bool newval)
{
    nvs_handle my_handle;
    if(get_zx_outlevel_inverted() != newval) {
        ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
        ESP_ERROR_CHECK( nvs_set_u8(my_handle, "OUTLEVEL_INV", newval ? 1 : 0 ) );
        ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
        nvs_close(my_handle);
    }
}
 
 
static void send_zxf_loader_uncompressed(uint8_t* name_or_null){
    ESP_LOGI(TAG,"Send (uncompressed) loader N \n");
    taps_tx_wait_all_done();

    zxfimg_create(ZXFI_LOADER);
    uint8_t namechar=0xA6;  /* one byte file name */ 

    taps_tx_packet_t p;
    p.packet_type_id=TTX_DATA_ZX81_STDSPEED;
    p.name= name_or_null ? name_or_null : &namechar;
    p.namesize=1;
    while(p.name[p.namesize-1] < 128) p.namesize++; /* find out actual length */
    p.data=zxfimg_get_img();
    p.datasize=zxfimg_get_size();
    p.para=0;
    p.predelay_ms=100;
    taps_tx_enqueue(&p, true);
    ESP_LOGI(TAG,"Name %d, data %d bytes \n",p.namesize,p.datasize);

    zxfimg_delete();
}


static void send_zxf_image_compr(){
    taps_tx_packet_t p;
    taps_tx_wait_all_done();
    p.packet_type_id=TTX_DATA_ZX81_QLOAD;
    p.name=NULL;
    p.namesize=0;
    p.data=zxfimg_get_img();
    p.datasize=zxfimg_get_size();
    p.para=get_zx_outlevel_inverted()?1:0;
    p.predelay_ms=100;
    taps_tx_enqueue(&p, true);
}




static void send_direct_data_compr(const uint8_t* sdata, uint32_t siz){
    taps_tx_packet_t p;
    taps_tx_wait_all_done();
    p.packet_type_id=TTX_DATA_ZX81_QLOAD;
    p.name=NULL;
    p.namesize=0;
    p.data=sdata;
    p.datasize=siz;
    p.para=get_zx_outlevel_inverted()?1:0;
    p.predelay_ms=1;
    taps_tx_enqueue(&p, true);
}



static void zxsrv_filename_received(){
    char namebuf[FILFB_SIZE];
    zx_string_to_ascii(file_first_bytes,file_name_len,namebuf);
    ESP_LOGI(TAG,"SAVE file, name [%s]\n",namebuf);
}

static void save_received_zxfimg(){
    FILE *fd = NULL;
    uint16_t i;
    const char *dirpath="/spiffs/";
    char entrypath[ESP_VFS_PATH_MAX+17];
    char namebuf[FILFB_SIZE];
    zx_string_to_ascii(file_first_bytes,file_name_len,namebuf);
    if(NULL==strchr(namebuf,'.') ){ /* add extension if none provided */
        strlcpy(namebuf + strlen(namebuf), ".p", FILFB_SIZE - strlen(namebuf));
    }
    ESP_LOGI(TAG,"SAVE - file [%s]\n",namebuf);
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    strlcpy(entrypath + strlen(dirpath), namebuf, sizeof(entrypath) - strlen(dirpath));
    fd = fopen(entrypath, "wb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to write to file : %s", entrypath);
        return;
    }
    for(i=0; i<zxfimg_get_size(); i++) {
        fputc( zxfimg_get_img()[i], fd );
    } 
    /* Close file after write complete */
    fclose(fd);
    ESP_LOGI(TAG,"SAVE - done\n");
}




#define EVT_TIMEOUT_MS 50
#define COMPRLOAD_TIMEOUT_MS 2000  /* usually returns after 500ms*/

static struct{
    uint8_t active_tag;
    uint16_t expected_size;
} qsavestatus;

static void zxsrv_task(void *arg)
{
    uint16_t watchdog_cnt=0;    
    zxserv_event_t evt;
    char* directload_filepath=NULL;
    uint32_t diag_sum=0;
    ESP_LOGI(TAG,"sfzx_task START \n");
    /* different ULA flavors need different levels - TODO bring to NVS */
    while(true){
		if(pdTRUE ==  xQueueReceive( msg_queue, &evt, EVT_TIMEOUT_MS / portTICK_RATE_MS ) ) {
			//ESP_LOGI(TAG,"Retrieved evt %d",evt.evt_type);
            if(evt.evt_type==ZXSG_HIGH){
                // Load
                if(!taps_is_tx_active()){
                    // check if name was given
                    directload_filepath=NULL;
                    if (tape_string_name_len){
                        tape_string_name[tape_string_name_len-1] |= 0x80; /* mark end */
                        directload_filepath = zxsrv_find_file_from_zxname(tape_string_name); /* see if we have a match */
                    }
                    send_zxf_loader_uncompressed( tape_string_name_len ? tape_string_name : NULL );
                    if (watchdog_cnt==0) watchdog_cnt=1;
                    // next - main menu
                    zxdlg_reset();
                } else {
                    ESP_LOGI(TAG,"Ignore loader request as loader is active \n"); 
                }
            }else if(evt.evt_type==ZXSG_QSAVE_TAG){
                qsavestatus.active_tag=evt.data;
                qsavestatus.expected_size=evt.addr;
                if(qsavestatus.expected_size==0) qsavestatus.expected_size=256; /* 8 bit encoding on other side */
                //ESP_LOGI(TAG,"ZXSG_QSAVE_TAG %d",qsavestatus.active_tag); 
            }else if(evt.evt_type==ZXSG_QSAVE_DATA){
                if(qsavestatus.active_tag==ZX_QSAVE_TAG_HANDSHAKE){
                    // ZX will send RAMTOP as content
                    if(evt.addr+1==qsavestatus.expected_size){
                        const uint8_t res_signal[]={0xcc,0x33,0xcc,0x33};
                        ESP_LOGI(TAG,"ZX_QSAVE_TAG_HANDSHAKE"); 
                        send_direct_data_compr(res_signal,sizeof(res_signal));
                        qsavestatus.active_tag=0;
                    }
                }
                else if(qsavestatus.active_tag==ZX_QSAVE_TAG_DATA){
                    if( (evt.addr&0xff) ==0) diag_sum=0;
                    diag_sum+=evt.data;
                    if( (evt.addr&0xff)+1 == qsavestatus.expected_size){
                        ESP_LOGI(TAG,"diag_sum at %d is %xh",evt.addr,diag_sum); 
                    }
                }

            }else if(evt.evt_type==ZXSG_FILE_DATA){
                if(evt.addr<FILFB_SIZE){
                    file_first_bytes[evt.addr]=(uint8_t) evt.data;
                    // extract file name
                    if(evt.addr==0){
                        file_name_len=0;
                        if(evt.data==ZX_SAVE_TAG_STRING_RESPONSE){
                            ESP_LOGI(TAG,"QSAVE Tag"); 
                        }
                    }
                    if(!file_name_len){
                        if( evt.data&0x80 ){
                            file_name_len=evt.addr+1;
                            // zxsrv_filename_received(); not only filename, could also be string input
                        } 
                    }
                    if(file_first_bytes[0]==ZX_SAVE_TAG_STRING_RESPONSE && evt.data==0x80){
                            // return string
                            ESP_LOGI(TAG,"STRING INPUT addr %d \n",evt.addr); 
                            for(int i=0;i<=evt.addr;i++){
                                ESP_LOGI(TAG,"  STR field %d %02X \n",evt.addr,file_first_bytes[i] ); 
                            }
                            if (zxdlg_respond_from_string(  &file_first_bytes[4], file_first_bytes[2])){
                                send_zxf_image_compr();
                                zxfimg_delete();
                            }
                    }

                    //
                    if(evt.addr==1){
                        if(file_first_bytes[0]==ZX_SAVE_TAG_LOADER_RESPONSE){
                            // send compressed second stage
                            if(directload_filepath){
                                ESP_LOGI(TAG,"Response from %dk ZX, send (compressed) file directly %d\n",(evt.data-0x40)/4,watchdog_cnt );                        
                                // TODO cold respond with out of memory if RAM does not match the file
                                if(zxsrv_load_file(directload_filepath)){
                                    send_zxf_image_compr();
                                    if (watchdog_cnt==1) watchdog_cnt=0; /* no watchdog here, may take too long */
                                    zxfimg_delete();
                                }
                                directload_filepath=0;
                            }else{
                                ESP_LOGI(TAG,"Response from %dk ZX, send 2nd (compressed) stage %d\n",(evt.data-0x40)/4,watchdog_cnt );                        
                                if (zxdlg_respond_from_key(0)){
                                    send_zxf_image_compr();
                                    if (watchdog_cnt==1) watchdog_cnt=2; /* watchdog armed, we need to check if the compressed loader loads successfully */
                                    zxfimg_delete();
                                }
                            }
                            ESP_LOGI(TAG,"Response done\n"); 
                        } else if(file_first_bytes[0]==ZX_SAVE_TAG_MENU_RESPONSE){
                            // send key rsponse etc
                            ESP_LOGI(TAG,"MENU RESPONSE KEYPRESS code %02X \n",evt.data); 
                            watchdog_cnt=0;
                            if (zxdlg_respond_from_key(evt.data)){
                                send_zxf_image_compr();
                                zxfimg_delete();
                            }
                        }
                    }
                }
                if(file_name_len && evt.addr>=file_name_len){
                    zxfimg_set_img(evt.addr-file_name_len,evt.data);
                    if(evt.addr>file_name_len+30 && zxfimg_get_size()==1+evt.addr-file_name_len ){
                        file_first_bytes[file_name_len-1] ^= 0x80; // end marker for name                      
                        save_received_zxfimg();
                        file_name_len=0;
                        zxdlg_reset();
                    }
                }
			} else if (evt.evt_type==ZXSG_SLOWM_50HZ || evt.evt_type==ZXSG_SLOWM_60HZ) {
                if(watchdog_cnt>=2){
                    ESP_LOGW(TAG,"Slow mode after initial LOAD, success after %d WD cnts.",watchdog_cnt);
                    watchdog_cnt=0; /* deactivate watchdog after succesfully loaded */
                }
            } else {
                if (evt.evt_type==ZXSG_SAVE) ESP_LOGI(TAG,"Evt SAVE");
                else if (evt.evt_type==ZXSG_SILENCE) ESP_LOGI(TAG,"Evt SILENCE");
                else if (evt.evt_type==ZXSG_HIGH) ESP_LOGI(TAG,"Evt HIGH");
                else if (evt.evt_type==ZXSG_NOISE) ESP_LOGI(TAG,"Evt NOISE");
                else 
                    ESP_LOGI(TAG,"Unexpected evt %d %d",evt.evt_type,watchdog_cnt);
            }
		} else {
            /* no new event, check if we have a failure in compressed loading */
            if(watchdog_cnt>=2){
                if(++watchdog_cnt > COMPRLOAD_TIMEOUT_MS/EVT_TIMEOUT_MS  ){
                    ESP_LOGW(TAG,"Compressed load watchdog - maybe wrong level (%c)!",(get_zx_outlevel_inverted() ? 'i':'n' ) );
                    set_zx_outlevel_inverted( ! get_zx_outlevel_inverted() );
                    watchdog_cnt=0;
                }
            }
        }
        
    }

}



void zxsrv_init()
{
    msg_queue=xQueueCreate(300,sizeof( zxserv_event_t ) );
    xTaskCreate(zxsrv_task, "zxsrv_task", 1024 * 4, NULL, configMAX_PRIORITIES - 5, NULL);
}

zxserv_evt_type_t current_status=ZXSG_INIT;

zxserv_evt_type_t zxsrv_get_zx_status()
{
    return current_status;
}

void zxsrv_send_msg_to_srv( zxserv_evt_type_t msg, uint16_t addr, uint16_t data)
{
    zxserv_event_t evt;
	current_status=evt.evt_type=msg;
	evt.addr=addr;
	evt.data=data;
	if( xQueueSendToBack( msg_queue,  &evt, 100 / portTICK_RATE_MS ) != pdPASS )
	{
		// Failed to post the message, even after 100 ms.
		ESP_LOGE(TAG, "Server write queue blocked");
	}
}


