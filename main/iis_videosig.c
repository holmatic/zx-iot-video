#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "esp_adc_cal.h"
#include "zx_server.h"
#include "user_knob.h"
#include "signal_from_zx.h"


static const char* TAG = "i2svid";


//i2s number
#define VID_I2S_NUM           (0)//1 is used by VGA..
//i2s sample rate
// 
#define VID_I2S_SAMPLE_RATE   (625000) // times 32 is 20Mhz
#define USEC_TO_BYTE_SAMPLES(us)   (us*VID_I2S_SAMPLE_RATE*4/1000000) 
#define MILLISEC_TO_BYTE_SAMPLES(ms)   (ms*VID_I2S_SAMPLE_RATE*4/1000) 



//i2s data bits
// Will send out 15bits MSB first
#define VID_I2S_SAMPLE_BITS   (32)

#define VID_I2S_BLOCK_LEN_SAMPLES      (1024)
#define VID_I2S_BLOCK_LEN_BYTES      (VID_I2S_BLOCK_LEN_SAMPLES * VID_I2S_SAMPLE_BITS/8)



#define VID_PWMO_PIN	           (25) // 12 make this 25?
#define VID_PWMLEVEL_PIN           (33) // 13 make this 33 as input
#define VID_SIGNALIN_PIN           (32) // 14 make this 32 as input ?


#define STD_NUM_HDR_LINES 24   // upper header - 240 = 24+192+24

static void vid_in_task(void*arg);
static void pwm_timer_callb( TimerHandle_t xTimer );



static uint8_t* i2s_read_buff=NULL;

 
static 	ledc_channel_config_t ledc_channel = {
		.channel    = LEDC_CHANNEL_0,
		.duty       = 0,
		.gpio_num   = VID_PWMO_PIN,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.hpoint     = 0,
		.timer_sel  = LEDC_TIMER_1          
	};






static void pwm_timer_callb( TimerHandle_t xTimer )
{
	static int last_lvl=-1;
	static int stepsize=50;
	static int dutyval=500;
	int lvl;
	lvl=gpio_get_level(VID_PWMLEVEL_PIN);
	if(last_lvl>=0 &&  lvl!=last_lvl ){
		// level change detected
		stepsize=1;
		//ESP_LOGI(TAG,"Vid level PWM - toggle detected to lvl %d , duty %d ",lvl, dutyval);
	}else{
		if(stepsize<100)
		{
			if(stepsize>95)			ESP_LOGE(TAG,"Vid level PWM - FAILURE to regulate, stuck at level %d with dutyval %d ",lvl, dutyval);
			stepsize+=1;
		}
	}
	if(lvl){
		if (dutyval>stepsize)
			dutyval-=stepsize;
		else
			dutyval=0;
	}else {
		if (dutyval<1000-stepsize)
			dutyval+=stepsize;
		else
			dutyval=1000;
	}

	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, dutyval);
	ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	//ESP_LOGI(TAG,"Timer function, lvl %d , duty %d ",lvl, dutyval);
	last_lvl=lvl;
}

static inline uint32_t usec_to_32bit_words(uint32_t usecs){
	return (usecs*20)/32;
}

static inline uint32_t usec_to_samples(uint32_t usecs){
	return (usecs*20);
}

/* some global (ehem) variables */
uint32_t vid_pixel_mem[10*240];
uint8_t vid_scan_startline=0+2;   // default   0, first line that is scanned and put into the pixel mem, lines above are not touched by this module 
uint8_t vid_scan_endline=239-2;   // default 239, last line that is scanned and put into the pixel mem, lines above are not touched by this module


static int last_sync_timer=0; // set >0 when we have a sync, counts down to 0 after sync lost


static uint8_t video_synced_cnt=0;
static bool video_synced_state=false;

static int timeout_verbose=10;


bool vid_is_synced(){
	return video_synced_state;
}

IRAM_ATTR uint32_t vid_get_next_data()
{
	uint32_t *rd_pt= (uint32_t* ) i2s_read_buff;
	uint32_t d;
	static uint32_t data_w_pos=0;
	static uint32_t data_w_count=0;

	while(data_w_pos>=data_w_count){
		size_t bytes_read=0;
		data_w_pos=data_w_count=0;
		//i2s_read(i2s_port_t i2s_num, void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait);
		ESP_ERROR_CHECK(i2s_read(VID_I2S_NUM, (void*) i2s_read_buff, VID_I2S_BLOCK_LEN_BYTES*2, &bytes_read, 3000 / portTICK_RATE_MS)); // should always succeed as data comes in continously
//		if(bytes_read>VID_I2S_BLOCK_LEN_BYTES*2-100) { ESP_LOGI(TAG," bytes_read: %d of %d ", bytes_read,VID_I2S_BLOCK_LEN_BYTES*2);}
		data_w_count+=bytes_read/sizeof(uint32_t);
		sfzx_periodic_check();
	}
	d=rd_pt[data_w_pos++];
	if(!video_synced_state){ // todo only if no regular picture active
		sfzx_checksample(d);
	}
	return d;
}




// we normally do not have to wait longer than 20 lines as 290 are covered by screen display
static void vid_look_for_vsync(){
	uint32_t num_0_words=0;
	uint32_t num_0_lines=0;

	if(video_synced_cnt<10) video_synced_cnt++;
	//if(timeout_verbose<100) timeout_verbose++; // verbose!
	for(int i=0;i<usec_to_32bit_words(60*64);i++){
		if(vid_get_next_data()==0){
			num_0_words++;
		} else {
			num_0_words=0;
		}
		if(num_0_words>=usec_to_32bit_words(57)){
			num_0_lines++;
			num_0_words=0;
			if(num_0_lines>=2) return;
		}
	}
	video_synced_cnt=0;
	if(timeout_verbose>0) {ESP_LOGD(TAG," vid_look_for_vsync timeout ");timeout_verbose-=20;}
}


// vsync is about 400us, thus 6 lines
static void vid_look_for_screen(){
	uint32_t num_1_words=0;
	for(int i=0;i<usec_to_32bit_words(10*64);i++){
		if(vid_get_next_data()==0xffffffff){
			num_1_words++;
		} else {
			num_1_words=0;
		}
		if(num_1_words>=usec_to_32bit_words(51)-2){ // -1 (later-2) was for Zeditor where sync time was prabably nonstandard. However also ignore that data..
			// dummy read to avoid timeout in first vid_find_hsync Hsync later --- critical for early ULA timing without back porch
			vid_get_next_data();
			vid_get_next_data();	
			return; 
		}
	}
	video_synced_cnt=0;	
	if(timeout_verbose>0) {ESP_LOGI(TAG," vid_look_for_screen timeout ");timeout_verbose-=20;}
}

 static uint32_t line;

int __builtin_clz (unsigned int x);
int __builtin_ctz (unsigned int x); // trailing zeros

static void vid_find_hsync(uint32_t *line_acc_bits, uint32_t* line_bits_result, bool allow_resync ){
	uint32_t data,lastd=1;
	for(int i=0;i<usec_to_32bit_words(12)+0;i++){ 
		data=vid_get_next_data();
		if (data==0 ){ /* second condition should prevent from */
			// we could check for enough length here but that somehow caused artefacts, and also did not help much..  if(*line_acc_bits+20>=*line_bits_result){
			if(i==0 && !allow_resync) {
				video_synced_cnt=0; 
				if(timeout_verbose>0) {ESP_LOGI(TAG," vid_find_hsync start w 0 timeout %d ",line);timeout_verbose-=20;}
			}
			int trailing_zeros=__builtin_ctz(lastd);
			*line_bits_result=*line_acc_bits-trailing_zeros;
			*line_acc_bits=32+trailing_zeros; // also count the 0 word we akready have read
			return;
		}
		*line_acc_bits+=32;
		lastd=data;
	}
	/* timeout    TODO: Make first line more flexible in regards of this timeout, as different video modes might have different first lines */
	*line_acc_bits=0;
	video_synced_cnt=0;	
	if(timeout_verbose>0) {ESP_LOGI(TAG," vid_find_hsync timeout. ");timeout_verbose-=20;}
}


static inline void vid_ignore_line(uint32_t *line_acc_bits){
	bool have_1=false;
	for(int i=0;i<usec_to_32bit_words(60);i++){
		if (vid_get_next_data()) have_1=true;
		*line_acc_bits+=32;
	}
	if(!have_1) {
		video_synced_cnt=0; // cannot have have sync here 
		if(timeout_verbose>0) {ESP_LOGI(TAG," vid_ignore_line timeout ");timeout_verbose-=20;}
	}
}


static uint32_t get_pixel_adjust_nv()
{
    esp_err_t err;
    uint32_t val=162;  /* default goes here, 162 for ZX, 125 for NU */
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u32(my_handle, "VID_PIXEL_PHASE", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
    nvs_close(my_handle);
    return val;
}


static uint32_t get_vert_line_adjust_nv()
{
    esp_err_t err;
    uint32_t val=32;  /* default goes here, 31 for NU, 32 for original */
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u32(my_handle, "VID_VERT_LIN", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
    nvs_close(my_handle);
    return val;
}



static uint32_t get_clocks_per_line_nv()
{
    esp_err_t err;
    uint32_t val=414;  /* default goes here, 414 for ZX, 416 for ACE */
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u32(my_handle, "VID_CLK_P_LIN", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
    nvs_close(my_handle);
    return val;
}


static void set_clocks_per_line_nv(uint32_t newval)
{
    nvs_handle my_handle;
    if(get_clocks_per_line_nv() != newval) {
        ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
        ESP_ERROR_CHECK( nvs_set_u32(my_handle, "VID_CLK_P_LIN", newval ) );
        ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
        nvs_close(my_handle);
    }
}


static void set_pixel_adjust_nv(uint32_t newval)
{
    nvs_handle my_handle;
    if(get_pixel_adjust_nv() != newval) {
        ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
        ESP_ERROR_CHECK( nvs_set_u32(my_handle, "VID_PIXEL_PHASE", newval ) );
        ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
        nvs_close(my_handle);
    }
}


static void set_vert_line_adjust_nv(uint32_t newval)
{
    nvs_handle my_handle;
    if(get_vert_line_adjust_nv() != newval) {
        ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
        ESP_ERROR_CHECK( nvs_set_u32(my_handle, "VID_VERT_LIN", newval ) );
        ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
        nvs_close(my_handle);
    }
}


static uint32_t pixel_calibration_active=0;
static uint8_t pixel_calibration_frames=0;
static uint32_t pixel_adjust=12; // will be set from nv-mem anyway		
static uint32_t vline_adjust=32; // will be set from nv-mem anyway

static uint32_t pixels_per_vline=414; // is 414 for ZX, 416 for Jupiter



static uint32_t cal_pix_adj=0;		
static bool cal_vert_adj_pending=false;		

#define CAL_FILT_HSIZ 3 // halfsize of the filter, so 3 would mean 2*N+1 = 7 values averaged
static uint32_t cal_pix_bestadj_val=0;		/* for finding optimum value*/
static uint32_t cal_pix_bestadj_pos=0;		

static uint32_t cal_pix_filter[2*CAL_FILT_HSIZ+1];		/* smoothing filter */


static uint32_t cal_pix_adj_posbuf=0;

static uint32_t cal_score=0;


extern uint32_t vid_get_vline_offset()
{
	return STD_NUM_HDR_LINES; //
}


void vid_cal_pixel_start(){

	cal_pix_adj=0;
	pixel_calibration_active=200;
	pixel_calibration_frames=10;
	pixels_per_vline=414; 	/* this procedure is currently only started fro ZX, not ACE, so fall back */
	cal_pix_adj=pixel_calibration_active;
	cal_pix_bestadj_val=0;
	for(int i=0;i<2*CAL_FILT_HSIZ+1;i++) cal_pix_filter[i]=0;
	cal_score=0;
	cal_vert_adj_pending=true;
}

static void calpixel_framecheck(){
	uint32_t vline_test_adjust=26;
	if(pixel_calibration_active){
		if(cal_vert_adj_pending){
			if( --pixel_calibration_frames==0){
				for(vline_test_adjust=16;vline_test_adjust<32;vline_test_adjust++){
					/* last 4 lines are busy, check if we have the line above and below clean */
					if(vid_pixel_mem[vline_test_adjust*10 + 159*10 + 1 ]==0 && vid_pixel_mem[vline_test_adjust*10 + 192*10 + 1 ]==0){
						// vertical position match
						ESP_LOGI(TAG," vline_test_adjust match for %d (default 24) ", vline_test_adjust);			
						vline_adjust += vline_test_adjust-STD_NUM_HDR_LINES; // goal is to have 24 lines above and below
						set_vert_line_adjust_nv(vline_adjust); // depends on current setting
						break;
					}
				}
				cal_vert_adj_pending=false;
				pixel_calibration_frames=6;
			}
		}
		else if(--pixel_calibration_frames==0){
			//uint32_t score=lcd_get_retransmits_and_restart(true);
			uint32_t avg_score=cal_pix_filter[CAL_FILT_HSIZ]; /* double weight on the central one */
			for(int i=0; i<2*CAL_FILT_HSIZ+1; i++) avg_score+=cal_pix_filter[i];
			ESP_LOGI(TAG," Step %d: Score %d, avg %d", cal_pix_adj,cal_score,avg_score );			
			if(avg_score>cal_pix_bestadj_val){
				/* new optimum */
				cal_pix_bestadj_pos=cal_pix_adj+CAL_FILT_HSIZ; /* change calculation when changing stepsize from 1  */
				cal_pix_bestadj_val=avg_score;
			}
			/* feed averaging data */
			for(int i=0; i<2*CAL_FILT_HSIZ; i++) cal_pix_filter[i]=cal_pix_filter[i+1]; // shift through 
			cal_pix_filter[2*CAL_FILT_HSIZ]=cal_score;

			pixel_calibration_frames=6;
			pixel_calibration_active--;
			cal_pix_adj=pixel_calibration_active;
			cal_score=0;
		}
		else
		{
			/* checks */
			if(vid_pixel_mem[STD_NUM_HDR_LINES*10+ 21*80]==0) cal_score++;
			if(vid_pixel_mem[STD_NUM_HDR_LINES*10+ 21*80+1]==0xff00ff00) cal_score+=50;
			if(vid_pixel_mem[STD_NUM_HDR_LINES*10+ 21*80+10+1]==0xff00ff00) cal_score+=50;
			if(vid_pixel_mem[STD_NUM_HDR_LINES*10+ 21*80+20+1]==0xff00ff00) cal_score+=50;

			/* the test screen has a chequered pattern */
			for(uint32_t l=0; l<12; l+=1){
				for(uint32_t w=0; w<10; w++){
					uint32_t p,d;
					d=vid_pixel_mem[STD_NUM_HDR_LINES*10 +  23*80+10*l+w];
					if(w==0 || w==9)
						p=0;
					else
						p= ( (w^l) & 1 ) ? 0xaaaaaaaa : 0x55555555;
						//p= ( (w^l) & 1 ) ? 0x55555555 : 0xaaaaaaaa;
					if(p == d) cal_score++;
				}
			}

			if((cal_pix_adj==165||cal_pix_adj==129) && pixel_calibration_frames==2)
				ESP_LOGI(TAG," Sample: %08x %08x %08x", vid_pixel_mem[ (3+21)*80 ],vid_pixel_mem[(3+23)*80+1],vid_pixel_mem[(3+23)*80+8] );	
		}
		if(pixel_calibration_active<80){
			pixel_calibration_active=0;
			ESP_LOGI(TAG," Done: Optimum is at %d.", cal_pix_bestadj_pos);	
			set_pixel_adjust_nv(cal_pix_bestadj_pos);		
			set_clocks_per_line_nv(pixels_per_vline);
			pixel_adjust=cal_pix_bestadj_pos;
		}
	}
}
static uint32_t startpos=250;

void  calc_startpos_for_frame(uint32_t incv){
	// range 87-90, increment 1/64, 200 steps, 60ms per step =12 sec
	uint32_t adj=pixel_adjust;
	if(pixel_calibration_active){
		adj=cal_pix_adj;
	}
	startpos=87*incv+adj*incv/64;
}

static inline void vid_scan_line(uint32_t *line_acc_bits, uint32_t line,uint32_t incv, int show){
	uint32_t words_sampled=0;
	uint32_t bits_sampled=0;
	int bpos;
	//int showbitmatch=show;
	uint32_t rawdata=0;
	int pixmemoffset=10*line;

	// ignore backporch etc
	for(int i=0;i<usec_to_32bit_words(10);i++){ // 10 usec makes us see the last bits of sync sometimes
		rawdata=vid_get_next_data();// ignore except last
		*line_acc_bits+=32;
	}

	//startpos=89*incv+incv/2+incv/32;   // orig Zeddy  incv/2+incv/32; and incv/2+3*incv/64  best so far
	//startpos=88*incv-incv/4;   // NU 7 updates
	//startpos=88*incv-incv/4-incv/32;;   // NU 4-11 updates
	bpos = startpos-(*line_acc_bits<<16);
	if(show && line==90){
		ESP_LOGI(TAG," Line: %d, startgap_bits: %d", line,bpos>>16);
		show=0;
	}

	for (words_sampled=0;words_sampled<10;words_sampled++ )	
	{
		uint32_t outdata=0;
		if(video_synced_state){
			for(bits_sampled=0; bits_sampled<32; bits_sampled++)
			{
				uint32_t bmask;
				outdata <<= 1;
				while (bpos>0x200000){
					bpos-=0x200000;
					rawdata=vid_get_next_data();
					*line_acc_bits+=32;
				}
				bmask = (0x80000000 >> (bpos>>16)  );
				outdata |=  (rawdata & bmask) ? 0: 1;
				//if(showbitmatch&&outdata) {ESP_LOGI(TAG," Bit sample %08X %08X ", bmask,rawdata);showbitmatch=0;}
				bpos += incv;
			}
			vid_pixel_mem[ words_sampled+pixmemoffset ]=outdata;
		}else{
			outdata=vid_get_next_data();	// have no sync anyway, just do some animation quickly
			outdata^=vid_get_next_data();
			outdata^=vid_get_next_data();
				if (last_sync_timer==0)  vid_pixel_mem[ words_sampled+pixmemoffset ]= outdata;
				else if(line<12||line>228) vid_pixel_mem[ words_sampled+pixmemoffset ]= outdata;
				else if (words_sampled==0) vid_pixel_mem[ words_sampled+pixmemoffset ]= (outdata & 0xFFF00000)|0xC0000000;
				else if (words_sampled==9) vid_pixel_mem[ words_sampled+pixmemoffset ]= (outdata & 0x00000FFF)|0x00000003;
		}
		//if(show && outdata){
		//		ESP_LOGI(TAG," Line: %d, word %d, content: %08X ", line,words_sampled,outdata);
		//		show=0;
		//}		
	}

/*
	for(int i=0;i<usec_to_32bit_words(49)-1;i++){ // 320 Pixel is 49us, some safety overhead...
		uint32_t d=vid_get_next_data();
		if(d!=0xffffffff){
			if(show){
				ESP_LOGI(TAG," Line: %d, word %d, bpos: %d, content: %08X ", line,i,*line_acc_bits,d);
				show=0;
			}
		}
		*line_acc_bits+=32;
	}
	*/
}

static uint16_t pending_user_adj=0;

static void vid_in_task(void*arg)
{
	bool last_video_synced_state=false;
	bool had_video_synced_state=false; /* todo bring this functionality to external module using vid_scan_startline/endline */
	uint32_t frame_count=0;
	uint32_t line_bits_inc=0x00031900; // rough default for 20MHz vs 6.5 Mhz
	uint32_t noisepattern=0x55555555; 
	pixel_adjust=get_pixel_adjust_nv();
	vline_adjust=get_vert_line_adjust_nv();
	pixels_per_vline=get_clocks_per_line_nv();
    ESP_LOGI(TAG,"vid_in_task START  (pixadj %d, vlineadj %d)",pixel_adjust,vline_adjust);
    while(true){
		uint32_t line_acc_bits=0;
		uint32_t line_bits_result=0;
		uint32_t line_bits_acc=0;
		uint32_t lbcount=0;
		vid_look_for_vsync();
		// find start of frame
		vid_look_for_screen();
		vid_find_hsync(&line_acc_bits,&line_bits_result, true); // line_bits_result needs to be low/zero here
		line_bits_result=0;
		for( line=1; line<280; line++){
			if(!had_video_synced_state){
				if( line>=vline_adjust+vid_scan_startline && line<=vid_scan_endline+vline_adjust){
					// pattern
					uint32_t woffset=(line-vline_adjust)*10;
					for(int w=0;w<10;w++){
						vid_pixel_mem[ w+woffset ]=noisepattern;
						noisepattern=noisepattern*7; // pseudo random
						noisepattern^=noisepattern>>8; // pseudo random
					}
				}
				vid_ignore_line(&line_acc_bits);
			} else if( line>=vline_adjust+vid_scan_startline && line<=vid_scan_endline+vline_adjust){
				vid_scan_line(&line_acc_bits, line-vline_adjust,line_bits_inc, frame_count%500==50?0:0 );
			} else {
				vid_ignore_line(&line_acc_bits);
			}
			video_synced_state= (video_synced_cnt>=2);
			vid_find_hsync(&line_acc_bits,&line_bits_result, line<56 /* was 4 but got trouble with DrBeeps 1k games (there 56) */ || line>240  ); /* in FAST mode, we may have a strange resync at line 247*/
			if(lbcount<20 && line>4 && line_bits_result>usec_to_samples(62) && line_bits_result<usec_to_samples(66)){
				line_bits_acc+=line_bits_result;
				lbcount++;
				if(lbcount==20){
					line_bits_inc = (line_bits_acc<<16)/(20*pixels_per_vline);	/* one zxline is 207 cpu cycles=414 pixel, 10 times avg */
					calc_startpos_for_frame(line_bits_inc); 
				}
			}

			if(frame_count%50000==100){
				if((line==10||line==200)){
					ESP_LOGI(TAG,"Note: Frames: %d, line: %d, linlength: %d, inc %X ", frame_count,line,line_bits_result, line_bits_inc);
					if(frame_count<1500 && line==200){
						char *buffer=malloc(2048);
						vTaskGetRunTimeStats( buffer);
						ESP_LOGI(TAG,"\n%s",buffer);
						free(buffer);
					}

				}
			}
		}
		calpixel_framecheck();
		if(video_synced_state!=last_video_synced_state){
			last_video_synced_state=video_synced_state;
			if(video_synced_state) had_video_synced_state=true;
			sfzx_report_video_signal_status(video_synced_state);
			timeout_verbose=60;
		}
		frame_count++;
		if(video_synced_state){
			last_sync_timer=500;
		}
		else if(last_sync_timer) last_sync_timer--;
		user_knob_periodic_check();
		if(pending_user_adj){
			if(pending_user_adj-- ==1){
				// store current settings
				set_pixel_adjust_nv(cal_pix_bestadj_pos);		
				set_clocks_per_line_nv(pixels_per_vline);
			}
		}
	}
}


void vid_user_scr_adj_event()
{
	if(pending_user_adj==0){
		pixels_per_vline=416;	/* manual alignment nomally not one for ZX, so switch to Jupiter ACE */
		pixel_adjust+=14; 		/* Heinz reported that he needed 3 manual steps of 7 for his ACE, so +14 here to make is roughly be the first step */
	}
	pixel_adjust+=7;
	if(pixel_adjust > 220){
		pixel_adjust-= 141; // cycle through adjust range 
		pixels_per_vline = pixels_per_vline==416 ? 414:416;	// toggle zx/ace for every second loop
	}
	pending_user_adj=500; /* after adjustment plus 10 sec inactivity, write results to nv */
}


/**
 * @brief I2S ADC/DAC mode init.
 */
void vid_init()
{
	int i2s_num = VID_I2S_NUM;
	i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate =  VID_I2S_SAMPLE_RATE,
        .bits_per_sample = VID_I2S_SAMPLE_BITS,
	    .communication_format =  I2S_COMM_FORMAT_I2S_MSB,
	    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
	    .intr_alloc_flags = 0,
	    .dma_buf_count = 12, // 4
	    .dma_buf_len = VID_I2S_BLOCK_LEN_SAMPLES,
	    .use_apll = 0,//1, True cause problems at high rates (?)
	};
	 //install and start i2s driver
    //xQueueCreate(5, sizeof(i2s_event_t));
	ESP_ERROR_CHECK( i2s_driver_install(i2s_num, &i2s_config, 5, NULL/*&event_queue*/ ));
	//init DOUT pad
    static const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_PIN_NO_CHANGE,     //I2S_PIN_NO_CHANGE, 18
        .ws_io_num = I2S_PIN_NO_CHANGE,
        .data_out_num =  I2S_PIN_NO_CHANGE , // 22
        .data_in_num = VID_SIGNALIN_PIN      //I2S_PIN_NO_CHANGE
    };
    i2s_set_pin(i2s_num, &pin_config);
    
    i2s_read_buff=(uint8_t*) calloc(VID_I2S_BLOCK_LEN_BYTES*2, sizeof(uint8_t));
    if(!i2s_read_buff) printf("calloc of %d failed\n",VID_I2S_BLOCK_LEN_BYTES);
    
	// PWM out on PIN 12 

    ledc_timer_config_t ledc_timer = {
		.duty_resolution = 10,
        //.duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
        .freq_hz = 50000,                      // frequency of PWM signal
        .speed_mode = LEDC_LOW_SPEED_MODE,           // timer mode
        .timer_num = LEDC_TIMER_1,            // timer index
        .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
    };

	ledc_timer_config(&ledc_timer);

    // Set LED Controller with previously prepared configuration
    ledc_channel_config(&ledc_channel);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 750);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

    xTaskCreate(vid_in_task, "vid_in_task", 1024 * 3, NULL, 19, NULL);


	TimerHandle_t pwm_timer;
	pwm_timer=xTimerCreate("VidLvlPWM",100 / portTICK_RATE_MS,pdTRUE, ( void * ) 0,pwm_timer_callb);
	xTimerStart( pwm_timer, 100 );

	// feedbak pin to 
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = 1ULL<<VID_PWMLEVEL_PIN | 1ULL<<VID_SIGNALIN_PIN;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

	for(int i=0; i<sizeof(vid_pixel_mem)/sizeof(uint32_t);i++){
		vid_pixel_mem[i]= ((i/10)&1) ? 0x55555555 : 0xaaaaaaaa;
	}
}
