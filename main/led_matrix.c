#include <stdio.h>
#include <string.h>
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/adc.h"

#include "driver/ledc.h"

#include "esp_adc_cal.h"
#include "zx_server.h"
#include "signal_from_zx.h"



static const char* TAG = "ledmx";

#define LEDMX_DATA_R1       (4)
#define LEDMX_DATA_G1       (2)
#define LEDMX_DATA_B1       (5)
#define LEDMX_DATA_R2       (15)
#define LEDMX_DATA_G2       (18)
#define LEDMX_DATA_B2       (19)

#define LEDMX_ADDR_A       (27)
#define LEDMX_ADDR_B       (32)
#define LEDMX_ADDR_C       (33)
#define LEDMX_ADDR_D       (34)
#define LEDMX_ADDR_E       (35)

#define LEDMX_PWM_NOE_PIN       (21)
#define LEDMX_CLK_PIN           (23)
#define LEDMX_LATCH_ENA         (26)

static void ledmx_task(void*arg);
 
static 	ledc_channel_config_t ledc_channel = {
		.channel    = LEDC_CHANNEL_1,
		.duty       = 0,
		.gpio_num   = LEDMX_PWM_NOE_PIN,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.hpoint     = 0,
		.timer_sel  = LEDC_TIMER_1          
	};


static uint8_t display[4096];


/**
 * @brief Init interface to LED matrix 
 */
void ledmx_init()
{

	// PWM out on PIN 21 
	// The iis_video uses the dsame timer with LEDC_CHANNEL_0 !
    ledc_timer_config_t ledc_timer = {
        //.duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
        .freq_hz = 50000,                      // frequency of PWM signal
        .speed_mode = LEDC_LOW_SPEED_MODE,           // timer mode
		.duty_resolution = 10,
        .timer_num = LEDC_TIMER_1,            // timer index
        .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
    };

	ledc_timer_config(&ledc_timer);



    // Set LED Controller with previously prepared configuration
    ledc_channel_config(&ledc_channel);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 250);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);


	// feedback pin to 
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask =
	 	  1ULL<<LEDMX_DATA_R1
		| 1ULL<<LEDMX_DATA_G1
		| 1ULL<<LEDMX_DATA_B1
		| 1ULL<<LEDMX_DATA_R2
		| 1ULL<<LEDMX_DATA_G2
		| 1ULL<<LEDMX_DATA_B2
		| 1ULL<<LEDMX_ADDR_A
		| 1ULL<<LEDMX_ADDR_B
		| 1ULL<<LEDMX_ADDR_C
	//	| 1ULL<<LEDMX_ADDR_D
	//	| 1ULL<<LEDMX_ADDR_E
		| 1ULL<<LEDMX_CLK_PIN
		| 1ULL<<LEDMX_LATCH_ENA
	    ;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

	for(int i=0; i<4096; i++){
		display[i]=0;
	}

	display[ 99]=2;
	display[100]=2;
	display[101]=1;
	display[102]=4;
	display[103]=7;

    xTaskCreate(ledmx_task, "mxled_task", 1024 * 3, NULL, 8, NULL);

}


static void show_one_line(uint8_t line_nr)
{
	uint8_t d1,d2;

	for(int i=0; i<64; i++){
		d1=display[line_nr*64 + i];
		d2=display[(line_nr+32)*64 + i];
		gpio_set_level(LEDMX_CLK_PIN, 0);
		gpio_set_level(LEDMX_DATA_R1, (d1&4) ? 0:1 );
		gpio_set_level(LEDMX_DATA_G1, (d1&2) ? 0:1 );
		gpio_set_level(LEDMX_DATA_B1, (d1&1) ? 0:1 );
		gpio_set_level(LEDMX_DATA_R2, (d2&4) ? 0:1 );
		gpio_set_level(LEDMX_DATA_G2, (d2&2) ? 0:1 );
		gpio_set_level(LEDMX_DATA_B2, (d2&1) ? 0:1 );
		gpio_set_level(LEDMX_CLK_PIN, 1);
		gpio_set_level(LEDMX_CLK_PIN, 1);
	}
	gpio_set_level(LEDMX_DATA_R2, (d2&4) ? 0:1 );
	gpio_set_level(LEDMX_LATCH_ENA, 0);
	gpio_set_level(LEDMX_LATCH_ENA, 0);
	gpio_set_level(LEDMX_LATCH_ENA, 1);
	gpio_set_level(LEDMX_ADDR_A, (line_nr&1) ? 1:0 );
	gpio_set_level(LEDMX_ADDR_B, (line_nr&2) ? 1:0 );
	gpio_set_level(LEDMX_ADDR_C, (line_nr&4) ? 1:0 );

	gpio_set_level(LEDMX_DATA_R1, 0);
	gpio_set_level(LEDMX_DATA_G1, 0 );
	gpio_set_level(LEDMX_DATA_B1, 0 );
	gpio_set_level(LEDMX_DATA_R2, 0 );
	gpio_set_level(LEDMX_DATA_G2, 0 );
	gpio_set_level(LEDMX_DATA_B2, 0 );

}


static void ledmx_task(void*arg)
{
    ESP_LOGI(TAG,"vid_in_task START \n");
	//vTaskDelay(100 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	//i2s_adc_enable(SFZX_I2S_NUM);
	uint8_t line_nr=0;
    while(true){
	   	//ESP_LOGI(TAG,"ledmx_task line \n");
		vTaskDelay(1);//0 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
		show_one_line(line_nr);
		++line_nr;
		if(line_nr>=64) line_nr=0;
    }
}
