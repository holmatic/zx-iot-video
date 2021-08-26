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

//#include "zx_server.h"

#include "tape_io.h"
#include "tape_signal.h"

static const char* TAG = "taps";

static void taps_task(void*arg);

#define OVERSAMPLE 1
#define MILLISEC_TO_BYTE_SAMPLES(ms)   (OVERSAMPLE*ms*TAPIO_SAMPLE_SPEED_HZ/1000/8) 
#define USEC_TO_BYTE_SAMPLES(ms)   (OVERSAMPLE*(ms*TAPIO_SAMPLE_SPEED_HZ/1000)/1000/8) 

#define USEC_TO_SAMPLES(ms)   (OVERSAMPLE*(ms*TAPIO_SAMPLE_SPEED_HZ/1000)/1000) 
#define SAMPLES_to_USEC(samples)   (samples*1000/(OVERSAMPLE*TAPIO_SAMPLE_SPEED_HZ/1000)) 


#define BYTE_SAMPLES_to_USEC(bytes)   (bytes*8000/(OVERSAMPLE*TAPIO_SAMPLE_SPEED_HZ/1000)) 



#if 0



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
	    while(ix<MAX_TRANSFER_LEN_BYTES){
			 set_sample(samplebuf,ix++,  IDLE_LEVEL );
		}
		if(zxfile.remaining_wavsamples) zxfile.remaining_wavsamples=zxfile.remaining_wavsamples>MAX_TRANSFER_LEN_BYTES? zxfile.remaining_wavsamples-MAX_TRANSFER_LEN_BYTES :0;
	}else if(file_tag==FILE_TAG_COMPRESSED){
		if(zxfile.bitcount==0 && !zxfile.preamble_done){
			zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(100); // maybe needed to not react too fast on menu
			zxfile.preamble_done=1;
		}
	    while(ix<MAX_TRANSFER_LEN_BYTES) {
			if(!zxfile.startbit_done && zxfile.remaining_wavsamples==0){
					/* good point to possibly exit here as one full byte is done...*/
					if(ix>MAX_TRANSFER_LEN_BYTES-50*OVERSAMPLE){
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
	    while(ix<MAX_TRANSFER_LEN_BYTES) {
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
						if(ix>MAX_TRANSFER_LEN_BYTES-250){
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

#endif 

static QueueHandle_t tx_cmd_queue;
static QueueHandle_t rx_evt_queue;

static void taps_task(void*arg)
{
    //i2s_event_t evt;
    //size_t buffered_file_count=0;
    
	while(true){
		/* receive only */
		tapio_clear_transmit_buffers();
		while( uxQueueMessagesWaiting( tx_cmd_queue )==0  ){
			tapio_process_next_transfer(0);
		}
		/* handle transmit */



	}
#if 0

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
			if (fill_buf_from_file(tapio_transmit_buffer[active_transfer_ix],file_data_queue,buffered_file_count,active_file,&bytes_to_send )){
				buffered_file_count=0;
				memset(&zxfile,0,sizeof(zxfile));
				//zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(400); // break btw files>>> will be done at start
				ESP_LOGD(TAG, "ENDFILE %d",active_file);
				active_file=FILE_NOT_ACTIVE;
				file_busy=1;	// todo only set back after 
			}
			if(bytes_to_send){
				if(num_active_transfers && num_active_transfers>=NUM_PARALLEL_TRANSFERS-1){
					wait_and_finish_transfer(spi);
					ESP_LOGD (TAG, "SEND SPI dwait done");
					num_active_transfers--;
				}
				start_transfer(spi, active_transfer_ix, bytes_to_send*8);
				ESP_LOGD (TAG, "SEND SPI data %x %d  wv%d f%d",active_transfer_ix, bytes_to_send,zxfile.remaining_wavsamples,buffered_file_count);
				num_active_transfers++;
				/* use alternating buffers */
				active_transfer_ix = (active_transfer_ix+1) % NUM_PARALLEL_TRANSFERS;
			}
		}
		if (file_busy==1) file_busy=0;
    }
	#endif
}

#if 0


static zxserv_evt_type_t phase=ZXSG_INIT;

static const char* phasenames[]={"INIT","SLOW-50Hz","SLOW-60Hz","SAVE","SILENCE","HIGH","NOISE"};

static void set_det_phase(zxserv_evt_type_t newphase)
{
	if(newphase!=phase){
		phase=newphase;
		zxsrv_send_msg_to_srv( newphase, 0,0);
		ESP_LOGI(TAG,"Detected %s \n", phasenames[newphase-ZXSG_INIT]  );
	}
}
#endif

typedef enum {
    TAPRF_INIT     = 0,             /*!< initial status */
    TAPRF_HDR_STARTED   = 1,        /*!< possibly geceiving header */
    TAPRF_HDR_RECEIVED   = 2,        /*!< start retriving file */
    TAPRF_RETRIEVE_DATA   = 3,       /*!< during file transfer, check if ZX loads or ignores */
} taprfs_state_t;


typedef struct tap_rec_status_tag
{
    taprfs_state_t state;
    uint8_t bitcount;
    uint8_t data;
    uint16_t pulscount;
    uint16_t e_line;
    uint16_t bytecount;
    uint16_t namelength;
} tap_rec_status_t;

static tap_rec_status_t recfile;    // some signal statistics
#if 1

static void recfile_bit(uint8_t bitval)
{
    if(bitval) recfile.data |= (0x80>>recfile.bitcount);
    if(++recfile.bitcount>=8) {
        // have a byte
        if(recfile.bytecount%1000<=40) ESP_LOGI(TAG,"recfile byte %d data %02X",recfile.bytecount,recfile.data );
//        if(recfile.bytecount == recfile.namelength+16404-16393) recfile.e_line=recfile.data;
 //       if(recfile.bytecount == recfile.namelength+16405-16393) {
 //           recfile.e_line+=recfile.data<<8;
  //          ESP_LOGI(TAG,"File E_LINE %d - len %d+%d\n",recfile.e_line,recfile.e_line-16393,recfile.namelength);
   //     }
	//	zxsrv_send_msg_to_srv( ZXSG_FILE_DATA, recfile.bytecount, recfile.data);

        recfile.bitcount=0;
        recfile.bytecount++;
        // zx memory image is preceided by a name that end with the first inverse char (MSB set)
     //   if (recfile.namelength==0 && (recfile.data&0x80) ) recfile.namelength=recfile.bytecount;
        recfile.data=0;
     //   set_det_phase(ZXSG_SAVE);
    }
}


static void recfile_finalize()
{
	ESP_LOGI(TAG,"recfile finalize  %d bytes, %d bits",recfile.bytecount,recfile.bitcount );
	recfile.state=TAPRF_INIT;

}

#endif

static void rec_hdr_puls(uint32_t duration){
	if(recfile.state==TAPRF_INIT){
		recfile.pulscount=0;
		recfile.state=TAPRF_HDR_STARTED;
	} else if(recfile.state==TAPRF_HDR_STARTED || recfile.state==TAPRF_HDR_RECEIVED){
		recfile.pulscount++;
		if(recfile.pulscount==256){ /* ace ROM reuires 256 pulses, choose about th same here */
			ESP_LOGW(TAG,"TAPRF_HDR_RECEIVED\n");
			recfile.state=TAPRF_HDR_RECEIVED;
		}
//	} else if (recfile.state==TAPRF_RETRIEVE_DATA){
//		recfile_bit(1);
	} else if (recfile.state==TAPRF_RETRIEVE_DATA){
		ESP_LOGW(TAG,"Follow-up Header %d %d ",recfile.bitcount,recfile.bytecount);
		recfile_finalize();
		recfile.pulscount=0;
		recfile.state=TAPRF_HDR_STARTED;
	} else {
		ESP_LOGW(TAG,"unknown HDR puls ");
		recfile.state=TAPRF_INIT;
	}
}

static void rec_0_puls(){

	if(recfile.state==TAPRF_HDR_RECEIVED){
		ESP_LOGW(TAG,"TAPRF_RETRIEVE_DATA0\n");
		recfile.state=TAPRF_RETRIEVE_DATA;
		recfile.bytecount=0;
		recfile.bitcount=0;
    	recfile.data=0;
	} else if (recfile.state==TAPRF_RETRIEVE_DATA){
		recfile_bit(0);
	} else {
		ESP_LOGW(TAG,"unknown 0 puls ");
		recfile.state=TAPRF_INIT;
	}

}

static void rec_1_puls(uint32_t duration){
	if(recfile.state==TAPRF_HDR_STARTED || recfile.state==TAPRF_HDR_RECEIVED){
		ESP_LOGW (TAG, "rec_1_puls during HDR %d us", SAMPLES_to_USEC(duration) );
	} else if (recfile.state==TAPRF_RETRIEVE_DATA){
		recfile_bit(1);
	} else {
		ESP_LOGW(TAG,"unknown 1 puls ");
		recfile.state=TAPRF_INIT;
	}
}

static void rec_noise_puls(){
	if(recfile.state!=TAPRF_INIT){
		ESP_LOGW(TAG,"RESET after noise pulse\n");
	}
	recfile.state=TAPRF_INIT;
}


static bool actual_logic_level=0;
static uint32_t level_cnt=0;
static uint32_t puls_cnt=0;


static void analyze_1_to_0(uint32_t duration){
	// end of high phase,  245 for 0 , 488 for 1 , or 618us for pilot, 277 for end mark, 1.288 for gap 
	if (duration<USEC_TO_SAMPLES(150))   rec_noise_puls();
	else if (duration<=USEC_TO_SAMPLES(350))   rec_0_puls();
	else if (duration<=USEC_TO_SAMPLES(550))   rec_1_puls(duration);
	else if (duration<=USEC_TO_SAMPLES(680))   rec_hdr_puls(duration);


	if(duration<2 || duration>500 || (puls_cnt&0x1ff)< 5 )	ESP_LOGW (TAG, "High pulse %d smpls, %d us",duration, SAMPLES_to_USEC(duration) );
	puls_cnt++;

}

static void check_on_const_level(){
	if(recfile.state!=TAPRF_INIT && level_cnt>USEC_TO_SAMPLES(3000)){
		if (recfile.state==TAPRF_RETRIEVE_DATA)
			recfile_finalize();
		else{
			ESP_LOGW(TAG,"RESET after silence\n");
		}
		recfile.state=TAPRF_INIT;
	}
}

static void analyze_0_to_1(uint32_t duration){
	if(duration>1000)	ESP_LOGW (TAG, "High after long low - %d smpls, %d us",duration, SAMPLES_to_USEC(duration));
}

int __builtin_clz (unsigned int x);
int __builtin_ctz (unsigned int x); // trailing zeros

/* every incoming 8-bit sample MSB first */
void IRAM_ATTR rx_checksample(uint8_t data)
{
	if (data==0) {
		if(actual_logic_level){
			analyze_1_to_0(level_cnt);
			actual_logic_level=false;
			level_cnt=8;
		} else {
			level_cnt+=8;
		}
	} else if (data==0xff){
		if(!actual_logic_level){
			analyze_0_to_1(level_cnt);
			actual_logic_level=true;
			level_cnt=8;
		} else {
			level_cnt+=8;
		}
	} else {
		// level change within byte, assume just one transition
		int cnt_newlvl = __builtin_ctz( data^(actual_logic_level?0:0xff)  );
		level_cnt+=8-cnt_newlvl;
		if(actual_logic_level)
			analyze_1_to_0(level_cnt);
		else
			analyze_0_to_1(level_cnt);
		actual_logic_level=!actual_logic_level;
		level_cnt=cnt_newlvl;
	} 
}


static void on_rx_data(uint8_t* data, int size_bits){
	static uint32_t acc_kbytes=0;
	ESP_LOGD (TAG, "on_rx_data %d bits",size_bits);
	acc_kbytes+=size_bits/8192;
	if ((acc_kbytes&0xff)==0) ESP_LOGW (TAG, "on_rx_data acc %d Mbytes %x, plscnt=%d ",acc_kbytes/1024,data[0],puls_cnt);
	// todo we could quick-check for all-0 here as this is the usual case
	if( level_cnt>USEC_TO_SAMPLES(2500) && !actual_logic_level){
		for(int i=0;i<size_bits/8;i++){
			if(data[i]) goto not_just_all_0;
		}
		// simply have all-0
		level_cnt+=size_bits;
		check_on_const_level();
	} else {

not_just_all_0:

		for(int i=0;i<size_bits/8;i++){
			rx_checksample(data[i]);
		}
	}
}
