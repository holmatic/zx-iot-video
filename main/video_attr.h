

#ifndef _VIDEO_ATTR_H_
#define _VIDEO_ATTR_H_

#include "esp_err.h"
#include <esp_types.h>


#define VIDATTR_HSYNC_MASK 0x40
#define VIDATTR_VSYNC_MASK 0x80
#define VIDATTR_RED   0x08
#define VIDATTR_GREEN 0x04
#define VIDATTR_BLUE  0x02
#define VIDATTR_WHITE 0x0e
#define VIDATTR_CYAN  0x06
#define VIDATTR_MAGENTA 0x0a
#define VIDATTR_YELLOW 0x0c
#define VIDATTR_BLACK 0x00


#define VIDATTR_ATTR_SCR_SIZE (40*30)


/* return pointers to the 40x30 fields attribute memory */
void vidattr_get_mem(uint8_t** fg_mem, uint8_t** bg_mem);

/*  */
void vidattr_set_c_mode(char colour); // GYWF
void vidattr_set_inv_mode(bool invert);


// call once at startup
void video_attr_init();




#endif /* _VIDEO_ATTR_H_ */
