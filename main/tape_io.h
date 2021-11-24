// holmatic



// low-level functionality for transmitting (simultaneous RX/TX) computer tape signals per
// one bit digitizer in the form of MSB-first byte-sized data. Transmission size can be determined in bits.
// 

#ifndef _TAPE_IO_H_
#define _TAPE_IO_H_

#include <esp_types.h>

#ifdef __cplusplus
extern "C" {
#endif


#define  TAPIO_SAMPLE_SPEED_HZ  125000

#define  TAPIO_OVERSAMPLE 4

// buffer size in bytes for receive and transmit; multiple alternating sets are used so watch for mem consumption when increasing
#define  TAPIO_MAX_TRANSFER_LEN_BYTES  (1024) 

// register function on init to be called back when data is received
typedef void (*tapio_receive_callback_t)(uint8_t*, int);

// call once at startup
// callback(uint8_t* buffer, int size_in_bits)
void tapio_init(tapio_receive_callback_t on_incomming_data);

// wait until a tranfer ends if transfers still pending, callback may be triggered, return number of remaing active transfers
int tapio_wait_and_finish_transfer();

// if needed then wait until a transfer ends (callback may be triggered) , start new tranfer
// transmit_len_bits must be <= TAPIO_MAX_TRANSFER_LEN_BYTES*8; if transmit_len_bits=0, then it is set to the maximum automatically
void tapio_process_next_transfer(int transmit_len_bits);

// get a transmit buffer to fill for the next tapio_process_next_transfer; note it is TAPIO_MAX_TRANSFER_LEN_BYTES
uint8_t *tapio_get_next_transmit_buffer();

// set all buffers to 0, for example at start of a period of silent-output/receive-only
void tapio_clear_transmit_buffers();

#ifdef __cplusplus
}
#endif

#endif /* _TAPE_IO_H_ */
