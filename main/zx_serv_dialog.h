// holmatic
/* ZX Serv Dialog

Controls creation and response from tiny ZX programs/screens with interactive content
to build a multi-stage menu system

*/


#ifndef _ZX_SERV_DIALOG_H_
#define _ZX_SERV_DIALOG_H_
#include "esp_err.h"
#include <esp_types.h>


#ifdef __cplusplus
extern "C" {
#endif


void zxdlg_reset();


bool zxdlg_respond_from_key(uint8_t key);

bool zxdlg_respond_from_string(uint8_t* strg, uint8_t len);









#ifdef __cplusplus
}
#endif

#endif /* _ZX_SERV_DIALOG_H_ */
