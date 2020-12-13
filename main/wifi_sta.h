// holmatic


#ifndef _WIFI_STA_H_
#define _WIFI_STA_H_

#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"


// call once at startup, uses the nvs for storage of AP infos, requires nvs to be initialized
void wifi_sta_init();

// write new AP info to nvs, NULL pointer to omit, afterwards optionally (disconnect and) connect
void wifi_sta_reconfig(const char* ssid, const char* wf_pass, bool reconnect);

// return true if successfully connected and IP provided
bool wifi_sta_is_connected();

// ap scan requires disconnect etc
void wifi_sta_allow_for_AP_scan();

// status message
const char* wifi_get_status_msg();

// status message
const char* wifi_get_MAC_addr();

#endif /* _WIFI_STA_H_ */
