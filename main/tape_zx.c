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

static const char* TAG = "tapzx";


#define MILLISEC_TO_BYTE_SAMPLES(ms)   (TAPIO_OVERSAMPLE*ms*TAPIO_SAMPLE_SPEED_HZ/1000/8) 
#define USEC_TO_BYTE_SAMPLES(ms)   (TAPIO_OVERSAMPLE*(ms*TAPIO_SAMPLE_SPEED_HZ/1000)/1000/8) 

#define USEC_TO_SAMPLES(ms)   (TAPIO_OVERSAMPLE*(ms*TAPIO_SAMPLE_SPEED_HZ/1000)/1000) 
#define SAMPLES_to_USEC(samples)   (samples*1000/(TAPIO_OVERSAMPLE*TAPIO_SAMPLE_SPEED_HZ/1000)) 


#define BYTE_SAMPLES_to_USEC(bytes)   (bytes*8000/(TAPIO_OVERSAMPLE*TAPIO_SAMPLE_SPEED_HZ/1000)) 




static uint8_t outlevel_inv=0;

void stzx_set_out_inv_level(bool inv)
{
	outlevel_inv = inv ? 0xff : 0;
}

static inline void set_sample(uint8_t* samplebuf, uint32_t ix, uint8_t val)
{
    samplebuf[ix]=val;
}
#if TAPIO_OVERSAMPLE>1
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
	size_t overall_filesize;
	uint8_t *currsrc;	/* while transmitting file, this holds the read position, either in name or data area*/
	uint8_t file_tag;
} zxfile_wr_status_t;

static zxfile_wr_status_t zxfile;    // some signal statistics

static taps_tx_packet_t txpacket;    // current transmission

#define DEBUG_TRANSFERSWITCH 0   // will add some artefacts to observe the gaps between transfers in the oscilloscope

#define IDLE_LEVEL 0x00


//typedef struct taps_tx_packet_tag{
//	taps_txdata_id_t packet_type_id;
//	uint8_t *name;			/*!<  pointer to transmit name,  must be valid until packet done, NULL if NA  */
//	uint32_t namesize;		/*!<  size of data, must be 0 if *name is NULL  */
//	uint8_t *data;			/*!<  pointer to transmit data, must be valid until packet done, NULL if NA  */
//	uint32_t datasize;		/*!<  size of data, must be 0 if *data is NULL  */
//	uint32_t para;			/*!<  generic parameter, usage depends on packet_type_id   inversion level for qload  */ 
//} taps_tx_packet_t ;


void stzx_setup_new_file(const taps_tx_packet_t* txp){
	txpacket=*txp;
	memset(&zxfile,0,sizeof(zxfile));
	zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(100); // break btw files will be done at start
	zxfile.overall_filesize=txpacket.namesize+txpacket.datasize;
	zxfile.currsrc=txpacket.namesize ? txpacket.name : txpacket.data;
	zxfile.file_tag=txpacket.packet_type_id;
	outlevel_inv=0;// TODOTODDO txpacket.para;
}

static uint8_t getbyte(){
	if (zxfile.bitcount==txpacket.namesize*8) zxfile.currsrc=txpacket.data;	/* switch to data when name done */ 
	return *zxfile.currsrc++;
}

// return true if end of file reached
//static bool fill_buf_from_file(uint8_t* samplebuf, QueueHandle_t dataq, size_t overall_filesize, uint8_t file_tag, uint32_t *actual_len_to_fill)
bool stzx_fill_buf_from_file(uint8_t* samplebuf, size_t buffer_size, uint32_t *actual_len_to_fill){
	bool end_f=false;
    uint32_t ix=0;

#if DEBUG_TRANSFERSWITCH
	set_sample(samplebuf,ix++,  0xaa );	// mark signals
	for(int d=0;d<30;d++){
		set_sample(samplebuf,ix++,  0xff );
	}
#endif

	if(zxfile.file_tag==TTX_DATA_ZX81_QLOAD){
		if(zxfile.bitcount==0 && !zxfile.preamble_done){
			zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(2*100); // maybe needed to not react too fast on menu
			zxfile.preamble_done=1;
			ESP_LOGW(TAG, "Start Qload file");
		}
	    while(ix<buffer_size) {
			if(!zxfile.startbit_done && zxfile.remaining_wavsamples==0){
					/* good point to possibly exit here as one full byte is done...*/
					if(ix>buffer_size-50*TAPIO_OVERSAMPLE){
						// end of packet will create a 30-60us break at low level, shorter for bigger TAPIO_OVERSAMPLE
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
				if(zxfile.bitcount < zxfile.overall_filesize*8 ) {
					if( (zxfile.bitcount&7) ==0){
						zxfile.data=getbyte();
					}

#if TAPIO_OVERSAMPLE == 4
					/* use != operator as logical XOR */
					if(  (0==(zxfile.data &  (0x80 >> (zxfile.bitcount&7) ) ) ) != (outlevel_inv!=0)   ){
						zxfile.wavsample=wav_compr_zero;  // 0 data is high level for std ZX81 
						zxfile.remaining_wavsamples=sizeof(wav_compr_zero);
					}else{
						zxfile.wavsample=wav_compr_one;
						zxfile.remaining_wavsamples=sizeof(wav_compr_one);
					};
					zxfile.bitcount++;
#elif TAPIO_OVERSAMPLE == 1
					uint8_t smpl;
					smpl=0;
					if(0==(zxfile.data & (0x80 >> (zxfile.bitcount&7)  ))) smpl|=0xf0;
					zxfile.bitcount++;
					if(0==(zxfile.data & (0x80 >> (zxfile.bitcount&7)  ))) smpl|=0x0f;
					zxfile.bitcount++;
					smpl ^= outlevel_inv ? 0xee : 0x11;
					set_sample(samplebuf,ix++,  smpl );
#else
	#error "Invalid oversample rate"
#endif
					if( (zxfile.bitcount&7) ==0){
						zxfile.startbit_done=0;
					}
				} else {
					set_sample(samplebuf,ix++,  IDLE_LEVEL );
					if(!end_f)	ESP_LOGW(TAG, "End Qload file");
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
			ESP_LOGW(TAG, "Start Std file");
		}
	    while(ix<buffer_size) {
			if(zxfile.remaining_wavsamples){
				set_sample(samplebuf,ix++, zxfile.wavsample ? *zxfile.wavsample++ : IDLE_LEVEL );
				--zxfile.remaining_wavsamples;
			} else {
				if (zxfile.wavsample){ // after sample, always insert silence
					zxfile.wavsample=NULL;
					zxfile.bitcount++;  // prepare for next bit
					zxfile.remaining_wavsamples=USEC_TO_BYTE_SAMPLES(1300);
				} else {
					if(zxfile.bitcount < zxfile.overall_filesize*8 ) {
						/* good point to exit here as one bit is just done...*/
						if(ix>buffer_size-250){
#if DEBUG_TRANSFERSWITCH
							set_sample(samplebuf,ix++,  0xaa ); // mark signals
							for(int d=0;d<50;d++)
								set_sample(samplebuf,ix++,  0xff );
							set_sample(samplebuf,ix++,  0x55 );
#endif
							break;
						}
						if( (zxfile.bitcount&7) ==0){
							zxfile.data=getbyte();
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
						if(!end_f)	ESP_LOGW(TAG, "End Std file");
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


