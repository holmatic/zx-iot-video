// holmatic

// Creation of ZX file images with dynamic content

#ifndef _ZX_FILE_IMG_H_
#define _ZX_FILE_IMG_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {

    ZXFI_LOADER     ,       /* initial loader */
    ZXFI_MENU_KEY  ,        /* shows menu and responds on keypress */
    ZXFI_STR_INP,           /* input string */
    ZXFI_DRIVER,            /* downloadable driver for binary transfers etc */
    ZXFI_NUM
} zxfimg_prog_t;


// call once at startup
void zxfimg_create(zxfimg_prog_t prog_type);
void zxfimg_print_video(uint8_t linenum, const char* asciitxt);
void zxfimg_cpzx_video(uint8_t linenum, const uint8_t* zxstr, uint16_t len);

void zxfimg_set_img(uint16_t filepos,uint8_t data);
uint8_t* zxfimg_get_img();
uint16_t zxfimg_get_size();
uint16_t zxfimg_get_raw_fill_size();
void zxfimg_delete();


uint16_t convert_ascii_to_zx_str(const char* ascii_str); // return length
uint8_t convert_ascii_to_zx_code(int ascii_char);
void zx_string_to_ascii(const uint8_t* zxstr, size_t len,  char* buf_for_ascii);


#ifdef __cplusplus
}
#endif

#endif /* _ZX_FILE_IMG_H_ */
