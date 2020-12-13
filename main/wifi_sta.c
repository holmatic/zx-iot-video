
#include <sys/param.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "wifi_sta.h"


#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

static const char *TAG="wifi_sta";

//#define WIFI_LENGTH_SSID 32
//#define WIFI_LENGTH_PASS 64

static int wifi_connect_retry=0;
static bool wifi_is_connected=false;


static char wifi_stat_msg[33];
static char wifi_macaddr_msg[24];


// return true if successfully connected and IP provided
bool wifi_sta_is_connected()
{
    return wifi_is_connected;
}


const char* wifi_get_status_msg()
{
    return wifi_stat_msg;
}

const char* wifi_get_MAC_addr()
{
    uint8_t mac[6];
    //esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    sprintf(wifi_macaddr_msg,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return wifi_macaddr_msg;
}


/* Wi-Fi event handler */
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        ESP_ERROR_CHECK(esp_wifi_connect());
        wifi_connect_retry=3;
        wifi_is_connected=false;
        sprintf(wifi_stat_msg,"[ WIFI ] INIT...");        
        break;
    case SYSTEM_EVENT_STA_STOP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_STOP");
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "SYSTEM_EVENT_SCAN_DONE");
        esp_wifi_set_ps(WIFI_PS_NONE);
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_CONNECTED");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        sprintf(wifi_stat_msg,"[WIFI] HTTP://%s",ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        //sprintf(wifi_stat_msg,"WIFI  :-)");        
        wifi_connect_retry=3;
        wifi_is_connected=true;        
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        wifi_is_connected=false;
        if (wifi_connect_retry>0){      // we cannot scan for networks if we always immediately retry. Timeout for now, maybe later on retry after break..
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
            wifi_connect_retry--;
        }
        sprintf(wifi_stat_msg,"[ WIFI ]  -> [W] TO CONFIG ");        
        break;
    default:
        break;
    }
    return ESP_OK;
}


/* get wifi data from nve ans start */
static esp_err_t wifi_sta_config_from_nvs()
{
    esp_err_t err;
    size_t l;
    nvs_handle my_handle;
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "S1",
            .password = "P1",
        },
    };
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    err = nvs_open("zxstorage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        // Read
        printf("Reading wifi namefrom NVS ... ");
        l=sizeof(wifi_config.sta.ssid);
        err = nvs_get_str(my_handle, "WIFI_n", (char *)wifi_config.sta.ssid, &l);
        switch (err) {
            case ESP_OK:
                printf("Done\n");
                printf("Name = %s\n", (char *)wifi_config.sta.ssid);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value is not initialized yet!\n");
                err = nvs_set_str(my_handle, "WIFI_n", EXAMPLE_WIFI_SSID);
                printf((err != ESP_OK) ? "Set n Failed!\n" : "Done\n");
                err = nvs_set_str(my_handle, "WIFI_p", EXAMPLE_WIFI_PASS);
                printf((err != ESP_OK) ? "Set p Failed!\n" : "Done\n");
                printf("Committing updates in NVS ... ");
                err = nvs_commit(my_handle);
                printf((err != ESP_OK) ? "C Failed!\n" : "Done\n");
                err = nvs_get_str(my_handle, "WIFI_n", (char *)wifi_config.sta.ssid, &l);
                printf((err != ESP_OK) ? "RN Failed!\n" : "Done\n");
                break;
            default :
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
        l=sizeof(wifi_config.sta.password);
        err = nvs_get_str(my_handle, "WIFI_p", (char *)wifi_config.sta.password, &l);
        printf((err != ESP_OK) ? "RP Failed!\n" : "Done\n");

        // Commit written value.
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        nvs_close(my_handle);
    }
    if (err==ESP_OK){
        /* got data from nvs now */
        ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    }
    return err;
}

// write new AP info to nvs, NULL pointer to omit, afterwards optionally (disconnect and) connect
void wifi_sta_reconfig(const char* ssid, const char* wf_pass, bool reconnect)
{
    nvs_handle my_handle;
    if(reconnect){
        wifi_connect_retry=0;
        if(1||wifi_is_connected){
            ESP_LOGI(TAG, "Send WiFi stop");
            ESP_ERROR_CHECK(esp_wifi_stop());
            esp_wifi_set_ps(WIFI_PS_NONE);
        }
    }
    wifi_is_connected=false;

    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    if(ssid) {
        ESP_ERROR_CHECK( nvs_set_str(my_handle, "WIFI_n", ssid) );
    }
    if(wf_pass) {
        ESP_ERROR_CHECK( nvs_set_str(my_handle, "WIFI_p", wf_pass) );
    }
    ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
    nvs_close(my_handle);
    if(reconnect){
        ESP_ERROR_CHECK(wifi_sta_config_from_nvs());
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Send WiFi start");
#if (ESP_IDF_VERSION_MAJOR<5)
        // workaround for I2S DMA read
        // https://github.com/espressif/esp-idf/issues/3714
        esp_wifi_set_ps(WIFI_PS_NONE);
#endif
    }
}


// ap scan requires disconnect etc
void wifi_sta_allow_for_AP_scan()
{
    if(!wifi_is_connected) wifi_connect_retry=0;
}


/* Function to initialize Wi-Fi at station */
void wifi_sta_init(void)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(wifi_sta_config_from_nvs());
    ESP_ERROR_CHECK(esp_wifi_start());
#if (ESP_IDF_VERSION_MAJOR<5)
    // workaround for I2S DMA read
    // https://github.com/espressif/esp-idf/issues/3714
    esp_wifi_set_ps(WIFI_PS_NONE);
#endif

}



