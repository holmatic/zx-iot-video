// holmatic


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

    ZXSG_FILE_DATA = 400,

} zxserv_evt_type_t;


#define FILE_NOT_ACTIVE 0
#define FILE_TAG_NORMAL 101
#define FILE_TAG_COMPRESSED 202

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
