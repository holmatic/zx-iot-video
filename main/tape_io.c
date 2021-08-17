#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "tape_io.h"


#define TAP_IN_PIN 25

#define  BASE_SPEED_HZ  TAPIO_SAMPLE_SPEED_HZ
#define  OVERSAMPLE 1
#define  NUM_PARALLEL_TRANSFERS  4
#define MILLISEC_TO_BYTE_SAMPLES(ms)   (OVERSAMPLE*ms*BASE_SPEED_HZ/1000/8) 
#define USEC_TO_BYTE_SAMPLES(ms)   (OVERSAMPLE*(ms*BASE_SPEED_HZ/1000)/1000/8) 

static const char* TAG = "tapio";
static spi_device_handle_t tapio_spi;	// SPI used for tapi in and out sampling. a I2S would fit better but this is not available when 2x I2S used for video conversion
static uint8_t* tapio_transmit_buffer[NUM_PARALLEL_TRANSFERS];
static uint8_t* tapio_receive_buffer[NUM_PARALLEL_TRANSFERS];
 

// mechanism cycles through NUM_PARALLEL_TRANSFERS of buffers for seamless activity
static 	uint8_t tapio_num_active_transfers=0;
static 	uint8_t tapio_next_active_transfer_ix = 0;

// registered function to be called when data is received
static 	void (*tapio_receive_callback)(uint8_t*, int) = NULL;

int tapio_wait_and_finish_transfer(){
    spi_transaction_t *rtrans;
	if(tapio_num_active_transfers){
		ESP_ERROR_CHECK(spi_device_get_trans_result(tapio_spi, &rtrans, portMAX_DELAY));
		if(rtrans->rx_buffer){
			if(tapio_receive_callback) (*tapio_receive_callback)(rtrans->rx_buffer,rtrans->rxlength);
		}
		tapio_num_active_transfers--;
	}
	return tapio_num_active_transfers;
}


static void tapio_check_finalize_transfer(){
	if(tapio_num_active_transfers>=NUM_PARALLEL_TRANSFERS-1){
		tapio_wait_and_finish_transfer(tapio_spi);
	}
}

void tapio_process_next_transfer(int transmit_len_bits){
    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    static spi_transaction_t trans[NUM_PARALLEL_TRANSFERS];

	// make 
	tapio_check_finalize_transfer();
	if(transmit_len_bits<=0) transmit_len_bits=TAPIO_MAX_TRANSFER_LEN_BYTES*8;
	//memset(&trans, 0, sizeof(spi_transaction_t));
    //In theory, it's better to initialize trans and data only once and hang on to the initialized variables.
    trans[tapio_next_active_transfer_ix].tx_buffer=tapio_transmit_buffer[tapio_next_active_transfer_ix];        //finally send the signal data
    trans[tapio_next_active_transfer_ix].rx_buffer=tapio_receive_buffer[tapio_next_active_transfer_ix]  ; 
    trans[tapio_next_active_transfer_ix].length=transmit_len_bits;         //Data length, in bits
    trans[tapio_next_active_transfer_ix].rxlength=0;  // is in lib function static, so set new. (0 defaults this to the value of length). 
    trans[tapio_next_active_transfer_ix].flags=0;     //reset  SPI_TRANS_USE_TXDATA and SPI_TRANS_USE_RXDATA  flag

    //Queue transaction.
    ESP_ERROR_CHECK(spi_device_queue_trans(tapio_spi, &trans[tapio_next_active_transfer_ix], portMAX_DELAY));
	tapio_num_active_transfers++;
	// use alternating buffers 
	tapio_next_active_transfer_ix = (tapio_next_active_transfer_ix+1) % NUM_PARALLEL_TRANSFERS;

    //When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
    //mostly using DMA, so the CPU doesn't have much to do here. 
}

void tapio_clear_transmit_buffers(){
	for(int i=0; i<NUM_PARALLEL_TRANSFERS; i++) memset(tapio_transmit_buffer[i], 0, TAPIO_MAX_TRANSFER_LEN_BYTES);
}

uint8_t *tapio_get_next_transmit_buffer(){
	return tapio_transmit_buffer[tapio_next_active_transfer_ix];
}


void tapio_init(tapio_receive_callback_t on_incomming_data){
	esp_err_t ret;
	tapio_receive_callback=on_incomming_data;

	// configure the SPI interface
    spi_bus_config_t buscfg={
        .miso_io_num=TAP_IN_PIN,	// input was -1 on ZX where video input is also tape in
        .mosi_io_num=22,	// output
        .sclk_io_num=-1,	// not connected
        .quadwp_io_num=-1,	// not connected
        .quadhd_io_num=-1,	// not connected
        .max_transfer_sz=TAPIO_MAX_TRANSFER_LEN_BYTES
    };

	// configure the (pseudo) device - we just use MOSI and MISO for sampling
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=OVERSAMPLE*125*1000,    // Clock out at 125khz MHz
        .mode=0,                                // SPI mode 0
        .spics_io_num=-1,               		// CS pin, not connected here
        .queue_size=3,                          // We want to be able to queue several transactions
		.cs_ena_posttrans=0,
		.cs_ena_pretrans=0,
		.flags=SPI_DEVICE_NO_DUMMY /* write only (maybe also faster?) */
        //.pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };


	// just to be sure, make MISOinput w/o pullup/down
    gpio_config_t io_conf;
	// input, turn off all pullup/down
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = 1ULL<<TAP_IN_PIN;
    //disable pull-down mode
    io_conf.pull_down_en = false;
    io_conf.pull_up_en = false;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    // initialize the SPI interface
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 2); // TODO check DMA channel is available, currently assume it is so
    ESP_ERROR_CHECK(ret);
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &tapio_spi);
    ESP_ERROR_CHECK(ret);
	ret=spi_device_acquire_bus(tapio_spi, portMAX_DELAY);  // occupy the SPI bus for a device to do continuous transactions. 
    ESP_ERROR_CHECK(ret);


	// allocate the TX and RX buffers
	for(int i=0;i<NUM_PARALLEL_TRANSFERS;i++){
	    tapio_transmit_buffer[i]=heap_caps_malloc(TAPIO_MAX_TRANSFER_LEN_BYTES, MALLOC_CAP_DMA);
    	if(!tapio_transmit_buffer[i]) printf("calloc of %d failed\n",TAPIO_MAX_TRANSFER_LEN_BYTES);
	    tapio_receive_buffer[i]=heap_caps_malloc(TAPIO_MAX_TRANSFER_LEN_BYTES, MALLOC_CAP_DMA);
    	if(!tapio_receive_buffer[i]) printf("calloc of %d failed\n",TAPIO_MAX_TRANSFER_LEN_BYTES);
	}
}




