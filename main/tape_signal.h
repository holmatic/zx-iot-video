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
    TTX_DATA_ZX81_STDSPEED,   /*!< transmit given at standard-speed */
    TTX_DATA_ZX81_QLOAD,      /*!< transmit given at high speed   inversion level in para  */
//	TTX_SW_RX_SPEED_STD,	  /*!< switch receiver to standard-speed */
	TTX_NUM_TYPES  		 	  /*!<  */
} taps_txdata_id_t;

typedef struct taps_tx_packet_tag{
	taps_txdata_id_t packet_type_id;
	uint8_t *name;			/*!<  pointer to transmit name,  must be valid until packet done, NULL if NA  */
	uint32_t namesize;		/*!<  size of data, must be 0 if *name is NULL  */
	uint8_t *data;			/*!<  pointer to transmit data, must be valid until packet done, NULL if NA  */
	uint32_t datasize;		/*!<  size of data, must be 0 if *data is NULL  */
	uint32_t para;			/*!<  generic parameter, usage depends on packet_type_id   inversion level for qload  */ 
	uint16_t predelay_ms;	/*!<  inserted delay before sending the content */
} taps_tx_packet_t ;

/* submit to send a file or message, optionally wait till done */
void taps_tx_enqueue(taps_tx_packet_t* tx_packet, bool wait_done);

/* wait till all pending transmissions done, NOTE must be in same thread as taps_tx_enqueue, or externally mutexed */
void taps_tx_wait_all_done();


bool taps_is_tx_active();

/*
taps_tx_enqueue: if (num_active_tx==0 take(); num_active_tx++) num_active_tx is counting semaphore

	thread: done: --num_active_tx; if num_active_tx==0 give() 

void taps_tx_wait_all_done() take();give();


*/

/*	
 *	RX functionality
 *
 *  Interface to dedicated tape input via same port as output
 * 
 *  Usually talk to zx_server using the events defined there

 */



/*
*		Gerneral (RX and TX)
*/

// call once at startup
void taps_init(bool listen_mode);


#ifdef __cplusplus
}
#endif

#endif /* _TAPE_SIGNAL_H_ */
