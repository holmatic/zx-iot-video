/* ZX Server

Controls communication to the ZX computer by listening to signal_from and 
sending data via signal_to modules.

Works asynchroously, thus communication is done via queues

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/unistd.h>
#include "esp_err.h"
#include "esp_log.h"


#include "zx_file_img.h"

static const char *TAG = "zx_file_img";





#include "asm_code.c"

static const uint8_t* base_img[ZXFI_NUM]={ldrfile,menufile,str_inp,driver};



/* Helper for text code conversion */

static const char* CODETABLE="#_~\"@$:?()><=+-*/;,.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";	/* ZX81 code table, starting at 28 */

uint8_t convert_ascii_to_zx_code(int ascii_char)
{
	uint8_t zx_code=0;
	int upper_ascii_c=toupper(ascii_char);

    if(ascii_char==' ') return 0;
	while(CODETABLE[zx_code])
	{
		if(CODETABLE[zx_code]==upper_ascii_c) return 8+zx_code;		/* Exit with result on match */
		zx_code++;
	}
	/* return a default */
	return 4;
}


static uint8_t zx_txt_buf[33];

uint16_t convert_ascii_to_zx_str(const char* ascii_str) // return length
{
	uint8_t* d=zx_txt_buf;
	uint8_t inverse=0;
	uint16_t len=0;
	char c;
	while(  (c=*ascii_str++)  ){
		if(c=='[') inverse=0x80;
		else if(c==']') inverse=0;
		else{
			*d++ = convert_ascii_to_zx_code(c) | inverse;
			len++;
		}
	}
	return len;
}

void zx_string_to_ascii(const uint8_t* zxstr, size_t len,  char* buf_for_ascii)
{
    int i;
    uint8_t z;
    char c;
    for(i=0;i<len;i++) {
        c='?';
        z=zxstr[i] & 0x7f; // ignore invert
		if(z==0)
			c=' ';
        else if(z>=8 && z<64) c=CODETABLE[z-8];
		if(c>='A' && c<='Z' && (zxstr[i] & 0x80)  ) c= (char) tolower( (int) c); // inverted -> lower case
        *buf_for_ascii++=c;
    }
    *buf_for_ascii=0;   // string end
    return;
}

static uint8_t *memimg=0;


static const uint16_t img_offs=16393;

static uint16_t raw_fill_size=0;


static uint16_t mem_rd16(uint16_t memaddr)
{
	return memimg[memaddr-img_offs]+256*memimg[memaddr+1-img_offs];
}

static void  mem_wr16(uint16_t memaddr, uint16_t data)
{
	memimg[memaddr-img_offs]=data&0x00ff;
	memimg[memaddr+1-img_offs]= (data&0xff00)>>8;
}

static uint16_t mem_img_size(){
	return memimg[16404-img_offs]+256*memimg[16404+1-img_offs] - img_offs;
}

static void mem_insert(const uint8_t* src, uint16_t insert_pos, uint16_t ins_size){
	uint16_t v,p,old_sz;
	old_sz=mem_img_size();
	// update pointers
	for(v=16396;v<=16412;v+=2)	{
		p=mem_rd16(v);
		if (p>=insert_pos){
			mem_wr16(v,p+ins_size);
		}
	}
	// move
	for(v=old_sz-1; v>=insert_pos-img_offs;v--)	{
		memimg[v+ins_size]=memimg[v];
	}
	// fill
	for(v=0; v<ins_size;v++){
		memimg[v+insert_pos-img_offs]=src[v];
	}
}


void zxfimg_cpzx_video(uint8_t linenum, const uint8_t* zxstr, uint16_t len) {
	uint16_t dfile_pos=mem_rd16(16396);//dfile
	linenum+=1; //find initial lines
	while(linenum){
		if(memimg[dfile_pos++ -img_offs]==0x76) linenum--;
	}
	mem_insert(zxstr,dfile_pos,len);
}



uint8_t* zxfimg_get_dfile(){
	uint16_t dfile_pos=mem_rd16(16396);//dfile
	return &memimg[dfile_pos-img_offs];
}

uint16_t zxfimg_get_dfile_size(){
	uint16_t dfile_pos=mem_rd16(16396);//dfile
	uint16_t vars_pos=mem_rd16(16400);//vars
	return vars_pos-dfile_pos;
}



void zxfimg_print_video(uint8_t linenum, const char* asciitxt) {
	zxfimg_cpzx_video(linenum,zx_txt_buf, convert_ascii_to_zx_str(asciitxt) );
}



// call once at startup
void zxfimg_create(zxfimg_prog_t prog_type) {
	ESP_LOGI(TAG,"zxfimg_create %d \n",prog_type); 
	uint16_t i,sz;
    const uint8_t *src= base_img[prog_type];
	if(!memimg) memimg=calloc(16384,1);
	for(i=0;i<16507-img_offs;i++){
		memimg[i]=src[i];
	}
	sz=mem_img_size();
	for(;i<sz;i++){
		memimg[i]=src[i];
	}
}

uint8_t *zxfimg_get_img() {
    return memimg;
}

void  zxfimg_set_img(uint16_t filepos,uint8_t data) {
	if(!memimg) memimg=calloc(16384*2,1); /*extend size to 32K for binary operation */
	if(memimg && filepos<16384*2){
		memimg[filepos]=data;
		if(filepos+1>raw_fill_size) raw_fill_size=filepos+1;
	}
	else
		ESP_LOGE(TAG,"zxfimg_set_img alloc failed %d ",filepos); 
}




uint16_t zxfimg_get_size() {
    return mem_img_size();
}

uint16_t zxfimg_get_raw_fill_size() {
    return raw_fill_size;
}



void zxfimg_delete() {
	if(memimg) free(memimg);
	memimg=NULL;
	raw_fill_size=0;
}
