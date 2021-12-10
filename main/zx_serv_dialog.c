/* ZX Serv Dialog

Controls creation and response from tiny ZX programs/screens with interactive content
to build a multi-stage menu system

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_ota_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "zx_file_img.h"

#include "iis_videosig.h"
#include "lcd_display.h"//TODO
#include "video_attr.h"
#include "zx_server.h"
#include "wifi_sta.h"
#include "zx_serv_dialog.h"

static const char *TAG = "zx_srv_dlg";



static char txt_buf[85]; // linewidth plus []-options

#define FILFB_SIZE 68
#define FILENAME_SIZE 24


/* lookup/jump table  for response from ZX */
typedef bool (*resp_func) (const char*, int);

typedef struct {
    uint8_t zxkey;  
    resp_func func;
    char func_arg_s[FILENAME_SIZE];
    int func_arg_i;  
} zxsrv_menu_response_entry;

static zxsrv_menu_response_entry menu_response[24];

static uint8_t menu_resp_size=0;

/* forward declare */
static void clear_mrespond_entries();   
static void create_mrespond_entry(uint8_t zxkey, resp_func func, const char* func_arg_s,  int func_arg_i);

static bool zxsrv_respond_filemenu(const char *dirpath, int); 
static bool zxsrv_respond_fileload(const char *filepath, int dummy);
static bool zxsrv_respond_inpstr(const char *question, int offset);
static bool zxsrv_respond_wifiscan(const char *dirpath, int offset);
static bool zxsrv_videooptions(const char *dirpath, int offset);
static bool zxsrv_loaddriver(const char *dirpath, int offset);
 
static bool zxsrv_system(const char *dirpath, int offset);
static bool zxsrv_help(const char *dirpath, int offset);



static void clear_mrespond_entries(){
    menu_resp_size=0;
}

static void create_mrespond_entry(uint8_t zxkey, resp_func func, const char* func_arg_s,  int func_arg_i){
    menu_response[menu_resp_size].zxkey=zxkey;
    menu_response[menu_resp_size].func=func;
    strlcpy(menu_response[menu_resp_size].func_arg_s, func_arg_s, FILENAME_SIZE);
    menu_response[menu_resp_size].func_arg_i=func_arg_i;
    menu_resp_size++;
}

void zxdlg_reset(){
    clear_mrespond_entries();
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
}

bool zxdlg_respond_from_key(uint8_t zxkey){
    uint8_t e=0;
    for (e=0; e<menu_resp_size; e++){
        if( menu_response[e].zxkey == zxkey || e+1==menu_resp_size ){
            return (*menu_response[e].func)(menu_response[e].func_arg_s, menu_response[e].func_arg_i);
        }
    }
    return false;
}


bool zxdlg_respond_from_string(uint8_t* strg, uint8_t len){
    if (menu_resp_size)
        return (*menu_response[0].func)((char*)strg, len);
    return false;
}




static bool zxsrv_respond_fileload(const char *filepath, int dummy){
    int rdbyte;
    uint16_t fpos=0;
    FILE *fd = NULL;

    ESP_LOGI(TAG, "FILELOAD : %s", filepath);

    clear_mrespond_entries();

    fd = fopen(filepath, "rb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        return false;
    }
    while(1) {
        rdbyte=fgetc(fd);
        if (rdbyte==EOF) break;
        zxfimg_set_img(fpos++, rdbyte);
    } 
    /* Close file after read complete */
    fclose(fd);
    // next - main menu
    clear_mrespond_entries();
    return true; // to send_zxf_image_compr();zxfimg_delete();
}


bool zxsrv_load_file(const char *filepath){
    return  zxsrv_respond_fileload(filepath, 0);
}

// makes a number from two ascii hexa characters
static int ahex2int(char a, char b){

    a = (a <= '9') ? a - '0' : (a & 0x7) + 9;
    b = (b <= '9') ? b - '0' : (b & 0x7) + 9;

    return (a << 4) + b;
}

static bool zxsrv_retrieve_wlanpasswd(const char *inp, int len){
    size_t slen;
    int i,c,wi;
    bool is_hex;
    char pwbuf[FILFB_SIZE];
    ESP_LOGI(TAG, "zxsrv_retrieve_wlanpasswd:");
 
    zx_string_to_ascii((uint8_t*)inp,len,pwbuf);
    slen=strlen(pwbuf);

    // due to the limited charset of ZX, password can also be entered as hex string with leading $
    if(pwbuf[0]=='$' && slen>=3 && (slen&1) ){  // format $414243 with even number of digits
        is_hex=true;
        for (i=1; i<slen; i++){
            if (!isxdigit(pwbuf[i])) {is_hex=false;break;}
        }
        if (is_hex){
            ESP_LOGI(TAG, "Convert from HEX : %s", pwbuf);
            wi=0;
            for (i=1; i<len; i+=2){
                //  int strtol(const char *str, char **endptr, int base) needs end mark, so use simpe
                c=ahex2int(pwbuf[i],pwbuf[i+1]);
                ESP_LOGI(TAG, "Convert : %02x", c);                
                pwbuf[wi++] = c;
            }
            pwbuf[wi]=0;
        }
    }

    ESP_LOGI(TAG, "WLANPASSWD : %s", pwbuf);

    wifi_sta_reconfig(NULL, pwbuf, true); /* reconnect */

    for(i=0;i<60;i++){
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if(wifi_sta_is_connected()) return zxsrv_respond_filemenu("/spiffs/", 0);
    }
    ESP_LOGI(TAG, "no connect detected");
    /* not conected, offer retry */
    return zxsrv_respond_wifiscan("WIFI", 0);
}


static bool zxsrv_respond_inpstr(const char *question, int offset){

   // const char *dirpath="/spiffs/";
    zxfimg_create(ZXFI_STR_INP);
    sprintf(txt_buf,"[ INPUT STRING ] ");
    zxfimg_print_video(1,txt_buf);

    zxfimg_print_video(3,question);

    clear_mrespond_entries();
    /* append default entry */
    create_mrespond_entry(0, zxsrv_retrieve_wlanpasswd, "", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}


static bool zxsrv_wifi_inp_pass(const char *wifi_name, int offset){


    zxfimg_create(ZXFI_STR_INP);
    sprintf(txt_buf,"   #[ ENTER WIFI PASSWORD ]# ");
    zxfimg_print_video(1,txt_buf);
    sprintf(txt_buf,"- FOR ACCESS POINT");
    zxfimg_print_video(3,txt_buf);
    zxfimg_print_video(5,wifi_name);

    sprintf(txt_buf,"USE INVERSE FOR LOWER CASE");
    zxfimg_print_video(9,txt_buf);
    sprintf(txt_buf,"EXAMPLE:");
    zxfimg_print_video(10,txt_buf);
    sprintf(txt_buf,"  \"H[ELLO]\"");
    zxfimg_print_video(12,txt_buf);

    sprintf(txt_buf,"HEX INPUT OPTION:");
    zxfimg_print_video(15,txt_buf);
    sprintf(txt_buf,"TYPE ASCII CODES AS $HEX");
    zxfimg_print_video(16,txt_buf);
    sprintf(txt_buf,"EXAMPLE:");
    zxfimg_print_video(17,txt_buf);
    sprintf(txt_buf,"  \"$48656c6c6f\"");
    zxfimg_print_video(19,txt_buf);

    wifi_sta_reconfig(wifi_name, NULL, false);


    clear_mrespond_entries();
    /* append default entry */
    create_mrespond_entry(0, zxsrv_retrieve_wlanpasswd, "", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}



static bool zxsrv_loaddriver(const char *wifi_name, int offset){
    zxfimg_create(ZXFI_DRIVER);
    zxdlg_reset();  // no response expected
    return true; // to send_zxf_image_compr();zxfimg_delete();
}


static bool zxsrv_help(const char *wifi_name, int offset){

    zxfimg_create(ZXFI_MENU_KEY);

    zxfimg_cpzx_video (0, (const uint8_t *) "\x1c\x34\x04\x05\x05\x05\x04\x00", 8);
    zxfimg_cpzx_video (1, (const uint8_t *) "\x03\x89\x05\x05\x05\x05\x05\x01", 8);
    zxfimg_cpzx_video (2, (const uint8_t *) "\x00\x00\x00\x01\x01\x01\x00\x00", 8);

    sprintf(txt_buf,"      ##[ ZX-WESPI-V HELP ]##");
    zxfimg_print_video(3,txt_buf);

    sprintf(txt_buf,"- LOAD AND SAVE ZX PROGRAM FILES");
    zxfimg_print_video(6,txt_buf);
    sprintf(txt_buf,"  VIA STANDARD BASIC COMMANDS:");
    zxfimg_print_video(7,txt_buf);
    sprintf(txt_buf,"  USE [LOAD \"\"] AND FOLLOW MENU");
    zxfimg_print_video(9,txt_buf);
    sprintf(txt_buf,"  USE [SAVE \"ANYNAME\"] TO STORE");
    zxfimg_print_video(11,txt_buf);

    sprintf(txt_buf,"- ACCESS THE FILES VIA BROWSER:");
    zxfimg_print_video(13,txt_buf);
    sprintf(txt_buf,"  WESPI ACTS AS WIFI HTTP SERVER");
    zxfimg_print_video(14,txt_buf);
    sprintf(txt_buf,"  JUST LOOK UP IP ADDRESS:");
    zxfimg_print_video(15,txt_buf);
    zxfimg_print_video(17,wifi_get_status_msg());

    clear_mrespond_entries();
    create_mrespond_entry(60, zxsrv_respond_wifiscan, "WIFI", 0 ); // "W"
    /* append default entry */
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}


static bool zxsrv_system(const char *wifi_name, int offset){

    zxfimg_create(ZXFI_MENU_KEY);

    //zxfimg_cpzx_video (0, (const uint8_t *) "\x1c\x34\x04\x05\x05\x05\x04\x00", 8);   /* simple but memory-saving version of header; currently replaced by the fancy one below */
    //zxfimg_cpzx_video (1, (const uint8_t *) "\x03\x89\x05\x05\x05\x05\x05\x01", 8);
    //zxfimg_cpzx_video (2, (const uint8_t *) "\x00\x00\x00\x01\x01\x01\x00\x00", 8);
    zxfimg_cpzx_video (0, (const uint8_t *) "\x1c\x34\x04\x05\x05\x05\x04\x00\x00\x00\x83\x89\x89\x82\x83\x83\x83\x83\x83\x83\x83\x83\x83\x83\x83\x04\x00\x00\x38\x3e\x38", 31);
    zxfimg_cpzx_video (1, (const uint8_t *) "\x03\x89\x05\x05\x05\x05\x05\x01\x00\x00\x83\x09\x09\x81\x80\x8a\x8a\x8a\x8a\x8a\x8a\x8a\x8a\x8a\x8a\x07\x00\x00\x28\x34\x33", 31);
    zxfimg_cpzx_video (2, (const uint8_t *) "\x00\x00\x00\x01\x01\x01\x00\x00\x00\x00\x00\x03\x03\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2b\x2e\x2c", 31);

    sprintf(txt_buf," #[ ZX-WESPI-V SYSTEM CONFIG ]#");
    zxfimg_print_video(4,txt_buf);

    sprintf(txt_buf,"[S] KEEP SILENCE TO LOAD TAPE");
    zxfimg_print_video(7,txt_buf);

    sprintf(txt_buf,"[V] VIDEO CONFIG");
    zxfimg_print_video(9,txt_buf);

    sprintf(txt_buf,"[W] WIFI CONFIG");
    zxfimg_print_video(11,txt_buf);

    sprintf(txt_buf,"[D] DRIVER (EXPERIMENTAL)");
    zxfimg_print_video(13,txt_buf);

    sprintf(txt_buf,"MAC %s",wifi_get_MAC_addr());
    zxfimg_print_video(18,txt_buf);

    sprintf(txt_buf,"VERSION %s-%s",esp_ota_get_app_description()->version,esp_ota_get_app_description()->date);
    zxfimg_print_video(20,txt_buf);

    clear_mrespond_entries();
    create_mrespond_entry(59, zxsrv_videooptions, "VIDEO", 0 ); // "V"
    create_mrespond_entry(60, zxsrv_respond_wifiscan, "WIFI", 0 ); // "W"
    create_mrespond_entry(41, zxsrv_loaddriver, "DRV", 0 ); // "D"
    /* append default entry */
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}



static bool zxsrv_respond_wifiscan(const char *dirpath, int offset){

    wifi_ap_record_t * ap_list;
    uint16_t st=0,num_ap=0;
    //esp_err_t err;

    wifi_scan_config_t scanConf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    ESP_LOGI(TAG, "WIFI SCAN ...");
    wifi_sta_allow_for_AP_scan();
    while (  ESP_OK != esp_wifi_scan_start(&scanConf, true) )  {
        ESP_LOGI(TAG, "  SCAN failed, retry...");
        vTaskDelay(133 / portTICK_PERIOD_MS);    
    }
    ESP_LOGI(TAG, "SCAN done.");
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&num_ap));
    ESP_LOGI(TAG, "Num WIFI stations: %d ",num_ap);
    if (num_ap>10) num_ap=10;
    ap_list=calloc(num_ap, sizeof(wifi_ap_record_t) );
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&num_ap, ap_list));    

    zxfimg_create(ZXFI_MENU_KEY);

    zxfimg_cpzx_video (0, (const uint8_t *) "\x1c\x34\x04\x05\x05\x05\x04\x00\x00\x06\x00\x06\x00\x87\x01\x00\x06\x86\x00\x06\x04\x06\x00\x00\x87\x03\x01\x3c\x2e\x2b\x2e", 31);
    zxfimg_cpzx_video (1, (const uint8_t *) "\x03\x89\x05\x05\x05\x05\x05\x01\x00\x05\x05\x05\x03\x85\x00\x00\x82\x84\x00\x05\x86\x01\x00\x00\x05\x06\x01\x00\x28\x34\x33", 31);
    zxfimg_cpzx_video (2, (const uint8_t *) "\x00\x00\x00\x01\x01\x01\x00\x00\x00\x02\x02\x00\x00\x00\x03\x01\x01\x02\x02\x00\x02\x00\x00\x00\x01\x01\x01\x00\x2b\x2e\x2c", 31);

    clear_mrespond_entries();
    /* Iterate over all files / folders and fetch their names and sizes */
    for (st=0;st<num_ap;st++) {
        ESP_LOGI(TAG, "Found %s  %d ",ap_list[st].ssid ,ap_list[st].rssi);
        create_mrespond_entry(st+0x1c, zxsrv_wifi_inp_pass,  (char*) ap_list[st].ssid, 0 );
        snprintf(txt_buf,32,"[%X] %-18.22s (%d)",st&0xf, ap_list[st].ssid , ((128+(int)ap_list[st].rssi)*100/128) &0x127 );
        zxfimg_print_video(st+5,txt_buf);
    }
    free(ap_list);
    sprintf(txt_buf,"SELECT ACCESS POINT OR E[X]IT");
    zxfimg_print_video(22,txt_buf);


    /* append default entry */
    create_mrespond_entry(55, zxsrv_respond_inpstr, "INP-QU", 0 ); // "R"
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}


static bool zxsrv_videooptions(const char *name, int command){

    /* if this is called with a nonzero cmd, we are reloading ans executing the issued command */
    if(command){
            ESP_LOGI(TAG, "zxsrv_videooptions: %d",command);
            switch(command){
            case 'C':
                    vid_cal_pixel_start();
                    break;
            case 'W':
            case 'G':
            case 'A':
            case 'F':
            case 'B':
                vidattr_set_c_mode(command);
                break;
            case 'I':   
                vidattr_set_inv_mode(true);
                break;
            case 'N':
                vidattr_set_inv_mode(false);
                break;
            default:
                break;
        }

    }

    zxfimg_create(ZXFI_MENU_KEY);

    zxfimg_cpzx_video (0, (const uint8_t *) "\x1c\x34\x04\x05\x05\x05\x04\x00\x00\x86\x00\x85\x02\x07\x02\x07\x04\x00\x06\x03\x87\x89\x89\x89\x89\x04\x3b\x2e\x29\x2a\x34", 31);
    zxfimg_cpzx_video (1, (const uint8_t *) "\x03\x89\x05\x05\x05\x05\x05\x01\x00\x00\x05\x06\x00\x05\x00\x05\x85\x85\x03\x01\x85\x08\x08\x08\x08\x05\x00\x00\x28\x34\x33", 31);
    zxfimg_cpzx_video (2, (const uint8_t *) "\x00\x00\x00\x01\x01\x01\x00\x00\x00\x00\x02\x00\x02\x03\x02\x03\x01\x00\x03\x01\x00\x03\x03\x03\x03\x00\x00\x00\x2b\x2e\x2c", 31);

    sprintf(txt_buf,"###[ ZX-WESPI VIDEO SETTINGS ]###");
    zxfimg_print_video(4,txt_buf);

    sprintf(txt_buf," [C]ALIBRATE PIXEL PHASE");
    zxfimg_print_video(7,txt_buf);

    sprintf(txt_buf," [I]NVERSE OR [N]ORMAL");
    zxfimg_print_video(10,txt_buf);

    sprintf(txt_buf," [W]HITE, [G]REEN, [A]MBER, or [F]ANCY");
    zxfimg_print_video(12,txt_buf);

    /* the last four lines need to be excactly this way as the patterns are used by the calibration */
    sprintf(txt_buf,"[ ] [ ] ");
    zxfimg_print_video(20,txt_buf);
    zxfimg_print_video(21,txt_buf);
    sprintf(txt_buf,"####[####]####[####]####[####]####[####]");
    zxfimg_print_video(22,txt_buf);
    zxfimg_print_video(23,txt_buf);


    clear_mrespond_entries();
    create_mrespond_entry(56, zxsrv_system, "INP-QU", 0 ); // "S"
    
    create_mrespond_entry(40, zxsrv_videooptions, "VID-CAL", 'C' ); // "C"
    create_mrespond_entry(46, zxsrv_videooptions, "VID-INV", 'I' ); // "I"
    create_mrespond_entry(51, zxsrv_videooptions, "VID-NRM", 'N' ); // "N"
    create_mrespond_entry(60, zxsrv_videooptions, "VID-WH", 'W' ); // "W"
    create_mrespond_entry(44, zxsrv_videooptions, "VID-GR", 'G' ); // "G"
    create_mrespond_entry(38, zxsrv_videooptions, "VID-AM", 'A' ); // "A"
    create_mrespond_entry(43, zxsrv_videooptions, "VID-FA", 'F' ); // "F"
    create_mrespond_entry(39, zxsrv_videooptions, "VID-BU", 'B' ); // "B"
    
    /* append default entry */
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}



#define MAX_FILEENTRY_LINES 15

static bool zxsrv_respond_filemenu(const char *dirpath, int offset){

   // const char *dirpath="/spiffs/";
    char entrypath[ESP_VFS_PATH_MAX+17];
    char entrysize[16];
    const char *entrytype;
    uint8_t entry_num=0;
    uint8_t disp_line=0;
    uint16_t num_entries=0;
    struct dirent *entry;
    struct stat entry_stat;
    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    ESP_LOGI(TAG, "FILEMENU1 : %s offs %d ", dirpath, offset);
    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
    //    /* Respond with 404 Not Found */
    //    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return false;
    }
    // count the overall number of entries first
    num_entries=0;
    while ((entry = readdir(dir)) != NULL) num_entries++;
    closedir(dir);

    // create next/prev entries while we still have the valid dirpath
//    ESP_LOGI(TAG, "FILEMENU : num_entries %d offs %d ", num_entries, offset);
    clear_mrespond_entries();
    if(offset){
    	create_mrespond_entry(53, zxsrv_respond_filemenu, dirpath, offset>(MAX_FILEENTRY_LINES-1) ? offset-(MAX_FILEENTRY_LINES) : 0 ); // "P"
    }
    if(num_entries>15){
    	create_mrespond_entry(51, zxsrv_respond_filemenu, dirpath, offset+(MAX_FILEENTRY_LINES-1)<num_entries-4 ? offset+(MAX_FILEENTRY_LINES-1) : num_entries-4    ) ;  // "N"
    }

    dir = opendir(dirpath);

    zxfimg_create(ZXFI_MENU_KEY);
 //  closedir(dir);
 //  return true;
    //sprintf(txt_buf,"[ FILE MENU ]: (%s) ",dirpath);
    //zxfimg_print_video(1,txt_buf);

    zxfimg_cpzx_video (0, (const uint8_t *) "\x1c\x34\x04\x05\x05\x05\x04\x00\x00\x83\x04\x87\x87\x01\x00\x06\x00\x04\x06\x01\x06\x03\x87\x03\x04\x02\x00\x27\x3e\x3f\x3d", 31);
    zxfimg_cpzx_video (1, (const uint8_t *) "\x03\x89\x05\x05\x05\x05\x05\x01\x00\x06\x00\x87\x86\x02\x01\x05\x05\x05\x07\x01\x02\x86\x85\x03\x00\x84\x00\x39\x2a\x26\x32", 31);
    zxfimg_cpzx_video (2, (const uint8_t *) "\x00\x00\x00\x01\x01\x01\x00\x00\x02\x03\x03\x02\x00\x01\x00\x02\x02\x00\x02\x03\x02\x01\x02\x00\x02\x03\x01\x1e\x1c\x1e\x1c", 31);


    /* Iterate over all files / folders and fetch their names and sizes */
    disp_line=0;
    while ((entry = readdir(dir)) != NULL && disp_line<MAX_FILEENTRY_LINES) {
    	if (entry_num>=offset){
			entrytype = (entry->d_type == DT_DIR ? "(DIR)" : "");

			strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
			if (stat(entrypath, &entry_stat) == -1) {
				ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
				continue;
			}
			snprintf(entrysize,16, "%ld", entry_stat.st_size);
			ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name , entrysize);
			create_mrespond_entry(disp_line+0x1c, zxsrv_respond_fileload,  entrypath, 0 );

			snprintf(txt_buf,32," [%X] %.16s %.5s",disp_line,entry->d_name,entrytype);
			zxfimg_print_video(disp_line+5,txt_buf);
			disp_line++;
    	}
        entry_num++;
    }
    // NOTE: dirpath area has been overritten by the new entries, so do not use

    if(num_entries==0){
        sprintf(txt_buf," USE [SAVE] FROM ZX OR UPLOAD");
        zxfimg_print_video(8,txt_buf);
        sprintf(txt_buf," VIA WIFI TO ADD FILES HERE");
        zxfimg_print_video(10,txt_buf);
    }

    zxfimg_print_video(23,wifi_get_status_msg());

    //sprintf(txt_buf,"VER:0.04B %s",IDF_VER);
    snprintf(txt_buf,36,"%6s %6s  [S]YS [V]IDEO [H]ELP",entry!=NULL?"[N]EXT":"    ",offset?"[P]REV":"    ");
    zxfimg_print_video(21,txt_buf);

  // create_mrespond_entry(55, zxsrv_respond_inpstr, "INP-QU", 0 ); // "R"
    create_mrespond_entry(56, zxsrv_system, "INP-QU", 0 ); // "S"
    create_mrespond_entry(45, zxsrv_help, "HELP", 0 ); // "H"
    create_mrespond_entry(59, zxsrv_videooptions, "VIDEO", 0 ); // "V"
    create_mrespond_entry(60, zxsrv_respond_wifiscan, "WIFI", 0 ); // "W"
    /* append default entry */
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
    closedir(dir);
    return true; // to send_zxf_image_compr();zxfimg_delete();
}

static char zxsrv_find_file_entrypath[ESP_VFS_PATH_MAX+17];

char* zxsrv_find_file_from_zxname(uint8_t *tape_string_name){
    const char *dirpath="/spiffs/";

    char* result=NULL;
    struct dirent *entry;
    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    ESP_LOGI(TAG, "FILESEACH : %s  ", dirpath);
    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(zxsrv_find_file_entrypath, dirpath, sizeof(zxsrv_find_file_entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        return 0;
    }
    dir = opendir(dirpath);


    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        strlcpy(zxsrv_find_file_entrypath + dirpath_len, entry->d_name, sizeof(zxsrv_find_file_entrypath) - dirpath_len);
        uint8_t* inpname=tape_string_name;
        char* filen=entry->d_name;
        while(*filen){
            if( (convert_ascii_to_zx_code(*filen)&0x3f) != (*inpname&0x3f) ) /* ignore inverse */ break;
            /* match so far */
            if(*inpname >= 64){
                /* full match */
                result=zxsrv_find_file_entrypath;
                ESP_LOGI(TAG, "MATCH : %s  ", zxsrv_find_file_entrypath);
                break;
            }
            ++inpname;
            ++filen;
        }
        if(result) break;
    }
    closedir(dir);

    if(!result && tape_string_name[0]==22) return zxsrv_find_file_from_zxname(&tape_string_name[1]);  // Minus sign may be used for TAPE selection on NU, check if we have a match w/o this
    
    return result; // to send_zxf_image_compr();zxfimg_delete();
}
