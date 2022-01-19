/* ZX Server

Controls communication to the ZX computer by listening to signal_from and 
sending data via signal_to modules.

Works asynchronously, thus communication is done via queues

There is a shared incoming queue for events like silence/fileheader/etc that contains a small data buffer for 16byte messages (recognized names etc)

Some data then comes via a data queue like files stored


*/


#ifndef _ZX_SERVER_H_
#define _ZX_SERVER_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// call once at startup
void zxsrv_init();

typedef enum {

    ZXSG_INIT       = 300,        /*!< initial status */
    ZXSG_SLOWM_50HZ  ,        /*!< start retriving file */
    ZXSG_SLOWM_60HZ,                 /*!< during file transfer, check if ZX loads or ignores */
	ZXSG_SAVE,
	ZXSG_SILENCE,
	ZXSG_HIGH ,
	ZXSG_NOISE,

	ZXSG_OCR_NAME,
	ZXSG_OCR_DATA,
    ZXSG_QSAVE_TAG,
    ZXSG_QSAVE_EXIT,        /* data will be 0 for error, 1 for ok */

    ZXSG_FILE_DATA = 400,
    ZXSG_QSAVE_DATA,

} zxserv_evt_type_t;



#define FILE_NOT_ACTIVE 0
#define FILE_TAG_NORMAL 101
#define FILE_TAG_COMPRESSED 202


#define ZX_SAVE_TAG_LOADER_RESPONSE 70	// Initial loader responds with this tag and the RAMTOP info
#define ZX_SAVE_TAG_MENU_RESPONSE   73	// Menu user input key
#define ZX_SAVE_TAG_STRING_RESPONSE 74	// String input from menu dialog
#define ZX_SAVE_TAG_QSAVE_START     75	// Fast transfer 

#define ZX_QSAVE_TAG_HANDSHAKE      90	// Wespi should respond with some dummy signal to see it is there
#define ZX_QSAVE_TAG_SAVEPFILE      91
#define ZX_QSAVE_TAG_LOADPFILE      93  // load and binload and DIR

#define ZX_QSAVE_TAG_DATA           95
#define ZX_QSAVE_TAG_END_NOREPLY    98
#define ZX_QSAVE_TAG_END_RQ         99
#define ZX_QSAVE_END_RES_TAG         42


typedef struct {
    zxserv_evt_type_t  evt_type;   /*!< sfzx_evt_type_t */
    uint16_t  addr;    /*!<   */
    uint16_t  data;    /*!<   */
} zxserv_event_t;

void zxsrv_send_msg_to_srv( zxserv_evt_type_t msg, uint16_t addr, uint16_t data);

zxserv_evt_type_t zxsrv_get_zx_status();

#ifdef __cplusplus
}
#endif

#endif /* _ZX_SERVER_H_ */
