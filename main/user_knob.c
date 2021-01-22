
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_spi_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "video_attr.h"
#include "iis_videosig.h"

#include "user_knob.h"

static const char *TAG="user_knob";


#define VID_PUSHBUTTON_PIN          (23) // 14 make this 32 as input ?

static uint8_t pushbutton_holdoff=10;	

static uint32_t pushbutton_hold_cnt=0;	

static char mode='c';   // colour, adjust
static uint32_t mode_expire_cnt=0;	

static int last_level=0;   // colour, adjust

#define LONG_KEYPRESS_TICKS 80
#define REPEAT_TICKS 30
#define DEBOUNCE_TICKS 10
#define EXPIRE_TICKS 250

static const char* colour_lookup="WWGGYYBBFF";
static uint8_t colour_tog_ix=0;  

// call periodically from suitable thread
void user_knob_periodic_check(){
  if(pushbutton_holdoff)
    pushbutton_holdoff--;
  else{
    int level=gpio_get_level(VID_PUSHBUTTON_PIN);

    if(0==level){	// key is pressed!
      pushbutton_hold_cnt++;
      if(pushbutton_hold_cnt>LONG_KEYPRESS_TICKS) { // long press/hold
        mode='a';
        mode_expire_cnt=EXPIRE_TICKS;
        if ( (pushbutton_hold_cnt-LONG_KEYPRESS_TICKS) % REPEAT_TICKS  == 1 ) vid_user_scr_adj_event();
      }
    } else {    // key not pressed/released
      if(pushbutton_hold_cnt){
        // just released
        ESP_LOGI(TAG," Jupiter Pushbutton! ");
        if(pushbutton_hold_cnt<30){
          // shot keypress
          if(mode=='a'){
            vid_user_scr_adj_event();
          } else if (mode=='c'){
            // colour toggle    
            vidattr_set_c_mode(colour_lookup[colour_tog_ix]); 
            vidattr_set_inv_mode(colour_tog_ix&1);
            colour_tog_ix++;
            if(!colour_lookup[colour_tog_ix]) colour_tog_ix=0;
          }
        }
        pushbutton_hold_cnt=0;
      }
    }
    if(mode!='c' && mode_expire_cnt) { if(--mode_expire_cnt==0) mode='c'; }
    if(level!=last_level){
        last_level=level;
        pushbutton_holdoff=DEBOUNCE_TICKS; // next step in half a second...
    }
  }
}


// call once at startup
void user_knob_init(){
   gpio_config_t io_conf;
	// extra pushbutton, initially for for Juptiter ACE use where ew do not have interactive menus ...
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = 1ULL<<VID_PUSHBUTTON_PIN;
    //disable pull-down mode
    io_conf.pull_down_en = false;
    io_conf.pull_up_en = true;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    ets_delay_us(10); // time for pullup to pull up before first read
    last_level=gpio_get_level(VID_PUSHBUTTON_PIN);

}


