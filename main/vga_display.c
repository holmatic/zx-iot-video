
#include <sys/param.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"

#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "driver/periph_ctrl.h"
#include "rom/lldesc.h"
#include "soc/rtc.h"

#include "driver/gpio.h"
#include "iis_videosig.h"
#include "vga_display.h"

static const char *TAG="vga_disp";

#define GPIO_UNUSED 0xff                //!< Flag indicating that CD/WP is unused

#define PIN_NUM_MISO -1
#define PIN_NUM_HSYNC 13  //mosi
#define PIN_NUM_VSYNC  12  //clk
#define PIN_NUM_GREEN   14  //cs


static   bool                m_DMAStarted= false;
static   volatile lldesc_t * m_DMABuffer= NULL;
static   volatile uint8_t *  m_DMAData= NULL;
static intr_handle_data_t * m_isr_handle=NULL;

//FABGLIB_USE_APLL_AB_COEF = 0 (the default) 
#define FABGLIB_USE_APLL_AB_COEF 0
//#define QVGA_320x240_60Hz "\"320x240@60Hz\" 12.6 320 328 376 400 240 245 246 262 -HSync -VSync DoubleScan"

static void configureGPIO(gpio_num_t gpio, gpio_mode_t mode)
{
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
  gpio_set_direction(gpio, mode);
}

static void setupGPIO(gpio_num_t gpio, int bit, gpio_mode_t mode)
{
  if (gpio != GPIO_UNUSED) {
    configureGPIO(gpio, mode);
    gpio_matrix_out(gpio, I2S1O_DATA_OUT0_IDX + bit, false, false);
  }
}


static void init_stream()
{
  m_DMAStarted = false;
  //if (div1_onGPIO0) 
  //  setupGPIO(GPIO_NUM_0,  -1, GPIO_MODE_OUTPUT);  // note: GPIO_NUM_0 cannot be changed! NOT accesible
  setupGPIO(PIN_NUM_GREEN,   0, GPIO_MODE_OUTPUT);
  setupGPIO(GPIO_UNUSED,   1, GPIO_MODE_OUTPUT);
  setupGPIO(GPIO_UNUSED,   2, GPIO_MODE_OUTPUT);
  setupGPIO(GPIO_UNUSED,  3, GPIO_MODE_OUTPUT);  // TODO
  setupGPIO(GPIO_UNUSED,  4, GPIO_MODE_OUTPUT);
  setupGPIO(GPIO_UNUSED,  5, GPIO_MODE_OUTPUT);
  setupGPIO(PIN_NUM_HSYNC, 6, GPIO_MODE_OUTPUT);
  setupGPIO(PIN_NUM_VSYNC, 7, GPIO_MODE_OUTPUT);

  m_DMAData = (volatile uint8_t *) heap_caps_malloc(256, MALLOC_CAP_DMA);
  for (int i = 0; i < 256; ++i)
    m_DMAData[i] = i;

  m_DMABuffer = (volatile lldesc_t *) heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);
  m_DMABuffer->eof    = 0;
  m_DMABuffer->sosf   = 0;
  m_DMABuffer->owner  = 1;
  m_DMABuffer->qe.stqe_next = (lldesc_t *) m_DMABuffer;
  m_DMABuffer->offset = 0;
  m_DMABuffer->size   = 256;
  m_DMABuffer->length = 256;
  m_DMABuffer->buf    = (uint8_t*) m_DMAData;
}




typedef struct APLLParams_tag {
  uint8_t sdm0;
  uint8_t sdm1;
  uint8_t sdm2;
  uint8_t o_div;
} 
APLLParams;

#define tmax(a,b)  ((a)>(b)?(a):(b))
#define tmin(a,b)  ((a)<(b)?(a):(b))

   
#define FABGLIB_XTAL 40000000

static void APLLCalcParams(double freq, APLLParams * params, uint8_t * a, uint8_t * b, double * out_freq, double * error)
{
  double FXTAL = FABGLIB_XTAL;

  *error = 999999999;

  double apll_freq = freq * 2;

  for (int o_div = 0; o_div <= 31; ++o_div) {

    int idivisor = (2 * o_div + 4);

    for (int sdm2 = 4; sdm2 <= 8; ++sdm2) {

      // from tables above
      int minSDM1 = (sdm2 == 4 ? 192 : 0);
      int maxSDM1 = (sdm2 == 8 ? 128 : 255);
      // apll_freq = XTAL * (4 + sdm2 + sdm1 / 256) / divisor   ->   sdm1 = (apll_freq * divisor - XTAL * 4 - XTAL * sdm2) * 256 / XTAL
      int startSDM1 = ((apll_freq * idivisor - FXTAL * 4.0 - FXTAL * sdm2) * 256.0 / FXTAL);
#if FABGLIB_USE_APLL_AB_COEF
      for (int isdm1 = tmax(minSDM1, startSDM1); isdm1 <= maxSDM1; ++isdm1) {
#else
      int isdm1 = startSDM1; {
#endif

        int sdm1 = isdm1;
        sdm1 = tmax(minSDM1, sdm1);
        sdm1 = tmin(maxSDM1, sdm1);

        // apll_freq = XTAL * (4 + sdm2 + sdm1 / 256 + sdm0 / 65536) / divisor   ->   sdm0 = (apll_freq * divisor - XTAL * 4 - XTAL * sdm2 - XTAL * sdm1 / 256) * 65536 / XTAL
        int sdm0 = ((apll_freq * idivisor - FXTAL * 4.0 - FXTAL * sdm2 - FXTAL * sdm1 / 256.0) * 65536.0 / FXTAL);
        // from tables above
        sdm0 = (sdm2 == 8 && sdm1 == 128 ? 0 : tmin(255, sdm0));
        sdm0 = tmax(0, sdm0);

        // dividend inside 350-500Mhz?
        double dividend = FXTAL * (4.0 + sdm2 + sdm1 / 256.0 + sdm0 / 65536.0);
        if (dividend >= 350000000 && dividend <= 500000000) {
          // adjust output frequency using "b/a"
          double oapll_freq = dividend / idivisor;

          // Calculates "b/a", assuming tx_bck_div_num = 1 and clkm_div_num = 2:
          //   freq = apll_clk / (2 + clkm_div_b / clkm_div_a)
          //     abr = clkm_div_b / clkm_div_a
          //     freq = apll_clk / (2 + abr)    =>    abr = apll_clk / freq - 2
          uint8_t oa = 1, ob = 0;
#if FABGLIB_USE_APLL_AB_COEF
          double abr = oapll_freq / freq - 2.0;
          if (abr > 0 && abr < 1) {
            int num, den;
            floatToFraction(abr, 63, &num, &den);
            ob = tclamp(num, 0, 63);
            oa = tclamp(den, 0, 63);
          }
#endif

          // is this the best?
          double ofreq = oapll_freq / (2.0 + (double)ob / oa);
          double err = freq - ofreq;
          if (abs(err) < abs(*error)) {
            *params = (APLLParams){(uint8_t)sdm0, (uint8_t)sdm1, (uint8_t)sdm2, (uint8_t)o_div};
            *a = oa;
            *b = ob;
            *out_freq = ofreq;
            *error = err;
            if (err == 0.0)
              return;
          }
        }
      }

    }
  }
}


static void setupClock(int freq)
{
  APLLParams p = {0, 0, 0, 0};
  double error, out_freq;
  uint8_t a = 1, b = 0;
  APLLCalcParams(freq, &p, &a, &b, &out_freq, &error);

  I2S1.clkm_conf.val          = 0;
  I2S1.clkm_conf.clkm_div_b   = b;
  I2S1.clkm_conf.clkm_div_a   = a;
  I2S1.clkm_conf.clkm_div_num = 2;  // not less than 2
  
  I2S1.sample_rate_conf.tx_bck_div_num = 1; // this makes I2S1O_BCK = I2S1_CLK

  rtc_clk_apll_enable(true, p.sdm0, p.sdm1, p.sdm2, p.o_div);

  I2S1.clkm_conf.clka_en = 1;
}

static volatile int int_c_cnt=0;
static int int_fr_cnt=0;

static void IRAM_ATTR interrupt_handler(void * arg){
  int_c_cnt++;
  if (I2S1.int_st.out_eof) {

    const lldesc_t* desc = (lldesc_t*) I2S1.out_eof_des_addr;
    int_c_cnt++;
    //if (desc == s_frameResetDesc)
    //  s_scanLine = 0;
  }
  I2S1.int_clr.val = I2S1.int_st.val;
}



typedef struct esp_intr_alloc_args {
  int             source;
  int             flags;
  intr_handler_t  handler;
  void *          arg;
  intr_handle_t * ret_handle;
  TaskHandle_t    waitingTask;
} esp_intr_alloc_args;

static void esp_intr_alloc_pinnedToCore_task(void * arg)
{
  esp_intr_alloc_args* args = (esp_intr_alloc_args*) arg;
  esp_intr_alloc(args->source, args->flags, args->handler, args->arg, args->ret_handle);
  ESP_LOGI(TAG, "INT ALLOC DOEN ...");  
  xTaskNotifyGive(args->waitingTask);
  vTaskDelete(NULL);
}

static void esp_intr_alloc_pinnedToCore(int source, int flags, intr_handler_t handler, void * arg, intr_handle_t * ret_handle, int core)
{
  esp_intr_alloc_args args = { source, flags, handler, arg, ret_handle, xTaskGetCurrentTaskHandle() };
  xTaskCreatePinnedToCore(esp_intr_alloc_pinnedToCore_task, "" , 1024*3, &args, 3, NULL, core);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}



static void play_stream(int freq, lldesc_t volatile * dmaBuffers)
{
  if (!m_DMAStarted) {
    ESP_LOGI(TAG, "PLAY STREAM ...");
    // Power on device
    periph_module_enable(PERIPH_I2S1_MODULE);

    // Initialize I2S device
    I2S1.conf.tx_reset = 1;
    I2S1.conf.tx_reset = 0;

    // Reset DMA
    I2S1.lc_conf.out_rst = 1;
    I2S1.lc_conf.out_rst = 0;

    // Reset FIFO
    I2S1.conf.tx_fifo_reset = 1;
    I2S1.conf.tx_fifo_reset = 0;

    // LCD mode
    I2S1.conf2.val            = 0;
    I2S1.conf2.lcd_en         = 1;
    I2S1.conf2.lcd_tx_wrx2_en = 1;
    I2S1.conf2.lcd_tx_sdx2_en = 0;

    I2S1.sample_rate_conf.val         = 0;
    I2S1.sample_rate_conf.tx_bits_mod = 8;

    setupClock(freq);

    I2S1.fifo_conf.val                  = 0;
    I2S1.fifo_conf.tx_fifo_mod_force_en = 1;
    I2S1.fifo_conf.tx_fifo_mod          = 1;
    I2S1.fifo_conf.tx_fifo_mod          = 1;
    I2S1.fifo_conf.tx_data_num          = 32;
    I2S1.fifo_conf.dscr_en              = 1;

    I2S1.conf1.val           = 0;
    I2S1.conf1.tx_stop_en    = 0;
    I2S1.conf1.tx_pcm_bypass = 1;

    I2S1.conf_chan.val         = 0;
    I2S1.conf_chan.tx_chan_mod = 1;

    I2S1.conf.tx_right_first = 1;

    I2S1.timing.val = 0;

    // Reset AHB interface of DMA
    I2S1.lc_conf.ahbm_rst      = 1;
    I2S1.lc_conf.ahbm_fifo_rst = 1;
    I2S1.lc_conf.ahbm_rst      = 0;
    I2S1.lc_conf.ahbm_fifo_rst = 0;

    // Start DMA
    I2S1.lc_conf.val    = I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN;
    I2S1.out_link.addr  = (uint32_t) (dmaBuffers ? &dmaBuffers[0] : m_DMABuffer);
    I2S1.out_link.start = 1;
    I2S1.conf.tx_start  = 1;

    m_DMAStarted = true;
    ESP_LOGI(TAG, "PLAY STREAM started.");

    if (m_isr_handle == NULL) {
      //esp_intr_alloc_pinnedToCore(ETS_I2S1_INTR_SOURCE, ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM, interrupt_handler, NULL, &m_isr_handle, WIFI_TASK_CORE_ID ^ 1);

      ESP_ERROR_CHECK(esp_intr_alloc(ETS_I2S1_INTR_SOURCE, ESP_INTR_FLAG_IRAM, &interrupt_handler, 12, &m_isr_handle));

      I2S1.int_clr.val     = 0xFFFFFFFF;
      I2S1.int_ena.out_eof = 1;
      ESP_LOGI(TAG, "Int enabled.");

      //esp_intr_enable(m_isr_handle);
      //esp_intr_enable(&m_isr_handle);
    }

  }
}

//	25.2 640 656 752 800 480 490 492 525 with half-rate horizontally

#define SCR_NUM_LINES 525
#define SCR_NUM_VISIBLE_LINES 240
#define SCR_LINE_VSYNC_START 490
#define SCR_LINE_VSYNC_END 492
#define SCR_BYTES_LINE 400
#define SCR_BYTES_HSYNC 48
#define SCR_BYTES_PORCH 24   // 
#define SCR_BYTES_VISIBLE 320  // 8 remining after



#define WHITE_MASK 0x3F

#define HSYNC_MASK 0x40
#define VSYNC_MASK 0x80

static uint8_t *framebuf=NULL;
static volatile lldesc_t *dma_ll=NULL;


static void alloc_scrbuf(){
    if(!dma_ll) dma_ll = (volatile lldesc_t *) heap_caps_malloc(sizeof(lldesc_t)*SCR_NUM_LINES, MALLOC_CAP_DMA);
    if(!framebuf) framebuf = (uint8_t *) heap_caps_malloc(SCR_BYTES_LINE*(SCR_NUM_VISIBLE_LINES+2), MALLOC_CAP_DMA);
    // allocating too much RAM here kills the WLAN server (..?)

    for(int line=0;line<SCR_NUM_LINES;line++){
        volatile lldesc_t * buf=&dma_ll[line];
        buf->eof    = line==400? 1:0;
        buf->sosf   = 0;
        buf->owner  = 1;
        buf->qe.stqe_next = &dma_ll[ (line+1)%SCR_NUM_LINES ]; // wrap-around
        buf->offset = 0;
        buf->size   = SCR_BYTES_LINE;
        buf->length = SCR_BYTES_LINE;
        int mem_line_ix=line/2;
        if(line>=2*SCR_NUM_VISIBLE_LINES) mem_line_ix = SCR_NUM_VISIBLE_LINES;            // hsync only 
        if(line>=SCR_LINE_VSYNC_START &&  line<SCR_LINE_VSYNC_END) mem_line_ix = SCR_NUM_VISIBLE_LINES+1;  // vsync
        buf->buf    = &framebuf[mem_line_ix*SCR_BYTES_LINE];
    }
    for(int i=0;i<SCR_NUM_VISIBLE_LINES+2;i++){
      for(int x=0;x<SCR_BYTES_LINE;x++){
        uint8_t v=HSYNC_MASK|VSYNC_MASK;
        if(x>=SCR_BYTES_HSYNC+SCR_BYTES_PORCH && x<SCR_BYTES_HSYNC+SCR_BYTES_PORCH+SCR_BYTES_VISIBLE && i<SCR_NUM_VISIBLE_LINES) v |= (WHITE_MASK&((x*x+i*i)/8));
        if(x<SCR_BYTES_HSYNC)v&=~HSYNC_MASK;
        if(i==SCR_NUM_VISIBLE_LINES+1) v&=~VSYNC_MASK;
        framebuf[ (i*SCR_BYTES_LINE+x)^2  ]=v;    // ^2 as half words are strangely swapped...
      }
    }


}



static void stop_stream()
{
  if (m_DMAStarted) {
    rtc_clk_apll_enable(false, 0, 0, 0, 0);
    periph_module_disable(PERIPH_I2S1_MODULE);
    m_DMAStarted = false;
  }
}




static void vga_task(void*arg)
{

    esp_err_t ret;

    vTaskDelay(1000 / portTICK_RATE_MS); // allow some startup and settling time (might help, not proven)
    ESP_LOGI(TAG, "VGA-Alloc ...");        
    alloc_scrbuf();
    init_stream();
    play_stream(12500000,dma_ll);

    while(0){
        vTaskDelay(10); // allow some startup and settling time (might help, not proven)
    }

    int frames=0;
    while(1){
        //ESP_LOGI(TAG, "hostspi_task ...");        

        vTaskDelay(10); // allow some startup and settling time (might help, not proven)


        for(int l=0;l<SCR_NUM_VISIBLE_LINES;l++){
          for(int x=0;x<SCR_BYTES_VISIBLE;x++){
            uint8_t v=HSYNC_MASK|VSYNC_MASK;
            if(x<100 || x>250 || l<50 || l>150)
              v |= (WHITE_MASK&((x*x+l*l+frames)/16));
            else if (l>97)
              v |= WHITE_MASK;
            framebuf[ (l*SCR_BYTES_LINE+x+SCR_BYTES_HSYNC+SCR_BYTES_PORCH)^2  ]=v;    // ^2 as half words are strangely swapped...
          }
        }
        frames++;
        if(frames%100==10){
          vTaskDelay(2000);
          ESP_LOGI(TAG, "VGA intcnt %d",int_c_cnt);        
        }
    }
}


/* Function to initialize Wi-Fi at station */
void vga_disp_init(void)
{


    //xTaskCreate(vga_task, "vga_task", 1024 * 4, NULL, 12, NULL);
    xTaskCreatePinnedToCore(vga_task, "vga_task" , 1024*3, NULL, 12, NULL, WIFI_TASK_CORE_ID ^ 1);

}



