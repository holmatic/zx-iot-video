// holmatic


#ifndef _HOST_SPI_IF_H_
#define _HOST_SPI_IF_H_

#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"


// call once at startup
void host_spi_init();

#endif /* _HOST_SPI_IF_H_ */
