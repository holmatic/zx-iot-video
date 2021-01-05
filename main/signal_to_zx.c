#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"	// TODO remove i2s_event_t
#include "driver/spi_master.h"

#include "zx_server.h"
#include "signal_to_zx.h"

#define  NUM_PARALLEL_TRANSFERS  3
#define  MAX_WRITE_LEN_BYTES  (2048*4) // 6k is Enough for 1K straight w/o oversample
#define OVERSAMPLE 4
static const char* TAG = "stzx";

static spi_device_handle_t spi;

static void stzx_task(void*arg);

static QueueHandle_t event_queue=NULL;
static uint8_t* write_buffer[NUM_PARALLEL_TRANSFERS];
static  QueueHandle_t file_data_queue=NULL;
 
#define MILLISEC_TO_BYTE_SAMPLES(ms)   (OVERSAMPLE*ms*125000/1000/8) 
#define USEC_TO_BYTE_SAMPLES(ms)   (OVERSAMPLE*(ms*125)/1000/8) 

static void send_buffer(spi_device_handle_t spi, uint8_t parallel_trans_ix, int len_bits)
{
    //esp_err_t ret;

    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
    static spi_transaction_t trans[NUM_PARALLEL_TRANSFERS];
	//memset(&trans, 0, sizeof(spi_transaction_t));
    //In theory, it's better to initialize trans and data only once and hang on to the initialized
    //variables. We allocate them on the stack, so we need to re-init them each call.
    trans[parallel_trans_ix].tx_buffer=write_buffer[parallel_trans_ix];        //finally send the signal data
    trans[parallel_trans_ix].rx_buffer=NULL; 
    trans[parallel_trans_ix].length=len_bits;          //Data length, in bits
    trans[parallel_trans_ix].rxlength=0;  // is overitten on static, so set new 
    trans[parallel_trans_ix].flags=0; //undo SPI_TRANS_USE_TXDATA flag

    //Queue transaction.
    ESP_ERROR_CHECK(spi_device_queue_trans(spi, &trans[parallel_trans_ix], portMAX_DELAY));

    //When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
    //mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
    //finish because we may as well spend the time calculating the next line. When that is done, we can call
    //send_line_finish, which will wait for the transfers to be done and check their status.
}


static void send_buffer_finish(spi_device_handle_t spi)
{
    spi_transaction_t *rtrans;
	ESP_ERROR_CHECK(spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY));
}

/**
 * @brief I2S ADC/DAC mode init.
 */
void stzx_init()
{
   esp_err_t ret;

    spi_bus_config_t buscfg={
        .miso_io_num=-1,
        .mosi_io_num=22,
        .sclk_io_num=-1,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=MAX_WRITE_LEN_BYTES
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=OVERSAMPLE*125*1000,           //Clock out at 125khz MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=-1,               //CS pin
        .queue_size=3,                          //We want to be able to queue 7 transactions at a time
		.cs_ena_posttrans=0,
		.cs_ena_pretrans=0,
		.flags=SPI_DEVICE_NO_DUMMY /* write only (maybe also faster?) */
        //.pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 2); // TODO check DMA channel #
    ESP_ERROR_CHECK(ret);
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
	ret=spi_device_acquire_bus(spi, portMAX_DELAY);  // ccupy the SPI bus for a device to do continuous transactions. 
    ESP_ERROR_CHECK(ret);

	for(int i=0;i<NUM_PARALLEL_TRANSFERS;i++){
	    write_buffer[i]=heap_caps_malloc(MAX_WRITE_LEN_BYTES, MALLOC_CAP_DMA);
    	if(!write_buffer[i]) printf("calloc of %d failed\n",MAX_WRITE_LEN_BYTES);
	}

	event_queue=xQueueCreate(8,sizeof(i2s_event_t));


    xTaskCreate(stzx_task, "stzx_task", 1024 * 3, NULL, 9, NULL);
}

static uint8_t outlevel_inv=0;

void stzx_set_out_inv_level(bool inv)
{
	outlevel_inv = inv ? 0xff : 0;
}

static inline void set_sample(uint8_t* samplebuf, uint32_t ix, uint8_t val)
{
    samplebuf[ix]=val;
    //samplebuf[ix]=val ^ outlevel_inv;  // convert for endian byteorder
    
	
	//samplebuf[ix^0x0003]=val ^ outlevel_inv;  // convert for endian byteorder
}
#if OVERSAMPLE>1
const uint8_t wav_zero[]={  
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00
		};

const uint8_t wav_one[]={
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,

		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,

		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00

	   };
const uint8_t wav_compr_hdr_std[]={ 0xff,0xff,0xff,0xff,0x00,0x00};
const uint8_t wav_compr_hdr_inv[]={ 0x00,0x00,0x00,0x00,0xff,0xff};
const uint8_t wav_compr_zero[]={ 0xff,0xf8}; // zx81 has iverted input (negative pulse from DC results in 0->1 reading)
const uint8_t wav_compr_one[]={ 0x00,0x07};

#else
const uint8_t wav_zero[]={  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00 };
const uint8_t wav_one[]={   0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00 };
const uint8_t wav_compr_hdr_std[]={ 0xff,0xf0};
const uint8_t wav_compr_hdr_inv[]={ 0x00,0x0f};
#endif

//const uint8_t wav_compr_hdr[]={ 0xff,0xf0};



typedef struct zxfile_wr_status_info
{
    const uint8_t* wavsample; // pointer to sample, if zero, play
    uint32_t remaining_wavsamples; //
    uint32_t bitcount;
    uint8_t data;
    uint8_t startbit_done; // needed for compressed only
    uint8_t preamble_done; // 
} zxfile_wr_status_t;

static zxfile_wr_status_t zxfile;    // some signal statistics

#define DEBUG_TRANSFERSWITCH 0   // will add some artefacts to observe the gaps between transfers in the oscilloscope

#define IDLE_LEVEL 0x00


// return true if end of file reached
static bool fill_buf_from_file(uint8_t* samplebuf, QueueHandle_t dataq, size_t buffered_filesize, uint8_t file_tag, uint32_t *actual_len_to_fill)
{
	bool end_f=false;
    uint32_t ix=0;

#if DEBUG_TRANSFERSWITCH
	set_sample(samplebuf,ix++,  0xaa );	// mark signals
	for(int d=0;d<30;d++){
		set_sample(samplebuf,ix++,  0xff );
	}
#endif

	if(file_tag==FILE_NOT_ACTIVE){
	    while(ix<MAX_WRITE_LEN_BYTES){
			 set_sample(samplebuf,ix++,  IDLE_LEVEL );
		}
		if(zxfile.remaining_wavsamples) zxfile.remaining_wavsamples=zxfile.remaining_wavsamples>MAX_WRITE_LEN_BYTES? zxfile.remaining_wavsamples-MAX_WRITE_LEN_BYTES :0;
	}else if(file_tag==FILE_TAG_COMPRESSED){
		if(zxfile.bitcount==0 && !zxfile.preamble_done){
			zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(100); // maybe needed to not react too fast on menu
			zxfile.preamble_done=1;
		}
	    while(ix<MAX_WRITE_LEN_BYTES) {
			if(!zxfile.startbit_done && zxfile.remaining_wavsamples==0){
					/* good point to possibly exit here as one full byte is done...*/
					if(ix>MAX_WRITE_LEN_BYTES-50*OVERSAMPLE){
						// end of packet will create a 30-60us break at low level, shorter for bigger OVERSAMPLE
						break;
					} 
					zxfile.wavsample=outlevel_inv ? wav_compr_hdr_inv : wav_compr_hdr_std;
					zxfile.remaining_wavsamples=sizeof(wav_compr_hdr_std);
					zxfile.startbit_done=1;
			}
			if(zxfile.remaining_wavsamples){
				set_sample(samplebuf,ix++, zxfile.wavsample ? *zxfile.wavsample++ : IDLE_LEVEL );
				--zxfile.remaining_wavsamples;
			} else {
				zxfile.wavsample=NULL; // after sample, switch back
				if(zxfile.bitcount < buffered_filesize*8 ) {
					if( (zxfile.bitcount&7) ==0){
						if(pdTRUE != xQueueReceive( dataq, &zxfile.data, 0 ) ) ESP_LOGE(TAG, "End of data");
					}

#if OVERSAMPLE > 1
					/* use != operator as logical XOR */
					if(  (0==(zxfile.data &  (0x80 >> (zxfile.bitcount&7) ) ) ) != (outlevel_inv!=0)   ){
						zxfile.wavsample=wav_compr_zero;  // 0 data is high level for std ZX81 
						zxfile.remaining_wavsamples=sizeof(wav_compr_zero);
					}else{
						zxfile.wavsample=wav_compr_one;
						zxfile.remaining_wavsamples=sizeof(wav_compr_one);
					};
					zxfile.bitcount++;
#else
					uint8_t smpl;
					smpl=0;
					if(0==(zxfile.data & (0x80 >> (zxfile.bitcount&7)  ))) smpl|=0xf0;
					zxfile.bitcount++;
					if(0==(zxfile.data & (0x80 >> (zxfile.bitcount&7)  ))) smpl|=0x0f;
					zxfile.bitcount++;
					smpl ^= outlevel_inv ? 0xee : 0x11;
					set_sample(samplebuf,ix++,  smpl );
#endif
					if( (zxfile.bitcount&7) ==0){
						zxfile.startbit_done=0;
					}
				} else {
					set_sample(samplebuf,ix++,  IDLE_LEVEL );
					if(!end_f)	ESP_LOGW(TAG, "End compr file");
					end_f=true;
					break;
				}
			}
		}
	}else{
		/* uncompressed, starts with silence */
		if(zxfile.bitcount==0 && !zxfile.preamble_done){
			zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(200); // break btw files)
			zxfile.preamble_done=1;
		}
	    while(ix<MAX_WRITE_LEN_BYTES) {
			if(zxfile.remaining_wavsamples){
				set_sample(samplebuf,ix++, zxfile.wavsample ? *zxfile.wavsample++ : IDLE_LEVEL );
				--zxfile.remaining_wavsamples;
			} else {
				if (zxfile.wavsample){ // after sample, always insert silence
					zxfile.wavsample=NULL;
					zxfile.bitcount++;  // prepare for next bit
					zxfile.remaining_wavsamples=USEC_TO_BYTE_SAMPLES(1300);
				} else {
					if(zxfile.bitcount < buffered_filesize*8 ) {
						/* good point to exit here as one bit is just done...*/
						if(ix>MAX_WRITE_LEN_BYTES-250){
#if DEBUG_TRANSFERSWITCH
							set_sample(samplebuf,ix++,  0xaa ); // mark signals
							for(int d=0;d<50;d++)
								set_sample(samplebuf,ix++,  0xff );
							set_sample(samplebuf,ix++,  0x55 );
#endif
							break;
						}
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
						set_sample(samplebuf,ix++,  IDLE_LEVEL );
						if(!end_f)	ESP_LOGW(TAG, "End std file");
						end_f=true;
						break;
					}
				}
			}
        }
    }
	*actual_len_to_fill=ix;
    return end_f;
}


static uint8_t file_busy=0;


// TOSO - Switch to singular call for transfering a file, as 'pipelined' transfer of multiple files is not really needed
#define SEND_HOLDOFF_BYTES 20000  // enough so we do not run out of data on first chunk even when compressed

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
			file_data_queue=xQueueCreate(16384+512,sizeof(uint8_t));
		}
		if (data!=FILE_TAG_NORMAL && data!=FILE_TAG_COMPRESSED){
			ESP_LOGE(TAG, "Invalid File start mark");
		}

		/* make sur previous file is done and gone, otherwise the byte counting mechanism malfunctios */
		while(file_busy) vTaskDelay(20 / portTICK_RATE_MS);

		file_busy=2;
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
    size_t buffered_file_count=0;
	uint8_t num_active_transfers=0;
	uint8_t active_file=FILE_NOT_ACTIVE;
	uint8_t active_transfer_ix = 0;
    while(1){
		if(pdTRUE ==  xQueueReceive( event_queue, &evt, 2 ) ) {
			if(evt.type==(i2s_event_type_t)STZX_FILE_START){
				buffered_file_count=evt.size;
                if(pdTRUE != xQueueReceive( file_data_queue, &active_file, 1 ) ) ESP_LOGE(TAG, "File Tag not available");
				ESP_LOGW(TAG, "STZX_FILE_START, inv %x  %d, tag %d", outlevel_inv, buffered_file_count,active_file);
			}
			else if(evt.type==(i2s_event_type_t)STZX_FILE_DATA){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_DATA, %d",buffered_file_count);
			}
			else if(evt.type==(i2s_event_type_t)STZX_FILE_END){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_END, %d",buffered_file_count);
			}else{
				ESP_LOGW(TAG, "Unexpected evt %d",evt.type);
			}
		}

		while ( (buffered_file_count && active_file) || zxfile.remaining_wavsamples){
			uint32_t bytes_to_send=0;
			if (fill_buf_from_file(write_buffer[active_transfer_ix],file_data_queue,buffered_file_count,active_file,&bytes_to_send )){
				buffered_file_count=0;
				memset(&zxfile,0,sizeof(zxfile));
				//zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(400); // break btw files>>> will be done at start
				ESP_LOGW(TAG, "ENDFILE %d",active_file);
				active_file=FILE_NOT_ACTIVE;
				file_busy=1;	// todo only set back after 
			}
			if(bytes_to_send){
				if(num_active_transfers && num_active_transfers>=NUM_PARALLEL_TRANSFERS-1){
					send_buffer_finish(spi);
					//ESP_LOGW(TAG, "SEND SPI dwait done");
					//vTaskDelay(1);
					num_active_transfers--;
				}
				send_buffer(spi, active_transfer_ix, bytes_to_send*8);
				ESP_LOGW(TAG, "SEND SPI data %x %d  wv%d f%d",active_transfer_ix, bytes_to_send,zxfile.remaining_wavsamples,buffered_file_count);
				num_active_transfers++;
				/* use alternating buffers */
				active_transfer_ix = (active_transfer_ix+1) % NUM_PARALLEL_TRANSFERS;
			}
		}
		if (file_busy==1) file_busy=0;
    }
}

