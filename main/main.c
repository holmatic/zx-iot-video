/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

#include "zx_server.h"
#include "signal_from_zx.h"
#include "wifi_sta.h"
#include "iis_videosig.h"
#include "video_attr.h"
#include "user_knob.h"
#include "led_matrix.h"
#include "lcd_display.h"
#include "vga_display.h"
#include "tape_signal.h"

/* This example demonstrates how to create file server
 * using esp_http_server. This file has only startup code.
 * Look in file_server.c for the implementation */

/* The example uses simple WiFi configuration that you can set via
 * 'make menuconfig'.
 * If you'd rather not, just change the below entries to strings
 * with the config you want -
 * ie. #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

static const char *TAG="zxiotmain";




/* Function to initialize SPIFFS */
static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,   // This decides the maximum number of files that can be created on the storage
      .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

/* non volatile storage */
void nvs_sys_init(){
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle my_handle;
    err = nvs_open("zxstorage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("Done\n");
        nvs_close(my_handle);
    }
}


/* currently support 2 blink LED, if more need to be supporred we could either make it mask or configurable */
#define PIN_NUM_BLINK_LED   2 // 2 for JOY-IT, 21 for TTGO  GPIO_NUM_MAX
#define PIN_NUM_2ND_BLINK_LED GPIO_NUM_MAX // 2 for JOY-IT, 21 for TTGO

#define BLINK_LED_ON 1
#define BLINK_LED_OFF 0

/* Blink LED, TODO pin number is configurable in NVS, default 2
*
*   Show
*       Light up during init
*       Flash slowly during slow mode (2.5 sec 1-2pulses dep on WIFI)
*       Flash fast during load/save 
*       Flash every 5 sec when not connected (no changes in input, 2 pulses when wifi)
*  
*/

static void bled_ini_single(uint8_t num)
{
    if(num<GPIO_NUM_MAX){
        gpio_pad_select_gpio(num);
	    gpio_set_direction(num, GPIO_MODE_OUTPUT);
	    gpio_set_level(num, BLINK_LED_ON);
    }
}


static void bled_init()
{
    if(PIN_NUM_BLINK_LED<GPIO_NUM_MAX) bled_ini_single(PIN_NUM_BLINK_LED);
    if(PIN_NUM_2ND_BLINK_LED<GPIO_NUM_MAX) bled_ini_single(PIN_NUM_2ND_BLINK_LED);
}


#define BLED_CYCLE_MS 40

static void bled_timer_event( TimerHandle_t pxTimer )
{
    static uint16_t count=0; /* cycle count */
    bool led_on=false;
    bool slowmode_detected=(zxsrv_get_zx_status()==ZXSG_SLOWM_50HZ ||  zxsrv_get_zx_status()==ZXSG_SLOWM_60HZ  ); 
    ++count;
    if (taps_is_tx_active()){
        led_on = ((count&3)==1);
    } else if (zxsrv_get_zx_status()==ZXSG_FILE_DATA){
        led_on = ((count&3)>1);
    } else {
        if (count==1) led_on = true; 
        if (count==6) led_on = slowmode_detected; 
        if (count==10) led_on = wifi_sta_is_connected(); 
        if (count>2500/BLED_CYCLE_MS && slowmode_detected) count=0; 
        if (count>5000/BLED_CYCLE_MS) count=0; 
    }
    if (count==1) led_on = true; 
    if (count>5000/BLED_CYCLE_MS) count=0; 

	if(PIN_NUM_BLINK_LED<GPIO_NUM_MAX)      gpio_set_level(PIN_NUM_BLINK_LED,     led_on ? BLINK_LED_ON : BLINK_LED_OFF);
	if(PIN_NUM_2ND_BLINK_LED<GPIO_NUM_MAX)  gpio_set_level(PIN_NUM_2ND_BLINK_LED, led_on ? BLINK_LED_ON : BLINK_LED_OFF);
 }
 

static void bled_ini_done()
{
    xTimerHandle t;
    t=xTimerCreate( "LED_Flasher",( BLED_CYCLE_MS / portTICK_PERIOD_MS), pdTRUE,0,bled_timer_event);
    xTimerStart(t,0);
}


/* Declare the function which starts the file server.
 * Implementation of this function is to be found in
 * file_server.c */
esp_err_t start_file_server(const char *base_path);


void app_main()
{
    //QueueHandle_t msgqueue=NULL;
    //msgqueue=xQueueCreate(10,sizeof(sfzx_evt_type_t));


    ESP_LOGI(TAG, "Starting XAM host app ...");


    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);



    nvs_sys_init();
    bled_init();
    zxsrv_init();
    taps_init(NULL);
    sfzx_init();
    user_knob_init();
    vid_init();
    lcd_disp_init();

    if(0) ledmx_init(); /* support for 64x64 low-res-graphics LED panel display, highly experimental and non-optimized  */
    video_attr_init();
    if(1) vga_disp_init();

	wifi_sta_init(); /* needs nvs_sys_init */

    /* Initialize file storage */
    ESP_ERROR_CHECK(init_spiffs());

    /* Start the file server */
    ESP_ERROR_CHECK(start_file_server("/spiffs"));

    bled_ini_done();

}
