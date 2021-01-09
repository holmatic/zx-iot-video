// holmatic


#ifndef _IIS_VIDEOSIGNAL_H_
#define _IIS_VIDEOSIGNAL_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"


// call once at startup
void vid_init();
void vid_cal_pixel_start();
bool vid_is_synced();
extern uint32_t vid_pixel_mem[];

/* number of top-header lines before actual standard-character screen starts*/
extern uint32_t vid_get_vline_offset();

#endif /* _IIS_VIDEOSIGNAL_H_ */
