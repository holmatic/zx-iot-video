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
    TTX_DATA_ZX81_QLOAD,      /*!< transmit given at high speed */
//	TTX_SW_RX_SPEED_STD,	  /*!< switch receiver to standard-speed */
	TTX_NUM_TYPES  		 	  /*!<  */
} taps_txdata_id_t;

typedef struct taps_tx_packet_tag{
	taps_txdata_id_t packet_type_id;
	uint8_t *data;			/*!<  pointer to transmit data, mrea must be valid until packet done  */
	uint32_t datasize;		/*!<  size of data, must be 0 if *data is NULL  */
	uint32_t para;			/*!<  generic parameter, usage depends on packet_type_id  */
} taps_tx_packet_t ;

/* submit to send a file or message, optionally wait till done */
void taps_tx_enqueue(taps_tx_packet_t* tx_packet, bool wait_done);

/* wait till all pending transmissions done */
void taps_tx_wait_all_done();

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



/* what type of data to listen to */
typedef enum {
    TRX_IGNORE = 100,       
	TRX_HEADER_STDSPEED,    /* normal SAVE OUTPUT */
} taps_rx_inp_t;

void taps_rx_set_listen_mode(taps_rx_inp_t mode);



/*
*		Gerneral (RX and TX)
*/

// call once at startup
void taps_init();


#ifdef __cplusplus
}
#endif

#endif /* _TAPE_SIGNAL_H_ */
