
#include <sys/param.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "esp_spi_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "iis_videosig.h"
#include "video_attr.h"

static const char *TAG="vid_attr";

uint8_t* attr_mem_fg=NULL; // swapped words (ix^2)
uint8_t* attr_mem_bg=NULL; // swapped words (ix^2)



/* ZX81 charset as stored in rom  */

const uint8_t zx_charset[512]={
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0x0F,0x0F,0x0F,0x0F,0xF0,0xF0,0xF0,0xF0,0xFF,0xFF,0xFF,0xFF,0xF0,0xF0,0xF0,0xF0,
  0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0x00,0x00,0x00,0x00,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0x00,0x00,0x00,0x00,0x00,0x24,0x24,0x00,0x00,0x00,0x00,0x00,
  0x00,0x1C,0x22,0x78,0x20,0x20,0x7E,0x00,0x00,0x08,0x3E,0x28,0x3E,0x0A,0x3E,0x08,0x00,0x00,0x00,0x10,0x00,0x00,0x10,0x00,0x00,0x3C,0x42,0x04,0x08,0x00,0x08,0x00,
  0x00,0x04,0x08,0x08,0x08,0x08,0x04,0x00,0x00,0x20,0x10,0x10,0x10,0x10,0x20,0x00,0x00,0x00,0x10,0x08,0x04,0x08,0x10,0x00,0x00,0x00,0x04,0x08,0x10,0x08,0x04,0x00,
  0x00,0x00,0x00,0x3E,0x00,0x3E,0x00,0x00,0x00,0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x3E,0x00,0x00,0x00,0x00,0x00,0x14,0x08,0x3E,0x08,0x14,0x00,
  0x00,0x00,0x02,0x04,0x08,0x10,0x20,0x00,0x00,0x00,0x10,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,
  0x00,0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00,0x18,0x28,0x08,0x08,0x08,0x3E,0x00,0x00,0x3C,0x42,0x02,0x3C,0x40,0x7E,0x00,0x00,0x3C,0x42,0x0C,0x02,0x42,0x3C,0x00,
  0x00,0x08,0x18,0x28,0x48,0x7E,0x08,0x00,0x00,0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00,0x00,0x3C,0x40,0x7C,0x42,0x42,0x3C,0x00,0x00,0x7E,0x02,0x04,0x08,0x10,0x10,0x00,
  0x00,0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00,0x3C,0x42,0x42,0x3E,0x02,0x3C,0x00,0x00,0x3C,0x42,0x42,0x7E,0x42,0x42,0x00,0x00,0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,
  0x00,0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00,0x78,0x44,0x42,0x42,0x44,0x78,0x00,0x00,0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00,0x00,0x7E,0x40,0x7C,0x40,0x40,0x40,0x00,
  0x00,0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00,0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00,0x3E,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x02,0x02,0x02,0x42,0x42,0x3C,0x00,
  0x00,0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x42,0x66,0x5A,0x42,0x42,0x42,0x00,0x00,0x42,0x62,0x52,0x4A,0x46,0x42,0x00,
  0x00,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40,0x00,0x00,0x3C,0x42,0x42,0x52,0x4A,0x3C,0x00,0x00,0x7C,0x42,0x42,0x7C,0x44,0x42,0x00,
  0x00,0x3C,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00,0xFE,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x42,0x42,0x42,0x42,0x24,0x18,0x00,
  0x00,0x42,0x42,0x42,0x42,0x5A,0x24,0x00,0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00,0x82,0x44,0x28,0x10,0x10,0x10,0x00,0x00,0x7E,0x04,0x08,0x10,0x20,0x7E,0x00
};


#define VBITMAP_IS_VALID_CHAR_L2 0x01
#define VBITMAP_IS_BLOCK_CHAR_L2 0x04
#define VBITMAP_IS_GREY_CHAR_L2  0x08
#define VBITMAP_IS_INV_CHAR_L2   0x02
#define VBITMAP_IS_VALID_CHAR_L6 0x10
#define VBITMAP_IS_BLOCK_CHAR_L6 0x40
#define VBITMAP_IS_GREY_CHAR_L6  0x80
#define VBITMAP_IS_INV_CHAR_L6   0x06

/* For the standard ZX81 charset, every possible bit map pattern (when looking at line 2 or 6 of the character) maps to flags that show if this is a valid char, and possible also if it is graphics etc */

const uint8_t vbitm_flags[256]={0x11,0x00,0x01,0x00,0x11,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x55,0x11,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x01,0x00,0x11,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x11,0x00,0x11,0x00,0x11,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x10,0x00,0x10,0x00,0x00,0x30,0x00,0x30,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x33,0x00,0x33,0x00,0x33,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x33,0x00,0x03,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x33,0x55,0x00,0x00,0x00,0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x33,0x00,0x03,0x00,0x55};

static int avg_empty_cnt=0;
static int avg_invalid_cnt=0;
static int avg_dark_cnt=0;

static bool vmode_nofancy=false;
static bool vmode_dark=false;




static uint8_t preferred_fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_MAGENTA;
static uint8_t preferred_bg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_BLACK;


static bool inv_setting=false;
static bool fancymode_setting=true;
static char colour_setting='G';
static bool applied_invert=false;



static void fill_attr_mem(){
    uint8_t fg=preferred_fg;
    uint8_t bg=preferred_bg;
    if(vmode_nofancy){ 
      if(!applied_invert){
        fg=preferred_bg;
        bg=preferred_fg;
      }
    } else { /* fancy mode */
      if(applied_invert){
        fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_BLACK;
        bg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_WHITE;  // written
      } else {
        fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_WHITE;   // written
        bg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_BLACK;
      }
    }
    for(int i=0;i<30*40;i++){
      attr_mem_fg[i] = fg ;
      attr_mem_bg[i] = bg ;
    } 
    ESP_LOGI(TAG, "VVV fill_attr_mem  - fg %x, bg %x, fm %d, afg %x",fg,bg,fancymode_setting,attr_mem_fg[125] );        

}





static uint8_t get_fg_colour_from_nv(){
    esp_err_t err;
    uint8_t val=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_WHITE;
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u8(my_handle, "VID_COL_FG", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
   nvs_close(my_handle);
   return val;
}

static void store_fg_colour_in_nv(uint8_t new_val){
    if(new_val!=get_fg_colour_from_nv()){
      nvs_handle my_handle;
      ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
      ESP_ERROR_CHECK( nvs_set_u8(my_handle, "VID_COL_FG", new_val ) );
      ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
      nvs_close(my_handle);
    }
}



static uint8_t get_bg_colour_from_nv(){
    esp_err_t err;
    uint8_t val=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_BLACK;
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    err = nvs_get_u8(my_handle, "V_COL_BG", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
   nvs_close(my_handle);
   return val;
}

static void store_bg_colour_in_nv(uint8_t new_val){
    if(new_val!=get_bg_colour_from_nv()){
      nvs_handle my_handle;
      ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
      ESP_ERROR_CHECK( nvs_set_u8(my_handle, "V_COL_BG", new_val ) );
      ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
      nvs_close(my_handle);
    }
}




static uint8_t get_inv_colour_from_nv(){
    esp_err_t err;
    uint8_t val=0;
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u8(my_handle, "V_COL_INV", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
   nvs_close(my_handle);
   return val;
}

static void store_inv_colour_in_nv(uint8_t new_val){
    if(new_val!=get_inv_colour_from_nv()){
      nvs_handle my_handle;
      ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
      ESP_ERROR_CHECK( nvs_set_u8(my_handle, "V_COL_INV", new_val ) );
      ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
      nvs_close(my_handle);
    }
}



static uint8_t get_fancy_colour_from_nv(){
    esp_err_t err;
    uint8_t val=0;
    nvs_handle my_handle;
    ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
    // Read
    err = nvs_get_u8(my_handle, "V_COL_FANCY", &val);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){
        ESP_ERROR_CHECK( err );
    }
   nvs_close(my_handle);
   return val;
}

static void store_fancy_colour_in_nv(uint8_t new_val){
    if(new_val!=get_fancy_colour_from_nv()){
      nvs_handle my_handle;
      ESP_ERROR_CHECK( nvs_open("zxstorage", NVS_READWRITE, &my_handle) );
      ESP_ERROR_CHECK( nvs_set_u8(my_handle, "V_COL_FANCY", new_val ) );
      ESP_ERROR_CHECK( nvs_commit(my_handle) ); 
      nvs_close(my_handle);
    }
}



/* return pointers to the 40x30 fields attribute memory */
void vidattr_get_mem(uint8_t** fg_mem, uint8_t** bg_mem){
  if(!attr_mem_fg) attr_mem_fg=malloc(VIDATTR_ATTR_SCR_SIZE);
  if(!attr_mem_bg) attr_mem_bg=malloc(VIDATTR_ATTR_SCR_SIZE);
  if(fg_mem) *fg_mem=attr_mem_fg;
  if(bg_mem) *bg_mem=attr_mem_bg;
}

/*  */
void vidattr_set_c_mode(char newcolour){
  if(colour_setting!=newcolour){
    colour_setting=newcolour;
    fancymode_setting=false; // default, may be corrected later
    preferred_bg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_BLACK;
    switch(newcolour){
        case 'G': 
          preferred_fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_GREEN;// VIDATTR_GREEN;
          vmode_nofancy=true;
          break;
        case 'B': 
          preferred_fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_WHITE;
          preferred_bg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_BLUE;
          vmode_nofancy=true;
          break;
        case 'Y': 
        case 'A': 
          preferred_fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_YELLOW;
          vmode_nofancy=true;
          break;
        case 'W': 
          preferred_fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK|VIDATTR_WHITE;
          vmode_nofancy=true;
          break;
        case 'F': 
          fancymode_setting=true;
          vmode_nofancy=false;
          inv_setting=false;
          vmode_dark=inv_setting;
          break;
        default: // no change
          break;
    }
    applied_invert=inv_setting;
    fill_attr_mem();
    store_fg_colour_in_nv(preferred_fg);
    store_bg_colour_in_nv(preferred_bg);
    store_inv_colour_in_nv(inv_setting?1:0);
    store_fancy_colour_in_nv(fancymode_setting);
  }
}


void vidattr_set_inv_mode(bool invert){
  if(invert!=inv_setting){
    inv_setting=invert;
    if(!vmode_nofancy) 
      applied_invert = (inv_setting!=vmode_dark);
    else
      applied_invert=inv_setting;
    fill_attr_mem();
  }
  store_inv_colour_in_nv(inv_setting?1:0);
}






static void create_fancy_colours()
{
    uint8_t * pix_mem8=(uint8_t *)vid_pixel_mem;
    int empty_cnt=0;
    int invalid_cnt=0;
    int dark_cnt=0;
    static uint8_t switchmode_holdoff=2;
    //uint8_t *curr_attr= attr_mem_fg;
    bool v= applied_invert;
    uint8_t *curr_attr= applied_invert ? attr_mem_bg:attr_mem_fg;
    uint8_t TEXT_COL = v ? VIDATTR_WHITE : VIDATTR_GREEN ;// ;
    uint8_t ITEXT_COL = v ? VIDATTR_GREEN :  VIDATTR_YELLOW;
    uint8_t BLOCK_COL = v ? VIDATTR_WHITE :  VIDATTR_WHITE;
    uint8_t GREY_COL = v ? VIDATTR_RED :  VIDATTR_CYAN;
    for(int y=0;y<24;y++){
      /* check for overscan */
      if(pix_mem8[ ( (24+2+y*8)*40 + 3)^3 ]) invalid_cnt+=4;
      /* now regular area */
      for(int x=0;x<32;x++){
        uint8_t pattern=pix_mem8[ ( (24+2+y*8)*40 +4+x)^3 ];
        uint8_t fg=VIDATTR_HSYNC_MASK|VIDATTR_VSYNC_MASK;
        uint8_t flags;
        if(pattern!=0){
          /* upper part not empty */
          flags=vbitm_flags[pattern];
          if(0 == (flags&VBITMAP_IS_VALID_CHAR_L2) ){
            fg|=TEXT_COL;  /* empty or normal text */
            invalid_cnt++; /* maybe high-res or so, nothing that fancy colours work with.. */
          } else if ( flags&VBITMAP_IS_BLOCK_CHAR_L2 ){
            fg|=BLOCK_COL; // blocks-> white
            if(pattern==0xff) dark_cnt++;
          } else if ( flags&VBITMAP_IS_INV_CHAR_L2 ){
            fg|=ITEXT_COL; // inverse text ->  yellow
            dark_cnt++;
          } else if ( flags&VBITMAP_IS_GREY_CHAR_L6 ){
            fg|=GREY_COL; // chequered-> cyan
          }else{
            fg|=TEXT_COL;  /* some normal text */
          }
        }else{
          /* upper part just empty */
          pattern=pix_mem8[ ( (24+6+y*8)*40 +4+x)^3 ];
          if(pattern==0){
              empty_cnt++;
              fg|=TEXT_COL;  /* empty or some normal text */
          } else {
            flags=vbitm_flags[pattern];
            if ( flags&VBITMAP_IS_BLOCK_CHAR_L6 ){
              fg|=BLOCK_COL; // blocks-> white
              if(pattern==0xff) dark_cnt++;
            } else if ( flags&VBITMAP_IS_GREY_CHAR_L6 ){
              fg|=GREY_COL; // chequered-> cyan
            }else{
              fg|=TEXT_COL;  /* empty or some normal text */
            }
          } 
        }
        if(!vmode_nofancy) curr_attr[(3+y)*40+4+x] = fg; // green on black
      } 
    }
    
    avg_empty_cnt=   (7*avg_empty_cnt + empty_cnt) /8;
    avg_invalid_cnt= (7*avg_invalid_cnt + invalid_cnt) /8;
    avg_dark_cnt=    (7*avg_dark_cnt  + dark_cnt) /8;

    if(switchmode_holdoff) {
      switchmode_holdoff--;
    } else {
      if(vmode_nofancy){
        // check if screen looks like regular characters again
        if( ( avg_invalid_cnt<2 || avg_invalid_cnt*16 < (768-avg_empty_cnt) ) && vid_is_synced() ){
            vmode_nofancy=false;
            switchmode_holdoff=50;
            fill_attr_mem();
        }
      } else {
        // check if screen looks like HRG or invalid
        if(avg_invalid_cnt > 4 && ( avg_invalid_cnt*8 > (768-avg_empty_cnt) || !vid_is_synced() ) ){
            vmode_nofancy=true;
            switchmode_holdoff=50;
            // remove colour info
            fill_attr_mem();
        }
      }

      if(vmode_dark){
        // check if screen looks normal/bright
        if(avg_dark_cnt<284){
            vmode_dark=false;
            switchmode_holdoff=50;
            // remove colour info
            applied_invert = (inv_setting!=vmode_dark);
            fill_attr_mem();
        }
      } else {
        // check if screen looks dark by inverse
        if(avg_dark_cnt > 484){
            vmode_dark=true;
            switchmode_holdoff=50;
            applied_invert = (inv_setting!=vmode_dark);
            fill_attr_mem();
        }
      }
    }

}





#define VBIT_CHAR_MISMATCH 0xff
#define VBIT_MAXSCORE 4


static uint8_t ocr_code[32];
static uint8_t ocr_score[32];

static void ocr_report_char(uint8_t xpos, uint8_t ypos, uint8_t charcode){
  if(charcode==ocr_code[xpos]){
    if(ocr_score[xpos] <VBIT_MAXSCORE){
      ocr_score[xpos]++;
    }
  }
  else{
    if(ocr_score[xpos]==0){
      ocr_code[xpos]=charcode;  /* old status expired, have new */
      ESP_LOGW(TAG, "OCR code %02x at pos %d",charcode,xpos );        
    }
    else ocr_score[xpos]--;
  }
}


static void ocr_scan_screen()
{
    uint8_t * pix_mem8=(uint8_t *)vid_pixel_mem;
    uint8_t flags;
    if(!vid_is_synced()) return; /* do not check if no input active */
    /* single line for now */
    for(int y=23;y<24;y++){
      /* full line */
      for(int x=0;x<32;x++){
        // check single character, first if letter at all
        uint8_t pattern=pix_mem8[ ( (24+2+y*8)*40 +4+x)^3 ]; // get characteristic byte at L2
        bool is_char=false;
        bool is_inv=false;
        if(pattern!=0){
          /* upper part not empty */
          flags=vbitm_flags[pattern];
          if(     (flags&VBITMAP_IS_VALID_CHAR_L2) ){
            is_char=true;  /* normal text */
            is_inv = ( flags&VBITMAP_IS_INV_CHAR_L2 ) ? true : false;  /* inverted */
          }
        }else{
          /* upper part just empty, try lower */
          pattern=pix_mem8[ ( (24+6+y*8)*40 +4+x)^3 ];
          if(pattern==0){
              ocr_report_char(x,y,0); /* assume it is all empty */
              continue;
          } else {
            flags=vbitm_flags[pattern];
            if(     (flags&VBITMAP_IS_VALID_CHAR_L6) ){
              is_char=true;  /* normal text */
              is_inv = ( flags&VBITMAP_IS_INV_CHAR_L6 ) ? true : false;  /* empty or normal text */
            }
          } 
        }
        /* see if we have a hint */
        if(is_char){
          uint8_t invmask= is_inv ? 0xff : 0;
          for(uint8_t ch=1;ch<64;ch++) {
              for(uint8_t b=7;b>0;b--){ /* only inner 6 bytes, others are blank anyway, start at lower as more specific */
                pattern=pix_mem8[ ( (24+b+y*8)*40 +4+x)^3 ]^invmask;
                if (pattern!=zx_charset[ch*8+b]) goto ocr_scan_no_match;
              }
              /* match ! */
              ocr_report_char(x,y,  ch | (0x80&invmask) ); 
              goto ocr_scan_have_match;
ocr_scan_no_match:
              continue; /* try next char */
          }
        }
        ocr_report_char(x,y,VBIT_CHAR_MISMATCH); /* mo character matched */
ocr_scan_have_match:
         continue; /* go to next pos */
      } /* x */
    } /* y */
    
}





static void vid_attr_task(void*arg)
{
    int frames=0;
    inv_setting=get_inv_colour_from_nv();
    preferred_fg=get_fg_colour_from_nv();
    preferred_bg=get_bg_colour_from_nv();
    fancymode_setting=get_fancy_colour_from_nv();
    applied_invert=inv_setting;
    vmode_dark=inv_setting;
    vmode_nofancy=true;
    fill_attr_mem();
    //ESP_LOGI(TAG, "VVVVVVXXVV1 Clr - fg %x, bg %x, fm %d, afg %x",preferred_fg,preferred_bg,fancymode_setting,attr_mem_fg[125] );        

    while(1){
        frames++;
        vTaskDelay(5 / portTICK_RATE_MS); 
        if(fancymode_setting){
          create_fancy_colours();
        }
        vTaskDelay(5 / portTICK_RATE_MS); 
        if(frames&1) ocr_scan_screen();
    }
}


/* Function to initialize Wi-Fi at station */
void video_attr_init()
{
  vidattr_get_mem(0,0); // allocate attribute mem
  xTaskCreate(vid_attr_task, "vid_attr_task", 1024 * 2, NULL, 2, NULL);
}



