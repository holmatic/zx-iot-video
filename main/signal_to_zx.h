// holmatic


#ifndef _SIGNAL_TO_ZX_H_
#define _SIGNAL_TO_ZX_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// call once at startup
void stzx_init();


typedef enum {
    STZX_FILE_START=200,    /*!< start file, data will follow with next cmd, data 0 */
	STZX_FILE_DATA,   /*!< data byte, to be sent between start and end */
	STZX_FILE_END,      /*!< file end, all data had been retieved, data 0 */
} stzx_mode_t;

#define ZX_SAVE_TAG_LOADER_RESPONSE 70	// Initial loader responds with this tag and the RAMTOP info
#define ZX_SAVE_TAG_MENU_RESPONSE   73	// Initial loader responds with this tag and the RAMTOP info


void stzx_send_cmd(stzx_mode_t cmd, uint8_t data);

void stzx_set_out_inv_level(bool inv);

bool stzx_is_transfer_active();

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_TO_ZX_H_ */
