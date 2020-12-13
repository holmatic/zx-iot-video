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
#include "signal_from_zx.h"


static const char* TAG = "i2svid";


//i2s number
#define VID_I2S_NUM           (0)//1
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
	    .dma_buf_count = 8, // 4
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

    xTaskCreate(vid_in_task, "vid_in_task", 1024 * 3, NULL, 9, NULL);


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


}



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


uint32_t vid_pixel_mem[10*240];




static uint32_t vid_get_next_data()
{
	uint32_t *rd_pt= (uint32_t* ) i2s_read_buff;
	uint32_t d;
	static uint32_t data_w_pos=0;
	static uint32_t data_w_count=0;

	while(data_w_pos>=data_w_count){
		size_t bytes_read=0;
		data_w_pos=data_w_count=0;
		//i2s_read(i2s_port_t i2s_num, void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait);
		ESP_ERROR_CHECK(i2s_read(VID_I2S_NUM, (void*) i2s_read_buff, VID_I2S_BLOCK_LEN_BYTES, &bytes_read, 3000 / portTICK_RATE_MS)); // should always succeed as data comes in continously
//		if(bytes_read>VID_I2S_BLOCK_LEN_BYTES*2-100) { ESP_LOGI(TAG," bytes_read: %d of %d ", bytes_read,VID_I2S_BLOCK_LEN_BYTES*2);}
		data_w_count+=bytes_read/sizeof(uint32_t);
		sfzx_periodic_check();
	}
	d=rd_pt[data_w_pos++];
	if(1){ // todo only if no regular picture active
		sfzx_checksample(d);
	}
	return d;
}

static uint8_t video_synced_cnt=0;
static bool video_synced_state=false;

static int timeout_verbose=10;





// we normally do not have to wait longer than 20 lines as 290 are covered by screen display
static void vid_look_for_vsync(){
	uint32_t num_0_words=0;
	uint32_t num_0_lines=0;

	if(video_synced_cnt<10) video_synced_cnt++;
	//if(timeout_verbose<100) timeout_verbose++;
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
	if(timeout_verbose>0) {ESP_LOGI(TAG," vid_look_for_vsync timeout ");timeout_verbose-=20;}
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
		if(num_1_words>=usec_to_32bit_words(51)-1) return; // -1 was for Zeditor where sync time was prabably nonstandard..
	}
	video_synced_cnt=0;	
	if(timeout_verbose>0) {ESP_LOGI(TAG," vid_look_for_screen timeout ");timeout_verbose-=20;}
}

 static uint32_t line;

int __builtin_clz (unsigned int x);
int __builtin_ctz (unsigned int x); // trailing zeros

static void vid_find_hsync(uint32_t *line_acc_bits, uint32_t* line_bits_result, bool allow_resync ){
	uint32_t data,lastd=1;
	for(int i=0;i<usec_to_32bit_words(12);i++){
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
	/* timeout */
	*line_acc_bits=0;
	video_synced_cnt=0;	
	if(timeout_verbose>0) {ESP_LOGI(TAG," vid_find_hsync timeout ");timeout_verbose-=20;}
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
    uint32_t val=162;  /* default goes here */
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


static uint32_t pixel_calibration_active=0;
static uint8_t pixel_calibration_frames=0;
static uint32_t pixel_adjust=12;		
static uint32_t cal_pix_adj=0;		

static uint32_t cal_pix_bestadj_val=0;		/* for finding optimum value*/
static uint32_t cal_pix_bestadj_pos=0;		
static uint32_t cal_pix_adj_buf1=0;		/* for smoothing */	
static uint32_t cal_pix_adj_buf2=0;		
static uint32_t cal_pix_adj_posbuf=0;

static uint32_t cal_score=0;

void vid_cal_pixel_start(){

	cal_pix_adj=0;
	pixel_calibration_active=200;
	pixel_calibration_frames=6;
	cal_pix_adj=pixel_calibration_active;
	cal_pix_bestadj_val=cal_pix_adj_buf1=cal_pix_adj_buf2=0;
	cal_score=0;
	//lcd_get_retransmits_and_restart(false);
}

static void calpixel_framecheck(){
	if(pixel_calibration_active){
		if(--pixel_calibration_frames==0){
			//uint32_t score=lcd_get_retransmits_and_restart(true);
			uint32_t avg_score=cal_score+cal_pix_adj_buf1+cal_pix_adj_buf1+cal_pix_adj_buf2;
			ESP_LOGI(TAG," Step %d: Score %d, avg %d", cal_pix_adj,cal_score,avg_score );			
			if(avg_score>cal_pix_bestadj_val){
				/* new optimum */
				cal_pix_bestadj_pos=cal_pix_adj_posbuf;
				cal_pix_bestadj_val=avg_score;
			}
			/* feed averaging data */
			cal_pix_adj_posbuf=cal_pix_adj;
			cal_pix_adj_buf2=cal_pix_adj_buf1;
			cal_pix_adj_buf1=cal_score;

			pixel_calibration_frames=6;
			pixel_calibration_active--;
			cal_pix_adj=pixel_calibration_active;
			cal_score=0;
		}
		else
		{
			/* checks */
			if(vid_pixel_mem[(3+21)*80]==0) cal_score++;
			if(vid_pixel_mem[(3+21)*80+1]==0xff00ff00) cal_score++;
			if(vid_pixel_mem[(3+21)*80+10+1]==0xff00ff00) cal_score++;
			if(vid_pixel_mem[(3+21)*80+20+1]==0xff00ff00) cal_score++;

			/* the test screen has a chequered pattern */
			for(uint32_t l=0; l<12; l+=1){
				for(uint32_t w=0; w<10; w++){
					uint32_t p,d;
					d=vid_pixel_mem[(3+23)*80+10*l+w];
					if(w==0 || w==9)
						p=0;
					else
						p= ( (w^l) & 1 ) ? 0xaaaaaaaa : 0x55555555;
					if(p == d) cal_score++;
				}
			}

			/*
			if(vid_pixel_mem[(3+23)*80+ 1]==0xaaaaaaaa) cal_score++;
			if(vid_pixel_mem[(3+23)*80+11]==0x55555555) cal_score++;
			if(vid_pixel_mem[(3+23)*80+21]==0xaaaaaaaa) cal_score++;
			if(vid_pixel_mem[(3+23)*80+31]==0x55555555) cal_score++;
			if(vid_pixel_mem[(3+23)*80+41]==0xaaaaaaaa) cal_score++;
			if(vid_pixel_mem[(3+23)*80+51]==0x55555555) cal_score++;
			if(vid_pixel_mem[(3+23)*80+ 8]==0x55555555) cal_score++;
			if(vid_pixel_mem[(3+23)*80+18]==0xaaaaaaaa) cal_score++;
			if(vid_pixel_mem[(3+23)*80+28]==0x55555555) cal_score++;
			if(vid_pixel_mem[(3+23)*80+38]==0xaaaaaaaa) cal_score++;
			if(vid_pixel_mem[(3+23)*80+48]==0x55555555) cal_score++;
			if(vid_pixel_mem[(3+23)*80+58]==0xaaaaaaaa) cal_score++;

			if(vid_pixel_mem[(3+23)*80+10]==0) cal_score++;
			if(vid_pixel_mem[(3+23)*80+20]==0) cal_score++;
			if(vid_pixel_mem[(3+23)*80+ 9]==0) cal_score++;
			if(vid_pixel_mem[(3+23)*80+19]==0) cal_score++;
			*/

			if((cal_pix_adj==165||cal_pix_adj==129) && pixel_calibration_frames==2)
				ESP_LOGI(TAG," Sample: %08x %08x %08x", vid_pixel_mem[ (3+21)*80 ],vid_pixel_mem[(3+23)*80+1],vid_pixel_mem[(3+23)*80+8] );	
		}
		if(pixel_calibration_active==0){
			ESP_LOGI(TAG," Done: Optimum is at %d.", cal_pix_bestadj_pos);	
			set_pixel_adjust_nv(cal_pix_bestadj_pos);		
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
		if(video_synced_state)
			vid_pixel_mem[ words_sampled+pixmemoffset ]=outdata;
		else{
			if(line<12||line>228) vid_pixel_mem[ words_sampled+pixmemoffset ]= outdata;
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



static void vid_in_task(void*arg)
{

	bool last_video_synced_state=false;
	uint32_t frame_count=0;
	uint32_t line_bits_inc=0x00031900; // rough default for 20MHz vs 6.5 Mhz
	pixel_adjust=get_pixel_adjust_nv();
    ESP_LOGI(TAG,"vid_in_task START \n");
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
			if(line>=30 && line<240+30){
				vid_scan_line(&line_acc_bits, line-30,line_bits_inc, frame_count%500==50?0:0 );
			} else {
				vid_ignore_line(&line_acc_bits);
			}
			video_synced_state= (video_synced_cnt>=2);
			vid_find_hsync(&line_acc_bits,&line_bits_result, line<4 || line>240  ); /* in FAST mode, we may have a strange resync at line 247*/
			if(lbcount<20 && line>4 && line_bits_result>usec_to_samples(62) && line_bits_result<usec_to_samples(66)){
				line_bits_acc+=line_bits_result;
				lbcount++;
				if(lbcount==20){
					line_bits_inc = (line_bits_acc<<16)/(20*414);	/* one zxline is 207 cpu cycles=414 pixel, 10 times avg */
					calc_startpos_for_frame(line_bits_inc); 
				}
			}

			if(frame_count%5000==100){
				if((line==10||line==200)){
					ESP_LOGI(TAG," Frames: %d, line: %d, linlength: %d, inc %X ", frame_count,line,line_bits_result, line_bits_inc);
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
			sfzx_report_video_signal_status(video_synced_state);
			timeout_verbose=60;
		}
		frame_count++;
	}

}


