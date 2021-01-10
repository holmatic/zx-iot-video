// holmatic


#ifndef _TFT_DISPLAY_H_
#define _TFT_DISPLAY_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"


// call once at startup
void lcd_disp_init();

#endif /* _TFT_DISPLAY_H_ */
