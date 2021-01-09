
#include <sys/param.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "driver/periph_ctrl.h"
#include "rom/lldesc.h"
#include "soc/rtc.h"

#include "driver/gpio.h"
#include "iis_videosig.h"
#include "video_attr.h"
#include "vga_display.h"

static const char *TAG="vga_disp";

#define GPIO_UNUSED 0xff                //!< Flag indicating that CD/WP is unused

#define PIN_NUM_MISO -1


#define PIN_NUM_RED  13  
#define PIN_NUM_GREEN  12  //clk
#define PIN_NUM_BLUE  14  

#define PIN_NUM_HSYNC 27
#define PIN_NUM_VSYNC 26


//#pragma GCC optimize ("O3")   // "O3" is 49us versus 51us at standard/current default "Os"

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
  setupGPIO(GPIO_UNUSED,   0, GPIO_MODE_OUTPUT);
  setupGPIO(PIN_NUM_BLUE,   1, GPIO_MODE_OUTPUT);
  setupGPIO(PIN_NUM_GREEN,   2, GPIO_MODE_OUTPUT);
  setupGPIO(PIN_NUM_RED,  3, GPIO_MODE_OUTPUT);
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

static void IRAM_ATTR interrupt_handler(void * arg); // fwd declare




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

      ESP_ERROR_CHECK(esp_intr_alloc(ETS_I2S1_INTR_SOURCE,  ESP_INTR_FLAG_LEVEL1 |ESP_INTR_FLAG_IRAM, interrupt_handler, NULL, &m_isr_handle));

      I2S1.int_clr.val     = 0xFFFFFFFF;
      I2S1.int_ena.out_eof = 1;
      ESP_LOGI(TAG, "Int enabled.");

    }

  }
}

//	25.2 640 656 752 800 480 490 492 525 with half-rate horizontally



#define SCR_CHUNK_LINES 5

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
static uint8_t *attr_mem_fg=NULL;
static uint8_t *attr_mem_bg=NULL;


// this is with colours and needs 51us
static inline void IRAM_ATTR src_write_line_payload(uint8_t* dst, int logic_line_nr){
  uint8_t fg,bg;
	int ix=10*logic_line_nr;
	int attr_ix=40*(logic_line_nr/8);
  dst+=SCR_BYTES_HSYNC+SCR_BYTES_PORCH;

  uint32_t mask0=0x20000000;  // int16-values are swapped!
  uint32_t mask1=0x10000000;  
  uint32_t mask2=0x80000000;  
  uint32_t mask3=0x40000000;  
	for (int xw=0; xw<10; xw++) {
		uint32_t pm=vid_pixel_mem[ix++];
    for (int c=0; c<4; c++) {
      fg=attr_mem_fg[attr_ix];
      bg=attr_mem_bg[attr_ix++];
      *dst++ = (pm & mask0) ? fg : bg;
      *dst++ = (pm & mask1) ? fg : bg;
      *dst++ = (pm & mask2) ? fg : bg;
      *dst++ = (pm & mask3) ? fg : bg;
      pm<<=4;
      *dst++ = (pm & mask0) ? fg : bg;
      *dst++ = (pm & mask1) ? fg : bg;
      *dst++ = (pm & mask2) ? fg : bg;
      *dst++ = (pm & mask3) ? fg : bg;
      pm<<=4;
		}
  }
}



static inline void IRAM_ATTR src_write_line_payload_55us(uint8_t* dst, int logic_line_nr){
  int dstix=SCR_BYTES_HSYNC+SCR_BYTES_PORCH;
	for (int xw=0; xw<10; xw++) {
		int ix=10*logic_line_nr+xw;
		uint32_t m=vid_pixel_mem[ix];
		for (int b=0; b<32; b++) {
			uint8_t val= (m & 0x80000000) ? (HSYNC_MASK|VSYNC_MASK|WHITE_MASK) : (HSYNC_MASK|VSYNC_MASK) ;
      dst[dstix^2]=val;
      dstix++;
			m<<=1;
		}
  }
}






static void IRAM_ATTR src_write_line_payload2(uint8_t* dst, int logic_line_nr){
    dst+=SCR_BYTES_HSYNC+SCR_BYTES_PORCH;
    for(int x=0;x<SCR_BYTES_VISIBLE;x++){
        uint8_t v=HSYNC_MASK|VSYNC_MASK;
        if(x>=logic_line_nr)  v ^= WHITE_MASK;
        dst[x^2]=v;
    }    
}

static void src_write_line_all(uint8_t* dst, int phys_line_nr){
  for(int b=0;b<SCR_BYTES_LINE;b++){
    uint8_t v=HSYNC_MASK|VSYNC_MASK;
    if(b<SCR_BYTES_HSYNC) v&=~HSYNC_MASK;
    if(phys_line_nr>=SCR_LINE_VSYNC_START && phys_line_nr<SCR_LINE_VSYNC_END) v &= ~VSYNC_MASK;
    dst[b^2]=v;
  }
  if(phys_line_nr<SCR_NUM_VISIBLE_LINES*2){
    src_write_line_payload(dst, phys_line_nr/2);
  }
}


static volatile int int_c_cnt=0;
static int int_fr_cnt=0;
int32_t isr_time_us=0;

static void IRAM_ATTR interrupt_handler(void * arg){
  if (I2S1.int_st.out_eof) {
    int64_t start_time = esp_timer_get_time();
    const lldesc_t* desc = (lldesc_t*) I2S1.out_eof_des_addr;
    if (desc == dma_ll){
      int_fr_cnt++;
      int_c_cnt=0;
    } 
    // for 0, we fill buffer 0 with 2, as this is what is next
    uint8_t *b=&framebuf[ ((int_c_cnt&1) ? SCR_BYTES_LINE*SCR_CHUNK_LINES :0 )    ];
    for(int l=0;l<SCR_CHUNK_LINES;l++){
        src_write_line_payload(b, (( ((int_c_cnt+2)*SCR_CHUNK_LINES) % (SCR_NUM_VISIBLE_LINES*2) )+l )/2  );
        b+=SCR_BYTES_LINE;
    }
    int_c_cnt++;
    isr_time_us=esp_timer_get_time()-start_time;
  }
  I2S1.int_clr.val = I2S1.int_st.val;
}


static void alloc_scrbuf(){
    for(int i=0;i<30*40;i++){
      attr_mem_fg[i] = HSYNC_MASK|VSYNC_MASK | (WHITE_MASK & 4) ; // green on black
      attr_mem_bg[i] = HSYNC_MASK|VSYNC_MASK    ;   // all-black
    } 

    if(!dma_ll) dma_ll = (volatile lldesc_t *) heap_caps_malloc(sizeof(lldesc_t)*SCR_NUM_LINES/SCR_CHUNK_LINES, MALLOC_CAP_DMA);
    if(!framebuf) framebuf = (uint8_t *) heap_caps_malloc( SCR_BYTES_LINE*SCR_CHUNK_LINES*(2+2) , MALLOC_CAP_DMA);
    // allocating too much RAM here kills the WLAN server (..?)

    for(int chunk=0;chunk<SCR_NUM_LINES/SCR_CHUNK_LINES;chunk++){
        volatile lldesc_t * buf=&dma_ll[chunk];
        buf->eof    = chunk < SCR_NUM_VISIBLE_LINES*2/SCR_CHUNK_LINES ? 1:0;
        buf->sosf   = 0;
        buf->owner  = 1;
        buf->qe.stqe_next = &dma_ll[ (chunk+1)%(SCR_NUM_LINES/SCR_CHUNK_LINES) ]; // wrap-around
        buf->offset = 0;
        buf->size   = SCR_BYTES_LINE*SCR_CHUNK_LINES;
        buf->length = SCR_BYTES_LINE*SCR_CHUNK_LINES;
        int mem_line_ix=0;
        if(chunk < SCR_NUM_VISIBLE_LINES*2/SCR_CHUNK_LINES) mem_line_ix = chunk%2; // rolling buffer        
        else if(chunk == SCR_LINE_VSYNC_START/SCR_CHUNK_LINES) mem_line_ix = 3;    // vsync included
        else mem_line_ix = 2;    // hsync only 
        buf->buf    = &framebuf[mem_line_ix*SCR_BYTES_LINE*SCR_CHUNK_LINES];
    }
    /* init pixel content */
    for(int buf_nr=0;buf_nr<2+2;buf_nr++){
      for(int l=0;l<SCR_CHUNK_LINES;l++){
        uint8_t *b=&framebuf[buf_nr*SCR_BYTES_LINE*SCR_CHUNK_LINES + l*SCR_BYTES_LINE   ];
        if(buf_nr<=1)
          src_write_line_all(b, (l+buf_nr*SCR_CHUNK_LINES)  /2  );
        else if(buf_nr==2) 
          src_write_line_all(b, SCR_NUM_VISIBLE_LINES*2 + l );
        else if(buf_nr==3) 
          src_write_line_all(b, SCR_LINE_VSYNC_START + l );
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


#define VBITMAP_IS_VALID_CHAR_L2 0x01
#define VBITMAP_IS_BLOCK_CHAR_L2 0x04
#define VBITMAP_IS_GREY_CHAR_L2  0x08
#define VBITMAP_IS_INV_CHAR_L2   0x02
#define VBITMAP_IS_BLOCK_CHAR_L6 0x40
#define VBITMAP_IS_GREY_CHAR_L6  0x80

/* For the standard ZX81 charset, every possible bit map pattern (when looking at line 2 or 6 of the character) maps to flags that show if this is a valid char, and possible also if it is graphics etc */


const uint8_t vbitm_flags[256]={0x01,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x45,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x89,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x89,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x45,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x00,0x03,0x00,0x45};

static int avg_empty_cnt=0;
static int avg_invalid_cnt=0;
static int avg_dark_cnt=0;

static bool vmode_nochars=false;
static bool vmode_dark=false;


static void create_fancy_colours3()
{
    uint8_t * pix_mem8=(uint8_t *)vid_pixel_mem;
    int empty_cnt=0;
    int invalid_cnt=0;
    int dark_cnt=0;
    static uint8_t switchmode_holdoff=2;
    uint8_t *curr_attr= vmode_dark ? attr_mem_bg:attr_mem_fg;
    for(int y=0;y<24;y++){
      /* check for overscan */
      if(pix_mem8[ ( (24+2+y*8)*40 + 3)^3 ]) invalid_cnt+=4;
      for(int x=0;x<32;x++){
        uint8_t pattern=pix_mem8[ ( (24+2+y*8)*40 +4+x)^3 ];
        uint8_t fg=HSYNC_MASK|VSYNC_MASK;
        uint8_t flags;
        if(pattern!=0){
          /* upper part not empty */
          flags=vbitm_flags[pattern];
          if(0 == (flags&VBITMAP_IS_VALID_CHAR_L2) ){
            fg|=0x4;  /* empty or normal text */
            invalid_cnt++; /* maybe high-res or so, nothing that fancy colours work with.. */
          } else if ( flags&VBITMAP_IS_BLOCK_CHAR_L2 ){
            fg|=0xe; // blocks-> white
            if(pattern==0xff) dark_cnt++;
          } else if ( flags&VBITMAP_IS_INV_CHAR_L2 ){
            fg|=0xc; // inverse text ->  yellow
            dark_cnt++;
          } else if ( flags&VBITMAP_IS_GREY_CHAR_L6 ){
            fg|=0x6; // chequered-> cyan
          }else{
            fg|=0x4;  /* some normal text */
          }
        }else{
          /* upper part just empty */
          pattern=pix_mem8[ ( (24+6+y*8)*40 +4+x)^3 ];
          if(pattern==0){
              empty_cnt++;
              fg|=0x4;  /* empty or some normal text */
          } else {
            flags=vbitm_flags[pattern];
            if ( flags&VBITMAP_IS_BLOCK_CHAR_L6 ){
              fg|=0xe; // blocks-> white
              if(pattern==0xff) dark_cnt++;
            } else if ( flags&VBITMAP_IS_GREY_CHAR_L6 ){
              fg|=0x6; // chequered-> cyan
            }else{
              fg|=0x4;  /* empty or some normal text */
            }
          } 
        }
        if(!vmode_nochars) curr_attr[(3+y)*40+4+x] = fg; // green on black
      } 
    }
    
    avg_empty_cnt=   (7*avg_empty_cnt + empty_cnt) /8;
    avg_invalid_cnt= (7*avg_invalid_cnt + invalid_cnt) /8;
    avg_dark_cnt=    (7*avg_dark_cnt  + dark_cnt) /8;

    if(switchmode_holdoff) {
      switchmode_holdoff--;
    } else {
      if(vmode_nochars){
        // check if screen looks like regular characters again
        if(avg_invalid_cnt<2 || avg_invalid_cnt*16 < (768-avg_empty_cnt) ){
            vmode_nochars=false;
            switchmode_holdoff=50;
        }
      } else {
        // check if screen looks like HRG or invalid
        if(avg_invalid_cnt > 4 && avg_invalid_cnt*8 > (768-avg_empty_cnt) ){
            vmode_nochars=true;
            switchmode_holdoff=50;
            // remove colour info
            for(int i=0;i<30*40;i++){
              attr_mem_fg[i] = HSYNC_MASK|VSYNC_MASK | (WHITE_MASK) ; // white on black
              attr_mem_bg[i] = HSYNC_MASK|VSYNC_MASK    ;   // all-black
            } 
        }
      }

      if(vmode_dark){
        // check if screen looks normal/bright
        if(avg_dark_cnt<284){
            vmode_dark=false;
            switchmode_holdoff=50;
            // remove colour info
            for(int i=0;i<30*40;i++){
              attr_mem_fg[i] = HSYNC_MASK|VSYNC_MASK | (WHITE_MASK) ; // white on black
              attr_mem_bg[i] = HSYNC_MASK|VSYNC_MASK    ;   // all-black
            } 
        }
      } else {
        // check if screen looks dark by inverse
        if(avg_dark_cnt > 484){
            vmode_dark=true;
            switchmode_holdoff=50;
            for(int i=0;i<30*40;i++){
              attr_mem_fg[i] = HSYNC_MASK|VSYNC_MASK ; // white on black
              attr_mem_bg[i] = HSYNC_MASK|VSYNC_MASK | (WHITE_MASK)   ;
            } 
        }
      }
    }

}




static void create_fancy_colours_conventional()
{
    uint8_t * pix_mem8=(uint8_t *)vid_pixel_mem;
    for(int y=0;y<24;y++){
      for(int x=0;x<32;x++){
        uint8_t pattern=pix_mem8[ ( (24+2+y*8)*40 +4+x)^3 ];
        uint8_t fg=HSYNC_MASK|VSYNC_MASK;
        if(pattern==0x0f || pattern==0xf0 || pattern==0xff) fg|=0xe; // blocks-> white
        else if( (pattern&0x81) == 0x81) fg|=0xc; // inverse text ->  yellow
        else if( (pattern&0x81) == 0x81) fg|=0xc; // inverse text ->  yellow
        else {
          pattern=pix_mem8[ ( (24+6+y*8)*40 +4+x)^3 ];
          if(pattern==0x0f || pattern==0xf0 || pattern==0xff) fg|=0xe; // blocks-> white
          else if(pattern==0x55 || pattern==0xaa) fg|=0x6; // chequered-> cyan
          else fg|=0x4;   // normal text etc-> green
        }
        attr_mem_fg[(3+y)*40+4+x] = fg; // green on black
      } 
    }


}



static void vga_task(void*arg)
{

    esp_err_t ret;

    ESP_LOGI(TAG, "VGA-Alloc ...");        
    vidattr_get_mem(&attr_mem_fg, &attr_mem_bg);
    alloc_scrbuf();
    init_stream();
    play_stream(12600000,dma_ll);

    int frames=0;
    while(1){
        //ESP_LOGI(TAG, "hostspi_task ...");        
        vTaskDelay(1); // allow some startup and settling time (might help, not proven)
        frames++;
        //create_fancy_colours();
        if(frames%1000==10){
          ESP_LOGI(TAG, "VGA intcnt %d %d, duration %d us (%d%%)",int_c_cnt,int_fr_cnt,isr_time_us, isr_time_us/SCR_CHUNK_LINES*100/32 );        
          ESP_LOGI(TAG, "Avg - empty %d, dark %d, invalid %d",avg_empty_cnt,avg_dark_cnt,avg_invalid_cnt );        
          ESP_LOGI(TAG, "Vmode dark %d, nochar %d",vmode_dark?1:0,vmode_nochars?1:0);        
        }
    }
}


/* Function to initialize Wi-Fi at station */
void vga_disp_init(void)
{


    //xTaskCreate(vga_task, "vga_task", 1024 * 4, NULL, 12, NULL);
    xTaskCreatePinnedToCore(vga_task, "vga_task" , 1024*3, NULL, 2, NULL, WIFI_TASK_CORE_ID ^ 1);

}



