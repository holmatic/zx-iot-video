
#include <sys/param.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "iis_videosig.h"
#include "led_matrix.h"

static const char *TAG="led_mtx";


#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   14

static spi_device_handle_t spi;


// void transfer_byte_generic( uint8_t data)
// {
//     static spi_transaction_t trans;
//     //memset(&trans, 0, sizeof(spi_transaction_t));

//     trans.tx_data[0]= data; /* MSB */
//     trans.length=8;          //Data length, in bits
//     trans.flags=SPI_TRANS_USE_TXDATA;

//     //ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
//     ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &trans));
// }



static void send_buffer(spi_device_handle_t spi, uint8_t *buf, int len)
{
    esp_err_t ret;

    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
    static spi_transaction_t trans;

    //In theory, it's better to initialize trans and data only once and hang on to the initialized
    //variables. We allocate them on the stack, so we need to re-init them each call.
    trans.tx_buffer=buf;        //finally send the line data
    trans.rx_buffer=NULL;
    trans.length=len;          //Data length, in bits
    trans.rxlength=0;  // is overitten on static, so set new 
    trans.flags=0; //undo SPI_TRANS_USE_TXDATA flag

    //Queue all transactions.
    ret=spi_device_queue_trans(spi, &trans, portMAX_DELAY);
    assert(ret==ESP_OK);

    //When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
    //mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
    //finish because we may as well spend the time calculating the next line. When that is done, we can call
    //send_line_finish, which will wait for the transfers to be done and check their status.
}


static void send_buffer_finish(spi_device_handle_t spi)
{
    spi_transaction_t *rtrans;
    esp_err_t ret;
    ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
    assert(ret==ESP_OK);
}

#define DATASIZE (68*64+200)

/* 
FM6126A

Register 1
11111111 11001110 default
|||||||| ||||||||- Low Gray Compensation Bit 0 (0-7, default 4) (default 0)
|||||||| |||||||-- Output enable 1=On, 0=Off (default 1)
|||||||| ||||||--- Intensity Bit 0 (15-63, default 63) (default 1)
|||||||| |||||---- Intensity Bit 1 (15-63, default 63) (default 1)
|||||||| ||||----- Inflection Point Bit 0 (0-7, default 4) (default 0)
|||||||| |||------ Inflection Point Bit 1 (0-7, default 4) (default 0)
|||||||| ||------- Inflection Point Bit 2 (0-7, default 4 (FM6126=6)) (default 1)
|||||||| |-------- Intensity Bit 2 (15-63, default 63) (default 1)

||||||||---------- Intensity Bit 3 (15-63, default 63) (default 1)
|||||||----------- Intensity Bit 4 (15-63, default 63) (default 1)
||||||------------ Intensity Bit 5 (15-63, default 63) (default 1)
|||||------------- Lower Blanking Level #1 Bit 0 (0-15, default 15) (default 1)
||||-------------- Lower Blanking Level #1 Bit 1 (0-15, default 15) (default 1)
|||--------------- Lower Blanking Level #1 Bit 2 (0-15, default 15) (default 1)
||---------------- Lower Blanking Level #1 Bit 3 (0-15, default 15) (default 1)
|----------------- First Line of Dark Compensation Bit 4 (0-15, default 8) (default 1)

Register 2
11111000 01100010 default red
11110000 01100010 default green
11101000 01100010 default blue
|||||||| ||||||||- Low Gray Compensation Bit 1 (0-7, default 4) (default 0)
|||||||| |||||||-- Low Gray Compensation Bit 2 (0-7, default 4) (default 1)
|||||||| ||||||--- SDO Output delay 1=On, 0=Off (Default 0)
|||||||| |||||---- Lower Blanking Level #2 (0-1, default 0)
|||||||| ||||----- Ghosting Enhancement (0=off*, 1=on)
|||||||| |||------ Always 1
|||||||| ||------- LE Data latch 1=On, 0=Off (Default 1)
|||||||| |-------- Always 0

||||||||---------- First Line of Dark Compensation Bit 0 (0-15, default 8) (default 0)
|||||||----------- First Line of Dark Compensation Bit 1 (0-15, default 8) (default 0)
||||||------------ First Line of Dark Compensation Bit 2 (0-15, default 8) (default 0)
|||||------------- OE Delay Bit 0
||||-------------- OE Delay Bit 1 (0-3, default red=3, green=2, blue=1)
|||--------------- Always 1
||---------------- Always 1
|----------------- Always 1

Register 3 (FM6127 only)
00011111 00000000  default
|||||||| ||||||||- Always 0
|||||||| |||||||-- Always 0
|||||||| ||||||--- Always 0
|||||||| |||||---- Always 0
|||||||| ||||----- Always 0
|||||||| |||------ Always 0
|||||||| ||------- Always 0
|||||||| |-------- Always 0

||||||||---------- Always 1
|||||||----------- Always 1
||||||------------ Always 1
|||||------------- Always 1
||||-------------- Always 1
|||--------------- Always 0
||---------------- Bad Pixel Elimination 1=On 0=Off*
|----------------- Always 0

*/

static void ledmx_task(void*arg)
{

    esp_err_t ret;

    spi_bus_config_t buscfg={
        .miso_io_num=PIN_NUM_MISO,
        .mosi_io_num=PIN_NUM_MOSI,
        .sclk_io_num=PIN_NUM_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        //.max_transfer_sz=DATASIZE  // defaults to 4094
        .max_transfer_sz=DATASIZE 
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=12*1000*1000,           //Clock out at 10 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=PIN_NUM_CS,               //CS pin
        .queue_size=3,                          //We want to be able to queue 7 transactions at a time
        //.pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 2); // TODO check DMA channel #
    ESP_ERROR_CHECK(ret);
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    uint8_t* buffer=heap_caps_malloc(DATASIZE, MALLOC_CAP_DMA);

    int frame=0;
    int active=0;
    while(1){
        //ESP_LOGI(TAG, "hostspi_task ...");        
        uint8_t *wrpt;
        uint32_t vl_offset=vid_get_vline_offset();
        wrpt=buffer;

        for(int y=0;y<64;y++){
            if(y==0 && (frame&0xfffffc)==0  ){
                /* Init Register 1 (11 pulses), 11111111 11001110 default */

                if(0)for(int c=0;c<4;c++){
                    for(int b=15;b<=0;b--){
                        uint8_t strobe = ( (c==3 && b<11)  ? 0x40 : 0);
                        uint8_t dat =  (0xffce & (1 << b)) ? 0x3f:0  ;
                        *wrpt++= dat|strobe;
                    }
                }
                //*wrpt++=0x3f;
                //for(int c=0;c<11-4+((frame/100)&7);c++) *wrpt++=0x40;
                //*wrpt++=0x3f;
                
                /* Init Register 2 (12 pulses), 11110000 01100010 default green */

                if(0)for(int c=0;c<4;c++){
                    for(int b=15;b<=0;b--){
                        uint8_t strobe = ( (c==3 && b<12)  ? 0x40 : 0); // we seem to need one pulse more...
                        uint8_t dat =  (0xf062 & (1 << b)) ? 0x3f:0  ;
                        *wrpt++= dat|strobe;
                    }
                }
                //*wrpt++=0x3f;
                for(int c=0;c<64 ;c++) *wrpt++=0x3f;//0x3f;
  //              for(int c=0;c<11+( (frame)&1);c++) *wrpt++=0x7f;
                for(int c=0;c<12;c++) *wrpt++=0x7f;
            }

            uint8_t* vmem=(uint8_t*)vid_pixel_mem;
            for(int x=0;x<64;x++){
                uint8_t data=0;
                uint8_t mask=0x87;
                if( (y-1) & 16){ data|=0x80;}
                if(y&32){ mask=0xb8; }
                if(x<1||x>59){ mask &= ~0x12; }
                if(1)
                {
                    if(y>=0 && y<48){
                        uint8_t v=vmem[( (vl_offset+2)*40 + y*160 +4+x/2)^3];
                        if( (x&1)==0 )  v>>=4;
                        v &= 0xf;
                        if(v==0xf) data|=0x3F;
                        else if(v==0) data|=0;
                        else if(v==0xa||v==0x5) data|=9;
                        else data|=0x24;
                    }
                    else if(y<56)
                    {
                        uint8_t v=(vmem[(vl_offset*40+184*40+(y-48)*40+4+x/8)^3] << (x&7) ) & 0x80;
                        data|=v?9:0;
                    }
                    else if(y<64)
                    {
                        uint8_t v=(vmem[(vl_offset*40+184*40+(y-56)*40+12+x/8)^3] << (x&7) ) & 0x80;
                        data|=v?9:0;
                    }

                }else{
                    //if(x>1 && x<69) data+=(x+y + ((frame/16)&63)  )&0x3F;
                    if(x>=0 && x<64) data |=  ( ( (x*x+y*y)/32 + ((frame/2)&4095)  ) &0x0c0 ) ? 0:9 ; // 0x12 green causes row clr
                    if(x>=0 && x<64) data |=  ( ( ( (80-x)*(80-x)+(70-y)*(70-y) ) /64 + ((frame/3)&4095)  ) &0x0c0 ) ? 0:0x12 ; // 0x12 green causes row clr
                    if(x>=0 && x<64) data |=  ( ( ( (25-x)*(25-x)+(30-y)*(30-y) ) /64 + ((frame/2)&4095)  ) &0x0c0 ) ? 0:0x24 ; // 0x12 green causes row clr
                    //if(x>20 && x<28) data= 10  ;
                    if(frame<2500){
                        if(x==62||x==1) data|=0x24;
                        if(y==1) data|=0x024;
                        if(y==62) data|=0x24;
                    }
                }

                data&=mask;
                if(x>=60 && x<64) data|=0x40;    // Latch ena - should just be 3, but does not work
                if(x==61 && (y==0||y==32)  )   data|=0x02;    // CLR
               // if(x>=75 && x<76) data=0x40;    // Latch ena 
                //*wrpt++=data;
                *wrpt++=data;
            }
            //*wrpt++=0;  /* needed to remove artefacts*/
            //*wrpt++=0;
            //*wrpt++=0;
            //*wrpt++=0;
        }
        // show last line, then lights off & OE Off at end
        for(int c=0;c<60;c++) *wrpt++=0x80;
        for(int c=0;c<4;c++) *wrpt++=0xc0; // CLR

        for(int c=0;c<60;c++) *wrpt++=0x00;
        for(int c=0;c<4;c++) *wrpt++=0x40; // CLR

        if(active>=2){
            send_buffer_finish(spi);
            active--;
        }

        send_buffer(spi, buffer, (wrpt-buffer)*8);
        active++;
        //ESP_LOGI(TAG, "send %d bytes", wrpt-buffer); 

        vTaskDelay(2); // allow some startup and settling time (might help, not proven)
        frame++;
    }
}


/* Function to initialize Wi-Fi at station */
void ledmx_init(void)
{


    xTaskCreate(ledmx_task, "ledmx_task", 1024 * 3, NULL, 14, NULL);

}



