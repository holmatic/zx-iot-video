#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_master.h"

//#include "zx_server.h"
#include "tape_zx.h"
#include "tape_io.h"
#include "tape_signal.h"

static const char* TAG = "taps";

static void taps_task(void*arg);


static QueueHandle_t tx_cmd_queue;	/* orders for tranmissions, memory owned by caller, needs to be valid until */

static QueueHandle_t done_msg_queue; /* single byte dummy ack for tranmission done */

static void taps_task(void*arg)
{
    
	while(true){
		/* receive only */
		tapio_clear_transmit_buffers();
		while( uxQueueMessagesWaiting( tx_cmd_queue )==0  ){
			tapio_process_next_transfer(0);
		}
		/* handle transmit */
		taps_tx_packet_t file;
		uint8_t *buf;
		uint32_t siz;
		bool end_of_file=false;
		if(xQueuePeek(tx_cmd_queue, &file,1 )){	/*  peek just reads without removing */
			stzx_setup_new_file(&file);
			do{
				buf = tapio_get_next_transmit_buffer();			
				end_of_file = stzx_fill_buf_from_file( buf, TAPIO_MAX_TRANSFER_LEN_BYTES, &siz) ;
				tapio_process_next_transfer(siz*8);
			} while(!end_of_file);
			while ( tapio_wait_and_finish_transfer() ){
				/* just wait till all done so we can clear the buffers */
			}
			xQueueReceive(tx_cmd_queue, &file,1 ); /*  done, now we remove the item */
			xQueueSendToBack( done_msg_queue, buf, portTICK_PERIOD_MS*100 );
		}
	}
}



// call once at startup
void taps_init(bool listen_mode)
{
	tx_cmd_queue=xQueueCreate(5, sizeof( taps_tx_packet_t ) );
	done_msg_queue=xQueueCreate(5, sizeof( uint8_t ) );
	tapio_init(NULL); /* TODO use listen_mode */
	xTaskCreate(taps_task, "taps_task", 1024 * 3, NULL, 9, NULL);
}

static uint32_t outstanding_acks=0;

/* submit to send a file or message, optionally wait till done */
void taps_tx_enqueue(taps_tx_packet_t* tx_packet, bool wait_done){
	char dummy;
	xQueueSendToBack( tx_cmd_queue, tx_packet, portTICK_PERIOD_MS*100 ); 
	if(wait_done)
		xQueueReceive(done_msg_queue, &dummy, portTICK_PERIOD_MS*1000*3600 ); /* could take a while for long tape */
	else
		outstanding_acks++;
}


/* wait till all pending transmissions done, must be in same thread as taps_tx_enqueue */
void taps_tx_wait_all_done(){
	uint8_t dummy=0;
	while(outstanding_acks){
		if (xQueueReceive(done_msg_queue, &dummy, portTICK_PERIOD_MS*1000 )) outstanding_acks--; 
	}

}


bool taps_is_tx_active(){
	return uxQueueMessagesWaiting( tx_cmd_queue )>=1;
}
