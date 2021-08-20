// holmatic


#ifndef _SIGNAL_FROM_ZX_H_
#define _SIGNAL_FROM_ZX_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

// call once at startup
void sfzx_init();

/* incoming, use all functions single-threaded please */
void sfzx_report_video_signal_status(bool vid_is_active); /* report if we have a regular video signal or are in FAST/LOAD/SAVE/ect */
void sfzx_checksample(uint32_t data);   /* every incoming 32-bit sample as long as vid_is_active=false */
void sfzx_periodic_check();             /* called periodically at roughly millisec scale */





#endif /* _SIGNAL_FROM_ZX_H_ */
