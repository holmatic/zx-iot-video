// holmatic


#ifndef _TAPE_SIGNAL_H_
#define _TAPE_SIGNAL_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

#include <esp_types.h>


#ifdef __cplusplus
extern "C" {
#endif


/*	
 *	TX functionality
 */

typedef enum {
    TTX_DATA_ACE_STDSPEED,    /*!< transmit given at standard-speed */
	TTX_SW_RX_SPEED_STD,	  /*!< switch receiver to standard-speed */
	TTX_NUM_TYPES  		 	  /*!<  */
} taps_txdata_id_t;

typedef struct taps_tx_packet_tag{
	taps_txdata_id_t packet_type_id;
	uint8_t *data;			/*!<  pointer to transmit data, mrea must be valid until packet done  */
	uint32_t datasize;		/*!<  size of data, must be 0 if *data is NULL  */
	uint32_t para;			/*!<  generic parameter, usage depends on packet_type_id  */
}taps_tx_packet_t ;

void taps_tx_enqueue(taps_tx_packet_t* tx_packet, bool wait_done);


/*
taps_tx_enqueue: if (num_active_tx==0 take(); num_active_tx++) num_active_tx is counting semaphore

	thread: done: --num_active_tx; if num_active_tx==0 give() 

void taps_tx_wait_all_done() take();give();


*/

/*	
 *	RX functionality
 */


typedef enum {
    TRX_INIT = 100,       
	TRX_HEADER_STDSPEED,
    TRX_RECEIVED_DATA,
} taps_rxevt_id_t;

/* structure used by the */
typedef struct taps_rd_event_tag{
    taps_rxevt_id_t  evt_type_id;   /*!< sfzx_evt_type_t */
    uint16_t  addr;    /*!<  bytecount for data  */
    uint16_t  data;    /*!<  for data  */
}taps_rd_event_type ;

void taps_rx_set_queue_to_use(QueueHandle_t rx_evt_q);



/*
*		Gerneral (RX and TX)
*/

// call once at startup
void taps_init();


#ifdef __cplusplus
}
#endif

#endif /* _TAPE_SIGNAL_H_ */
