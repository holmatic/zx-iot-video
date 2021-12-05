// holmatic
// continously scans the video signal input and puts result into the pixel memory


#ifndef _IIS_VIDEOSIGNAL_H_
#define _IIS_VIDEOSIGNAL_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"


// call once at startup
void vid_init();

// start a procedure to calibrate the timing based on the current (special pattern) picture
void vid_cal_pixel_start(); 

void vid_user_scr_adj_event();  

bool vid_is_synced();

// low-level function for data stream access
uint32_t vid_get_next_data();

/* number of top-header lines before actual standard-character screen starts*/
extern uint32_t vid_get_vline_offset();

// monochrome pixel memory, written by this module, and read by the display driver(s) VGA/LCD/LED
extern uint32_t vid_pixel_mem[];

// if another module wants to write (parts of) the screen, these ranges can be set so that the upper/lower part is left untouched by the video input scanner;
// expect ~64us delay after changing this till effectice as video scanner will complete the actual line first
extern uint8_t vid_scan_startline; // default   0, first line that is scanned and put into the pixel mem, lines above are not touched by this module 
extern uint8_t vid_scan_endline;   // default 239, last line that is scanned and put into the pixel mem, lines above are not touched by this module




#endif /* _IIS_VIDEOSIGNAL_H_ */
