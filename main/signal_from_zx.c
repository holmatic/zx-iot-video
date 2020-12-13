#include <stdio.h>
#include <string.h>
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "zx_server.h"
#include "signal_from_zx.h"


static const char* TAG = "sfzx";




#define USEC_TO_SAMPLES(us)   (us*20/32) 
#define MILLISEC_TO_SAMPLES(us)   (us*20000/32) 
static inline uint32_t samples_to_usec(uint32_t samples){
	return samples*32/20;
}



static uint32_t data_num_0=0;
static uint32_t data_num_1=0;
static bool data_vid_active=0;

static uint32_t data_num_short_0=0;


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


typedef enum {
    ZXFS_INIT     = 0,        /*!< initial status */
    ZXFS_HDR_RECEIVED   = 1,        /*!< start retriving file */
    ZXFS_RETRIEVE_DATA   = 2,                 /*!< during file transfer, check if ZX loads or ignores */
} zxfs_state_t;


typedef struct zxfile_rec_status_info
{
    zxfs_state_t state;
    uint8_t pulscount;
    uint8_t bitcount;
    uint8_t data;
    uint16_t e_line;
    uint16_t bytecount;
    uint16_t namelength;
} zxfile_rec_status_t;

static zxfile_rec_status_t zxfile;    // some signal statistics


static void zxfile_bit(uint8_t bitval)
{
    if(bitval) zxfile.data |= (0x80>>zxfile.bitcount);
    if(++zxfile.bitcount>=8) {
        // have a byte
        if(zxfile.bytecount%100<=1) ESP_LOGI(TAG,"ZXFile byte %d data %x\n",zxfile.bytecount,zxfile.data );
        if(zxfile.bytecount == zxfile.namelength+16404-16393) zxfile.e_line=zxfile.data;
        if(zxfile.bytecount == zxfile.namelength+16405-16393) {
            zxfile.e_line+=zxfile.data<<8;
            ESP_LOGI(TAG,"File E_LINE %d - len %d+%d\n",zxfile.e_line,zxfile.e_line-16393,zxfile.namelength);
        }
		zxsrv_send_msg_to_srv( ZXSG_FILE_DATA, zxfile.bytecount, zxfile.data);

        zxfile.bitcount=0;
        zxfile.bytecount++;
        // zx memory image is preceided by a name that end with the first inverse char (MSB set)
        if (zxfile.namelength==0 && (zxfile.data&0x80) ) zxfile.namelength=zxfile.bytecount;
        zxfile.data=0;
        set_det_phase(ZXSG_SAVE);
    }
}

static void zxfile_check_bit_end()
{
    //if(zxfile.bytecount%50==2) printf(" ZXFile bit %d pulses (%d us) \n",zxfile.pulscount,samples_to_usec(level.duration) );
    // test have shown that the 4 and 9 pulses are retrieved quite precisely, nevertheless add some tolerance
    if(zxfile.pulscount>=3 && zxfile.pulscount<=5){
        zxfile_bit(0);
    }
    else if(zxfile.pulscount>=7 && zxfile.pulscount<=11){
        zxfile_bit(1);
    }
    else{
        ESP_LOGI(TAG,"File read retrieved %d pulses, cancel\n",zxfile.pulscount);
        zxfile.state=ZXFS_INIT;
    }
    zxfile.pulscount=0;
}


static void analyze_1_to_0(uint32_t duration)
{
	// end of high phase
	if (zxfile.state>=ZXFS_HDR_RECEIVED){
		if(duration>=USEC_TO_SAMPLES(90) && duration<=USEC_TO_SAMPLES(250)){ // should be 150u
			++zxfile.pulscount;
		}
	}
}


static void analyze_0_to_1(uint32_t duration)
{

	if(duration>USEC_TO_SAMPLES(300) && duration<USEC_TO_SAMPLES(600))
	{
		// could be sync, but this will be handled by the video logic, so only acti if we are not already in display mode
		if(phase!=ZXSG_SLOWM_50HZ && phase!=ZXSG_SLOWM_60HZ) set_det_phase(ZXSG_NOISE);
	}

	if (zxfile.state>=ZXFS_HDR_RECEIVED){
		if(duration<USEC_TO_SAMPLES(250)){
				// okay, gap should be 150u for pules
		} else if (duration>USEC_TO_SAMPLES(1200) && duration<USEC_TO_SAMPLES(1800)){ // 1.3ms+0.15
			zxfile_check_bit_end();
		}
		else
		{
			ESP_LOGI(TAG,"File gap retrieved of %d usec, cancel with %d bytes\n",samples_to_usec(duration),zxfile.bytecount);
			zxfile.state=ZXFS_INIT;
		}
	} else { /* no header yet */
		 if ( !data_vid_active )
		 {
			 // No file but also no display nor silence
			 if(duration>USEC_TO_SAMPLES(300) && duration<USEC_TO_SAMPLES(2000)) set_det_phase(ZXSG_NOISE);
		 }

	}


	if(zxfile.state<ZXFS_HDR_RECEIVED && duration>MILLISEC_TO_SAMPLES(60))
	{
		// end of long break, could be header of file
		memset(&zxfile,0,sizeof(zxfile_rec_status_t));
		zxfile.state=ZXFS_HDR_RECEIVED;
	}
}



/* report if we have a regular video signal or are in FAST/LOAD/SAVE/ect */
void sfzx_report_video_signal_status(bool vid_is_active){
	if(data_vid_active!=vid_is_active){
		data_num_0=1;
		data_num_1=1;
		zxfile.state=ZXFS_INIT;
		ESP_LOGI(TAG,"Video signal status: %s\n", vid_is_active?"active":"inactive" );
		if(vid_is_active) set_det_phase(ZXSG_SLOWM_50HZ);
	}
	data_vid_active=vid_is_active;
} 


/* every incoming 32-bit sample as long as vid_is_active=false */
void sfzx_checksample(uint32_t data)
{
	if (data==0) {
		data_num_0++;
		if( data_num_1 ){
			if (data_num_0 > USEC_TO_SAMPLES(14) ){
				// not just hsync, have 1-to-0
				analyze_1_to_0(data_num_1);
				data_num_1=0;
				data_num_short_0=0;
			}
			else{
				data_num_short_0++;
				data_num_1++; // count for continuation through hsync
			}
		}
	} else if (data==0xffffffff){
		data_num_1++;
		if(data_num_0> USEC_TO_SAMPLES(14)){
			// have 0-to-1
			analyze_0_to_1(data_num_0);
			data_num_short_0=0;
		}
		data_num_0=0;
	} 
//	else
//	{
//		//  ignore all mixed-bit words in terms of transitions, but continue measureing duration 
//		if(data_num_1) data_num_1++; // count for continuation through hsync
//		if(data_num_0) data_num_0++; // count for continuation through hsync
//	}
}

/* called periodically at roughly millisec scale */
void sfzx_periodic_check(){
	if(data_num_1 > MILLISEC_TO_SAMPLES(80)){
		if(data_num_short_0 > data_num_1/16)	/* we see the H sync pulses but no V sync */	
			set_det_phase(ZXSG_HIGH); /* more than two frames high !*/
		else {
			/* silence, normally constant-low but due to hi pass filter is seen for us as constant high with no hsync */
			set_det_phase(ZXSG_SILENCE); 
		}
	}
	else if (data_num_0 > MILLISEC_TO_SAMPLES(100)) {
			/* silence, normally  constant-low would not be visible as low - 
			due to high pass filter, but if for any reason we see it nevertheles, we can also react accordingly */
			set_det_phase(ZXSG_SILENCE); 
		}
}          


void sfzx_init()
{}

