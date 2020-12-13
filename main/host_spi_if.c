
#include <sys/param.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "host_spi_if.h"

static const char *TAG="host_spi";


#define PIN_NUM_MISO 25
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  19
#define PIN_NUM_CS   26

static spi_device_handle_t spi;


typedef enum hspi_transfer_t
{
    HSPI_STATUS_ONLY=0,
    HSPI_RESET_QUEUE=0x30,
    HSPI_WRITE=0x60,
    HSPI_TRY_FETCH_READ=0x80,
    HSPI_TRIGGER_READ=0xC0,
    HSPI_NUM_CMD
} hspi_transfer_t;


uint32_t hostspi_transfer_byte_generic(hspi_transfer_t cmd, uint32_t addr, uint8_t* data, bool io_not_mem)
{
    static spi_transaction_t trans;
    static uint8_t laststatus;
    uint8_t request_flags=cmd;
    uint32_t count=0;

    memset(&trans, 0, sizeof(spi_transaction_t));
    if(cmd==HSPI_RESET_QUEUE) {
        laststatus=0; /* forget */
    } else {
        if(io_not_mem) request_flags|=0x10;
        /* check if there is data and we would like to read it - if not clear that flag  */
        if( (laststatus&0xc0) == 0 ) request_flags &= ~HSPI_TRY_FETCH_READ; 
    }

    trans.tx_data[0]= request_flags; /* MSB */
    trans.tx_data[1]= (addr>>8)&0xff; 
    trans.tx_data[2]= addr&0xff; 
    trans.tx_data[3]= ( cmd==HSPI_WRITE && data ) ? *data : 0;
    trans.length=32;          //Data length, in bits
    trans.flags=SPI_TRANS_USE_TXDATA|SPI_TRANS_USE_RXDATA;

    //ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
    ESP_ERROR_CHECK(spi_device_transmit(spi, &trans));
    //ESP_LOGI(TAG, "trans (rq %02x) result %02x %02x %02x %02x ",request_flags ,trans.rx_data[0],trans.rx_data[1],trans.rx_data[2],trans.rx_data[3]);
    laststatus=trans.rx_data[0];
    if(request_flags&0x80){
        ++count;
        if(cmd!=HSPI_WRITE && data) *data=trans.rx_data[3];
        //ESP_LOGI(TAG, " readrq    %02x ",trans.rx_data[3]);
        laststatus&=~0x80;   // have read one word, ignore if that was active
    }
    return count;
}



void hostspi_fifo_reset()
{
    hostspi_transfer_byte_generic(HSPI_RESET_QUEUE, 0, NULL,  /*IO*/false);
}

void hostspi_wr_mem_byte(uint32_t mem_addr, uint8_t wrdata)
{
    hostspi_transfer_byte_generic(HSPI_WRITE, mem_addr, &wrdata,  /*IO*/false);
}

void hostspi_wr_io_byte(uint32_t port_addr, uint8_t wrdata)
{
    hostspi_transfer_byte_generic(HSPI_WRITE, port_addr, &wrdata,  /*IO*/true);
}

void hostspi_read_mem_bytes(uint32_t mem_addr, uint8_t* rddata, uint32_t num_bytes )
{
    uint32_t rq_count=0;
    uint32_t in_count=0;
    while(in_count<num_bytes){
        //uint32_t hostspi_transfer_byte_generic(hspi_transfer_t cmd, uint32_t addr, uint8_t* data, bool io_not_mem)
        if (rq_count < num_bytes){
            in_count+=hostspi_transfer_byte_generic(HSPI_TRIGGER_READ, mem_addr+rq_count, &rddata[in_count],  /*IO*/false);
        } else {
            in_count+=hostspi_transfer_byte_generic(HSPI_TRY_FETCH_READ, 0, &rddata[in_count],  /*IO*/false);
        }
        rq_count++;
        if(rq_count>num_bytes+1000){
            /* something is wrong */
            ESP_LOGE(TAG, "hostspi_read_mem_bytes timeout :-( ");
            break;
        }
    } 
}

void hostspi_read_io_bytes(uint32_t port_addr, uint32_t num_bytes)
{
}


static void hostspi_testtrans()
{
   esp_err_t ret;
    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
    static spi_transaction_t trans;
    static uint8_t cnt=0;
    static uint8_t laststatus=0;
    uint8_t loop=0;

    for(loop=0;loop<3;++loop){
        uint8_t request_flags=0;
        memset(&trans, 0, sizeof(spi_transaction_t));
        if(laststatus&0xc0) request_flags=0x80;
        if(cnt&0x08) request_flags|=0x40;
        if(cnt==1) request_flags=0x30; /* reset FIFO */
        trans.tx_data[0]= request_flags; /* MSB */
        trans.tx_data[1]=cnt&1;
        trans.tx_data[2]=0x00;
        trans.tx_data[3]=cnt;
        trans.rx_data[0]=0x00;
        trans.rx_data[1]=0x00;
        trans.rx_data[2]=0x00;
        trans.rx_data[3]=0x00;
        trans.length=32;          //Data length, in bits
        trans.flags=SPI_TRANS_USE_TXDATA|SPI_TRANS_USE_RXDATA;

        //Queue transaction.
        //ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
        ret=spi_device_transmit(spi, &trans);
        ESP_ERROR_CHECK(ret);        
        ESP_LOGI(TAG, "trans (cnt %02x) result %02x %02x %02x %02x ",cnt,trans.rx_data[0],trans.rx_data[1],trans.rx_data[2],trans.rx_data[3]);
        laststatus=trans.rx_data[0];
        if(request_flags&0x80){
            ESP_LOGI(TAG, " readrq    %02x ",trans.rx_data[3]);
            laststatus&=0x7F;   // have read one word, ignore if that was active
        }
        cnt++;
    }
}

#include "../tools/charset_code.c"

static void mem_test()
{

    static uint8_t cnt=0;
    uint8_t indata[4];
    hostspi_wr_mem_byte(0x23,0x33);
    hostspi_wr_mem_byte(0x24,0x33);
    hostspi_wr_mem_byte(0x25,0x33);
    hostspi_wr_mem_byte(0x123,0x42);
    hostspi_wr_mem_byte(0x124,0x43);
    hostspi_wr_mem_byte(0x499,0x99);
    hostspi_wr_mem_byte(0x498,0x98);
    hostspi_wr_mem_byte(0x000,cnt);
    hostspi_wr_mem_byte(0x081,(cnt+1));
    hostspi_wr_mem_byte(31*128+84,cnt);
    hostspi_wr_mem_byte(0x114,cnt*3);
    hostspi_wr_mem_byte(0x110,cnt*5);
    hostspi_wr_mem_byte(0x202,0x02);
    hostspi_wr_mem_byte(0x201,0x01);
    hostspi_wr_mem_byte(0x1011,0x11);


    if(cnt&1){
        hostspi_wr_mem_byte(0x1030,0x0);
        hostspi_wr_mem_byte(0x1031,0x0);
        hostspi_wr_mem_byte(0x1032,0xff);
        hostspi_wr_mem_byte(0x1033,0x01);
        hostspi_wr_mem_byte(0x1034,0x02);
        hostspi_wr_mem_byte(0x1035,0x04);
        hostspi_wr_mem_byte(0x1036,0x08);
        hostspi_wr_mem_byte(0x1037,0x10);
        hostspi_wr_mem_byte(0x1038,0x20);
        hostspi_wr_mem_byte(0x1039,0xff);
        hostspi_wr_mem_byte(0x103a,0x0);
        hostspi_wr_mem_byte(0x103b,0x0);
    } else {
        hostspi_wr_mem_byte(0x1030,0x3F);
        hostspi_wr_mem_byte(0x1031,0x21);
        hostspi_wr_mem_byte(0x1032,0x21);
        hostspi_wr_mem_byte(0x1033,0x21);
        hostspi_wr_mem_byte(0x1034,0x21);
        hostspi_wr_mem_byte(0x1035,0x21);
        hostspi_wr_mem_byte(0x1036,0x21);
        hostspi_wr_mem_byte(0x1037,0x21);
        hostspi_wr_mem_byte(0x1038,0x21);
        hostspi_wr_mem_byte(0x1039,0x21);
        hostspi_wr_mem_byte(0x103a,0x21);
        hostspi_wr_mem_byte(0x103b,0x3F);
    }

    hostspi_wr_mem_byte(0x1101,0x11);
    hostspi_wr_mem_byte(0x1101,0x11);


    hostspi_wr_mem_byte(0x203,0x03);
    hostspi_wr_mem_byte(0x200,0x00);
    hostspi_wr_mem_byte(0x198,0x98);
    hostspi_read_mem_bytes(0x122,&indata[0],4);
    ESP_LOGI(TAG, "RD result A %02x %02x %02x %02x ",indata[0],indata[1],indata[2],indata[3]);
    hostspi_read_mem_bytes(0x200,&indata[0],4);
    ESP_LOGI(TAG, "RD result B %02x %02x %02x %02x ",indata[0],indata[1],indata[2],indata[3]);
    hostspi_read_mem_bytes(0x1200,&indata[0],4);
    ESP_LOGI(TAG, "RD result C %02x %02x %02x %02x ",indata[0],indata[1],indata[2],indata[3]);
    cnt++;
}

static void hostspi_task(void*arg)
{
    hostspi_fifo_reset();

    for(int i=0;i<2048;i++)
        hostspi_wr_mem_byte(i,0x00);
    hostspi_wr_mem_byte(0x1012,0x22);

    for(int i=0;i<sizeof(charset16x6);i++)
        hostspi_wr_mem_byte(i+0x1000,charset16x6[i]);

    for(int i=0;i<64;i++){
        hostspi_wr_mem_byte(11*128+8+i,0x20+i);
        hostspi_wr_mem_byte(12*128+8+i,0x40+i);
    }

    hostspi_wr_mem_byte(0x1012,0x22);


    //hostspi_testtrans();
    while(1){
        //ESP_LOGI(TAG, "hostspi_task ...");        
        mem_test();
    	vTaskDelay(4000 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
    }
}


/* Function to initialize Wi-Fi at station */
void host_spi_init(void)
{
    esp_err_t ret;

    spi_bus_config_t buscfg={
        .miso_io_num=PIN_NUM_MISO,
        .mosi_io_num=PIN_NUM_MOSI,
        .sclk_io_num=PIN_NUM_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=16
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=10*1000*1000,           //Clock out at 10 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=PIN_NUM_CS,               //CS pin
        .queue_size=7,                          //We want to be able to queue 7 transactions at a time
        //.pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 0);
    ESP_ERROR_CHECK(ret);
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    xTaskCreate(hostspi_task, "hostspi_task", 1024 * 3, NULL, 9, NULL);

}



