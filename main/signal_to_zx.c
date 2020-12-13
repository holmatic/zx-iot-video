#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "zx_server.h"
#include "signal_to_zx.h"


static const char* TAG = "stzx";


//i2s number
#define STZX_I2S_NUM           (1)
//i2s sample rate
// 
#define STZX_I2S_SAMPLE_RATE   (3906) // times 32 is 8us/bit = 64us/byte
#define USEC_TO_BYTE_SAMPLES(us)   (us*STZX_I2S_SAMPLE_RATE*4/1000000) 
#define MILLISEC_TO_BYTE_SAMPLES(ms)   (ms*STZX_I2S_SAMPLE_RATE*4/1000) 



//i2s data bits
// Will send out 15bits MSB first
#define STZX_I2S_SAMPLE_BITS   (16)

//I2S read buffer length in bytes
// we want to buffer several millisecs to be able to reschedule freely; on the
// other side, we do not want too many delays in order to be reactive, so say 10ms
#define STZX_I2S_WRITE_LEN_SAMPLES      (STZX_I2S_SAMPLE_RATE/25)
#define STZX_I2S_WRITE_LEN_BYTES      (STZX_I2S_WRITE_LEN_SAMPLES * STZX_I2S_SAMPLE_BITS/8)



static void stzx_task(void*arg);

static QueueHandle_t event_queue=NULL;
static uint8_t* i2s_writ_buff=NULL;
static  QueueHandle_t file_data_queue=NULL;
 


/**
 * @brief I2S ADC/DAC mode init.
 */
void stzx_init()
{
	int i2s_num = STZX_I2S_NUM;
	i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate =  STZX_I2S_SAMPLE_RATE,
        .bits_per_sample = STZX_I2S_SAMPLE_BITS,
	    .communication_format =  I2S_COMM_FORMAT_I2S_MSB,
	    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
	    .intr_alloc_flags = 0,
	    .dma_buf_count = 4,
	    .dma_buf_len = STZX_I2S_WRITE_LEN_SAMPLES,
	    .use_apll = 0,//1, True cause problems at high rates (?)
	};
	 //install and start i2s driver
    //xQueueCreate(5, sizeof(i2s_event_t));
	ESP_ERROR_CHECK( i2s_driver_install(i2s_num, &i2s_config, 5, &event_queue ));
	//init DOUT pad
    static const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = I2S_PIN_NO_CHANGE,
        .data_out_num = 22,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_set_pin(i2s_num, &pin_config);
    
    i2s_writ_buff=(uint8_t*) calloc(STZX_I2S_WRITE_LEN_BYTES, sizeof(uint8_t));
    if(!i2s_writ_buff) printf("calloc of %d failed\n",STZX_I2S_WRITE_LEN_BYTES);


    xTaskCreate(stzx_task, "stzx_task", 1024 * 3, NULL, 9, NULL);
}

static uint8_t outlevel_inv=0;

void stzx_set_out_inv_level(bool inv)
{
	outlevel_inv = inv ? 0xff : 0;
}

static inline void set_sample(uint8_t* samplebuf, uint32_t ix, uint8_t val)
{
    samplebuf[ix^0x0003]=val ^ outlevel_inv;  // convert for endian byteorder
}


//const uint8_t wav_zero[]={  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00};
//const uint8_t wav_one[]={  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00 };
const uint8_t wav_zero[]={  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff};
const uint8_t wav_one[]={  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff, 0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff,  0xff,0x00,0x00,0xff };


const uint8_t wav_compr_hdr[]={  0xff,0xf0};


typedef struct zxfile_wr_status_info
{
    const uint8_t* wavsample; // pointer to sample, if zero, play
    uint32_t remaining_wavsamples; //
    uint32_t bitcount;
    uint8_t data;
    uint8_t startbit_done; // needed for compressed only
} zxfile_wr_status_t;

static zxfile_wr_status_t zxfile;    // some signal statistics

// return true if end of file reached
static bool fill_buf_from_file(uint8_t* samplebuf, QueueHandle_t dataq, size_t buffered_filesize, uint8_t file_tag)
{
	bool end_f=false;
    uint32_t ix=0;
	uint8_t smpl;
	if(file_tag==FILE_NOT_ACTIVE){
	    while(ix<STZX_I2S_WRITE_LEN_BYTES){
			 set_sample(samplebuf,ix++,  0xff );
		}
		if(zxfile.remaining_wavsamples) zxfile.remaining_wavsamples=zxfile.remaining_wavsamples>STZX_I2S_WRITE_LEN_BYTES? zxfile.remaining_wavsamples-STZX_I2S_WRITE_LEN_BYTES :0;
	}else if(file_tag==FILE_TAG_COMPRESSED){
	    while(ix<STZX_I2S_WRITE_LEN_BYTES) {
			if(!zxfile.startbit_done && zxfile.remaining_wavsamples==0){
					zxfile.wavsample=wav_compr_hdr;
					zxfile.remaining_wavsamples=sizeof(wav_compr_hdr);
					zxfile.startbit_done=1;
			}
			if(zxfile.remaining_wavsamples){
				set_sample(samplebuf,ix++, zxfile.wavsample ? *zxfile.wavsample++ : 0xff );
				--zxfile.remaining_wavsamples;
			} else {
				zxfile.wavsample=NULL; // after sample, switch back
				if(zxfile.bitcount < buffered_filesize*8 ) {
					if( (zxfile.bitcount&7) ==0){
						if(pdTRUE != xQueueReceive( dataq, &zxfile.data, 0 ) ) ESP_LOGE(TAG, "End of data");
					}
					smpl=0;
					if(0==(zxfile.data & (0x80 >> (zxfile.bitcount&7)  ))) smpl|=0xf0;
					zxfile.bitcount++;
					if(0==(zxfile.data & (0x80 >> (zxfile.bitcount&7)  ))) smpl|=0x0f;
					zxfile.bitcount++;
					smpl^=0x11;
					set_sample(samplebuf,ix++,  smpl );
					if( (zxfile.bitcount&7) ==0){
						zxfile.startbit_done=0;
					}
				} else {
					set_sample(samplebuf,ix++,  0xff );
					if(!end_f)	ESP_LOGW(TAG, "End compr file");
					end_f=true;
				}
			}
		}
	}else{
	    while(ix<STZX_I2S_WRITE_LEN_BYTES) {
			if(zxfile.remaining_wavsamples){
				set_sample(samplebuf,ix++, zxfile.wavsample ? *zxfile.wavsample++ : 0xff );
				--zxfile.remaining_wavsamples;
			} else {
				if (zxfile.wavsample){ // after sample, always insert silence
					zxfile.wavsample=NULL;
					zxfile.bitcount++;  // prepare for next bit
					zxfile.remaining_wavsamples=USEC_TO_BYTE_SAMPLES(1300);
				} else {
					if(zxfile.bitcount < buffered_filesize*8 ) {
						if( (zxfile.bitcount&7) ==0){
							if(pdTRUE != xQueueReceive( dataq, &zxfile.data, 0 ) ) ESP_LOGE(TAG, "End of data");
						}
						if(zxfile.data & (0x80 >> (zxfile.bitcount&7)  )){
							zxfile.wavsample=wav_one;
							zxfile.remaining_wavsamples=sizeof(wav_one);
						} else {
							zxfile.wavsample=wav_zero;
							zxfile.remaining_wavsamples=sizeof(wav_zero);
						}
					} else {
						set_sample(samplebuf,ix++,  0xff );
						if(!end_f)	ESP_LOGW(TAG, "End std file");
						end_f=true;
					}
				}
			}
        }
    }
    return end_f;
}


static uint8_t file_busy=0;

#define SEND_HOLDOFF_BYTES 200  // enough so we do not run out of data on first chunk even when compressed

bool stzx_is_transfer_active()
{
	return file_busy!=0;
}

void stzx_send_cmd(stzx_mode_t cmd, uint8_t data)
{
    i2s_event_t evt;
    static uint8_t file_active=FILE_NOT_ACTIVE;
	static size_t fsize;

	if (cmd==STZX_FILE_START){
		if(file_active)
			ESP_LOGE(TAG, "File double-open");
		if(!file_data_queue){
			file_data_queue=xQueueCreate(16384,sizeof(uint8_t));
		}
		if (data!=FILE_TAG_NORMAL && data!=FILE_TAG_COMPRESSED){
			ESP_LOGE(TAG, "Invalid File start mark");
		}

		/* make sur previous file is done and gone, otherwise the byte counting mechanism malfunctios */
		while(file_busy) vTaskDelay(20 / portTICK_RATE_MS);

		file_busy=1;
	    if( xQueueSendToBack( file_data_queue,  &data, 100 / portTICK_RATE_MS ) != pdPASS ) {
	        // Failed to post the message, even after 100 ms.
			ESP_LOGE(TAG, "File write queue blocked");
	    }
		file_active=data;
		fsize=0;
	}
	else if (cmd==STZX_FILE_DATA){
		if(!file_active)
			ESP_LOGE(TAG, "File not open on data write");
	    if( xQueueSendToBack( file_data_queue,  &data, 100 / portTICK_RATE_MS ) != pdPASS )
	    {
	        // Failed to post the message, even after 100 ms.
			ESP_LOGE(TAG, "File write queue blocked");
	    }
	    ++fsize;
    	evt.size=fsize;
	    if(fsize==SEND_HOLDOFF_BYTES){
	    	/* enough bytes in to start off */
	    	evt.type=STZX_FILE_START;
	        if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event d queue blocked");
	    }
	    else if( fsize%1000==600){
	    	/* provide an update on the buffer level */
	    	evt.type=STZX_FILE_DATA;
	        if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event d queue blocked");
	    }

	}
	else if (cmd==STZX_FILE_END){
		if(!file_active)
			ESP_LOGE(TAG, "File not open on data write");

		evt.size=fsize;
		if(fsize<SEND_HOLDOFF_BYTES){
			evt.type=STZX_FILE_START;
		    if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event e queue blocked");
		}
		evt.type=STZX_FILE_END;
		if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event queue blocked");
		file_active=FILE_NOT_ACTIVE;
	}

}


static void stzx_task(void*arg)
{
    i2s_event_t evt;
    size_t bytes_written;
    size_t buffered_file_count=0;
	uint8_t active_file=FILE_NOT_ACTIVE;
    while(1){
		if(pdTRUE ==  xQueueReceive( event_queue, &evt, 10 / portTICK_RATE_MS ) ) { // todo: why need wait time here?
			bytes_written=0;

			if(evt.type==(i2s_event_type_t)STZX_FILE_START){
				buffered_file_count=evt.size;
                if(pdTRUE != xQueueReceive( file_data_queue, &active_file, 1 ) ) ESP_LOGE(TAG, "File Tag not available");
				ESP_LOGW(TAG, "STZX_FILE_START, %d, tag %d",buffered_file_count,active_file);
			}
			else if(evt.type==(i2s_event_type_t)STZX_FILE_DATA){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_DATA, %d",buffered_file_count);
			}
			else if(evt.type==(i2s_event_type_t)STZX_FILE_END){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_END, %d",buffered_file_count);
			}
			else if(evt.type==I2S_EVENT_TX_DONE){
				for(char i=0;i<2;i++){
					if (fill_buf_from_file(i2s_writ_buff,file_data_queue,buffered_file_count,active_file )){
						buffered_file_count=0;
						memset(&zxfile,0,sizeof(zxfile));
						zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(400); // break btw files
						ESP_LOGW(TAG, "ENDFILE %d",active_file);
						active_file=FILE_NOT_ACTIVE;
						file_busy--;
					}
					i2s_write(STZX_I2S_NUM, i2s_writ_buff, STZX_I2S_WRITE_LEN_BYTES, &bytes_written, 0);
					if (bytes_written!=STZX_I2S_WRITE_LEN_BYTES){
						ESP_LOGW(TAG, "len mismatch a, %d %d",bytes_written,STZX_I2S_WRITE_LEN_BYTES);
					}
				}
				//if (fill_buf_from_file(i2s_writ_buff,file_data_queue,buffered_file_count)) buffered_file_count=0;
				//i2s_write(STZX_I2S_NUM, i2s_writ_buff, STZX_I2S_WRITE_LEN_BYTES, &bytes_written, 0);
				//if (bytes_written!=STZX_I2S_WRITE_LEN_BYTES){
				//	ESP_LOGW(TAG, "len mismatch b, %d %d",bytes_written,STZX_I2S_WRITE_LEN_BYTES);
				//}
			}else{
				ESP_LOGW(TAG, "Unexpected evt %d",evt.type);
			}
		}
    }
}

