// holmatic


#pragma once

#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

#include <esp_types.h>
#include "tape_signal.h"



/*	
 *	TX functionality
 */

bool stzx_setup_new_file(const taps_tx_packet_t* src);

// return true if end of file reached
bool stzx_fill_buf_from_file(uint8_t* samplebuf, size_t buffer_size, uint32_t *actual_len_to_fill);


/*	
 *	RX functionality
 *
 *  Interface to dedicated tape input via same port as output
 * 
 *  Usually talk to zx_server using the events defined there

 */

/* incoming, use all functions single-threaded please */
void sfzx_report_video_signal_status(bool vid_is_active); /* report if we have a regular video signal or are in FAST/LOAD/SAVE/ect */
void sfzx_checksample(uint32_t data);   /* every incoming 32-bit sample as long as vid_is_active=false */
void sfzx_periodic_check();             /* called periodically at roughly millisec scale */
