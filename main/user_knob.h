

#ifndef _USER_KNOB_H_
#define _USER_KNOB_H_

#include "esp_err.h"
#include <esp_types.h>


 
// call periodically from suitable thread
void user_knob_periodic_check();


// call once at startup
void user_knob_init();



#endif /* _USER_KNOB_H_ */
