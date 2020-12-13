#include <stdio.h>
#include <string.h>
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "esp_adc_cal.h"

#include "lcd_display.h"


static const char* TAG = "tft_disp";

#define TFTDISP_CS       (4)
#define TFTDISP_DC       (5)
#define TFTDISP_CLK      (15)
#define TFTDISP_SDI      (18)
#define TFTDISP_RESET    (21)

static void tftd_task(void*arg);
 
#define USE_HW_SPI 1

/**
 * @brief Init interface to display unit
 */
void lcd_disp_init()
{

	#if USE_HW_SPI
	esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num=-1,
        .mosi_io_num=TFTDISP_SDI,
        .sclk_io_num=TFTDISP_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=16
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=10*1000*1000,           //Clock out at 10 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=TFTDISP_CS,               //CS pin
        .queue_size=7,                          //We want to be able to queue 7 transactions at a time
        //.pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret=spi_bus_initialize(VSPI_HOST, &buscfg, 0);
    ESP_ERROR_CHECK(ret);
    ret=spi_bus_add_device(VSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
	#endif



	// pin definitions
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask =
	 	  1ULL << TFTDISP_CS
		| 1ULL << TFTDISP_DC
		| 1ULL << TFTDISP_CLK
		| 1ULL << TFTDISP_SDI
		| 1ULL << TFTDISP_RESET
	    ;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    xTaskCreate(tftd_task, "tftd_task", 1024 * 3, NULL, 9, NULL);

}
#define USE_HORIZONTAL  	0 //定义液晶屏顺时针旋转方向 	0-0度旋转，1-90度旋转，2-180度旋转，3-270度旋转
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////	  
//定义LCD的尺寸
#define LCD_W 240
#define LCD_H 320

//sbit LCD_RS = P1^2;  		 //数据/命令切换
//sbit LCD_SDI = P1^5;		  //SPI写
//sbit LCD_SDO = P1^6;		     //SPI读
//sbit LCD_CS = P1^3;		//片选	
//sbit LCD_CLK = P1^7;   //SPI时钟
//sbit LCD_RESET = P3^3;	      //复位 
//sbit LCD_BL=P3^2;		//背光控制，STC89C52RC单片滑接3.3V

 
typedef struct  
{										    
	uint16_t width;			//LCD 宽度
	uint16_t height;			//LCD 高度
	uint16_t id;				//LCD ID
	uint8_t  dir;			//横屏还是竖屏控制：0，竖屏；1，横屏。	
	uint16_t	 wramcmd;		//开始写gram指令
	uint16_t  setxcmd;		//设置x坐标指令
	uint16_t  setycmd;		//设置y坐标指令	 
}_lcd_dev; 	

//LCD参数
extern _lcd_dev lcddev;	//管理LCD重要参数

void LCD_Init(void); 
void LCD_Clear(uint16_t Color);
void spi_write_byte(uint8_t d); //通过SPI写入一个字节数据
void LCD_WR_DATA(uint8_t Data); 
void LCD_WR_REG(uint8_t Reg);
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos);//设置光标位置
void LCD_SetWindows(uint16_t xStar, uint16_t yStar,uint16_t xEnd,uint16_t yEnd);//设置显示窗口
void LCD_DrawPoint(uint16_t x,uint16_t y);//画点
void LCD_WriteRAM_Prepare(void);
void LCD_direction(uint8_t direction );
void LCD_WR_DATA_16Bit(uint16_t Data);


//
#define WHITE         	 0xFFFF
#define BLACK         	 0x0000	  
#define BLUE         	 0x001F  
#define BRED             0XF81F
#define GRED 			 0XFFE0
#define GBLUE			 0X07FF
#define RED           	 0xF800
#define MAGENTA       	 0xF81F
#define GREEN         	 0x07E0
#define CYAN          	 0x7FFF
#define YELLOW        	 0xFFE0
#define BROWN 			 0XBC40 
#define BRRED 			 0XFC07 
#define GRAY  			 0X8430 

#define DARKBLUE      	 0X01CF	
#define LIGHTBLUE      	 0X7D7C	//浅蓝色  
#define GRAYBLUE       	 0X5458 //灰蓝色
//以上三色为PANEL的颜色 
 
#define LIGHTGREEN     	 0X841F //浅绿色
#define LGRAY 			 0XC618 //浅灰色(PANNEL),窗体背景色

#define LGRAYBLUE        0XA651 //浅灰蓝色(中间层颜色)
#define LBBLUE           0X2B12 //浅棕蓝色(选择条目的反色)



//LCD的画笔颜色和背景色	   
uint16_t POINT_COLOR=GREEN;	//画笔颜色
uint16_t BACK_COLOR=DARKBLUE;  //背景色 
//管理LCD重要参数
//默认为竖屏
_lcd_dev lcddev;

/*****************************************************************************
 * @name       :void spi_write_byte(uint8_t d)
 * @date       :2018-08-09 
 * @function   :Write a byte of data using C51's Software SPI
 * @parameters :d:Data to be written
 * @retvalue   :None
******************************************************************************/
void spi_write_byte(uint8_t d)
{
	uint8_t val=0x80;
	while(val)
	{
		if(d&val)
		{
			gpio_set_level(TFTDISP_SDI, 1);//LCD_SDI = 1;
		}
		else
		{
			gpio_set_level(TFTDISP_SDI, 0);	// LCD_SDI = 0;
		}
		gpio_set_level(TFTDISP_CLK, 0);	//	LCD_CLK = 0;
		gpio_set_level(TFTDISP_CLK, 1);	// LCD_CLK = 1;
		val>>=1;
	}
	gpio_set_level(TFTDISP_SDI, 0);	// LCD_SDI = 0;
	gpio_set_level(TFTDISP_CLK, 0);	//	LCD_CLK = 0;
}

/*****************************************************************************
 * @name       :void LCD_WR_REG(uint8_t data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit command to the LCD screen
 * @parameters :data:Command value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_REG(uint8_t Reg)	 
{	
	gpio_set_level(TFTDISP_DC, 0);//LCD_RS=0;
	gpio_set_level(TFTDISP_CS, 0);//LCD_CS=0;
	spi_write_byte(Reg);
	gpio_set_level(TFTDISP_CS, 1);//LCD_CS=1;
} 

/*****************************************************************************
 * @name       :void LCD_WR_DATA(uint8_t data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit data to the LCD screen
 * @parameters :data:data value to be written
 * @retvalue   :None
******************************************************************************/
 void LCD_WR_DATA(uint8_t Data)
{
	gpio_set_level(TFTDISP_DC, 1);//LCD_RS=1;
	gpio_set_level(TFTDISP_CS, 0);//LCD_CS=0;
	spi_write_byte(Data);
	gpio_set_level(TFTDISP_CS, 1);//LCD_CS=1;
}

/*****************************************************************************
 * @name       :void LCD_WR_DATA_16Bit(uint16_t Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :Data:Data to be written
 * @retvalue   :None
******************************************************************************/	 
void LCD_WR_DATA_16Bit(uint16_t Data)
{
	gpio_set_level(TFTDISP_DC, 1);//LCD_RS=0;
	gpio_set_level(TFTDISP_CS, 0);//LCD_CS=0;
	spi_write_byte(Data>>8);
	spi_write_byte(Data);
	gpio_set_level(TFTDISP_CS, 1);//LCD_CS=1;
}

/*****************************************************************************
 * @name       :void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue)
 * @date       :2018-08-09 
 * @function   :Write data into registers
 * @parameters :LCD_Reg:Register address
                LCD_RegValue:Data to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WriteReg(uint8_t LCD_Reg, uint8_t LCD_RegValue)
{
  	LCD_WR_REG(LCD_Reg);
	LCD_WR_DATA(LCD_RegValue);
}

/*****************************************************************************
 * @name       :void LCD_WriteRAM_Prepare(void)
 * @date       :2018-08-09 
 * @function   :Write GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_WriteRAM_Prepare(void)
{
 	LCD_WR_REG(lcddev.wramcmd);	  
}

/*****************************************************************************
 * @name       :void LCD_Clear(uint16_t Color)
 * @date       :2018-08-09 
 * @function   :Full screen filled LCD screen
 * @parameters :color:Filled color
 * @retvalue   :None
******************************************************************************/	
void LCD_Clear(uint16_t Color)
{
	uint16_t i,j;
	uint16_t k=1;
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);	
    for(i=0;i<lcddev.width;i++)
	 {
	  for (j=0;j<lcddev.height;j++)
	   	{
        	 LCD_WR_DATA_16Bit(Color);
	    }

	  }


}

/*****************************************************************************
 * @name       :void LCD_DrawPoint(uint16_t x,uint16_t y)
 * @date       :2018-08-09 
 * @function   :Write a pixel data at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/	
void LCD_DrawPoint(uint16_t x,uint16_t y)
{
	LCD_SetWindows(x,y,x,y);//设置光标位置 
	LCD_WR_DATA_16Bit(POINT_COLOR); 	    
} 	 

/*****************************************************************************
 * @name       :void LCD_Reset(void)
 * @date       :2018-08-09 
 * @function   :Reset LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_Reset(void)
{
	gpio_set_level(TFTDISP_RESET, 1); // LCD_RESET=1;
	vTaskDelay(50 / portTICK_PERIOD_MS);
	gpio_set_level(TFTDISP_RESET, 0); // LCD_RESET=0;
	vTaskDelay(50 / portTICK_PERIOD_MS);
	gpio_set_level(TFTDISP_RESET, 1); // LCD_RESET=1;
	vTaskDelay(50 / portTICK_PERIOD_MS);
}

/*****************************************************************************
 * @name       :void LCD_Init(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 	 
void LCD_Init(void)
{
	LCD_Reset(); //初始化之前复位
//*************2.8inch ILI9341初始化**********//	
	LCD_WR_REG(0xCF);  
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0xC9); //C1 
	LCD_WR_DATA(0X30); 
	LCD_WR_REG(0xED);  
	LCD_WR_DATA(0x64); 
	LCD_WR_DATA(0x03); 
	LCD_WR_DATA(0X12); 
	LCD_WR_DATA(0X81); 
	LCD_WR_REG(0xE8);  
	LCD_WR_DATA(0x85); 
	LCD_WR_DATA(0x10); 
	LCD_WR_DATA(0x7A); 
	LCD_WR_REG(0xCB);  
	LCD_WR_DATA(0x39); 
	LCD_WR_DATA(0x2C); 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x34); 
	LCD_WR_DATA(0x02); 
	LCD_WR_REG(0xF7);  
	LCD_WR_DATA(0x20); 
	LCD_WR_REG(0xEA);  
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x00); 
	LCD_WR_REG(0xC0);    //Power control 
	LCD_WR_DATA(0x1B);   //VRH[5:0] 
	LCD_WR_REG(0xC1);    //Power control 
	LCD_WR_DATA(0x00);   //SAP[2:0];BT[3:0] 01 
	LCD_WR_REG(0xC5);    //VCM control 
	LCD_WR_DATA(0x30); 	 //3F
	LCD_WR_DATA(0x30); 	 //3C
	LCD_WR_REG(0xC7);    //VCM control2 
	LCD_WR_DATA(0XB7); 
	LCD_WR_REG(0x36);    // Memory Access Control 
	LCD_WR_DATA(0x08); 
	LCD_WR_REG(0x3A);   
	LCD_WR_DATA(0x55); 
	LCD_WR_REG(0xB1);   
	LCD_WR_DATA(0x00);   
	LCD_WR_DATA(0x1A); 
	LCD_WR_REG(0xB6);    // Display Function Control 
	LCD_WR_DATA(0x0A); 
	LCD_WR_DATA(0xA2); 
	LCD_WR_REG(0xF2);    // 3Gamma Function Disable 
	LCD_WR_DATA(0x00); 
	LCD_WR_REG(0x26);    //Gamma curve selected 
	LCD_WR_DATA(0x01); 
	LCD_WR_REG(0xE0);    //Set Gamma 
	LCD_WR_DATA(0x0F); 
	LCD_WR_DATA(0x2A); 
	LCD_WR_DATA(0x28); 
	LCD_WR_DATA(0x08); 
	LCD_WR_DATA(0x0E); 
	LCD_WR_DATA(0x08); 
	LCD_WR_DATA(0x54); 
	LCD_WR_DATA(0XA9); 
	LCD_WR_DATA(0x43); 
	LCD_WR_DATA(0x0A); 
	LCD_WR_DATA(0x0F); 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x00); 		 
	LCD_WR_REG(0XE1);    //Set Gamma 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x15); 
	LCD_WR_DATA(0x17); 
	LCD_WR_DATA(0x07); 
	LCD_WR_DATA(0x11); 
	LCD_WR_DATA(0x06); 
	LCD_WR_DATA(0x2B); 
	LCD_WR_DATA(0x56); 
	LCD_WR_DATA(0x3C); 
	LCD_WR_DATA(0x05); 
	LCD_WR_DATA(0x10); 
	LCD_WR_DATA(0x0F); 
	LCD_WR_DATA(0x3F); 
	LCD_WR_DATA(0x3F); 
	LCD_WR_DATA(0x0F); 
	LCD_WR_REG(0x2B); 
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x01);
	LCD_WR_DATA(0x3f);
	LCD_WR_REG(0x2A); 
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0xef);	 
	LCD_WR_REG(0x11); //Exit Sleep
	vTaskDelay(120 / portTICK_PERIOD_MS); // delay_ms(120);
	LCD_WR_REG(0x29); //display on	

	//设置LCD属性参数
	LCD_direction(1);//0=USE_HORIZONTAL
	//LCD_BL=1;//点亮背光	 
}
 
/*****************************************************************************
 * @name       :void LCD_SetWindows(uint16_t xStar, uint16_t yStar,uint16_t xEnd,uint16_t yEnd)
 * @date       :2018-08-09 
 * @function   :Setting LCD display window
 * @parameters :xStar:the bebinning x coordinate of the LCD display window
								yStar:the bebinning y coordinate of the LCD display window
								xEnd:the endning x coordinate of the LCD display window
								yEnd:the endning y coordinate of the LCD display window
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetWindows(uint16_t xStar, uint16_t yStar,uint16_t xEnd,uint16_t yEnd)
{	
	LCD_WR_REG(lcddev.setxcmd);	
	LCD_WR_DATA(xStar>>8);
	LCD_WR_DATA(0x00FF&xStar);		
	LCD_WR_DATA(xEnd>>8);
	LCD_WR_DATA(0x00FF&xEnd);

	LCD_WR_REG(lcddev.setycmd);	
	LCD_WR_DATA(yStar>>8);
	LCD_WR_DATA(0x00FF&yStar);		
	LCD_WR_DATA(yEnd>>8);
	LCD_WR_DATA(0x00FF&yEnd);	

	LCD_WriteRAM_Prepare();	//开始写入GRAM				
}   

/*****************************************************************************
 * @name       :void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
 * @date       :2018-08-09 
 * @function   :Set coordinate value
 * @parameters :Xpos:the  x coordinate of the pixel
								Ypos:the  y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
{	  	    			
	LCD_SetWindows(Xpos,Ypos,Xpos,Ypos);	
} 

/*****************************************************************************
 * @name       :void LCD_direction(uint8_t direction)
 * @date       :2018-08-09 
 * @function   :Setting the display direction of LCD screen
 * @parameters :direction:0-0 degree
                          1-90 degree
													2-180 degree
													3-270 degree
 * @retvalue   :None
******************************************************************************/ 
void LCD_direction(uint8_t direction)
{ 
			lcddev.setxcmd=0x2A;
			lcddev.setycmd=0x2B;
			lcddev.wramcmd=0x2C;
	switch(direction){		  
		case 0:						 	 		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;		
			LCD_WriteReg(0x36,(1<<3)|(0<<6)|(0<<7));//BGR==1,MY==0,MX==0,MV==0
		break;
		case 1:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<3)|(0<<7)|(1<<6)|(1<<5));//BGR==1,MY==1,MX==0,MV==1
		break;
		case 2:						 	 		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;	
			LCD_WriteReg(0x36,(1<<3)|(1<<6)|(1<<7));//BGR==1,MY==0,MX==0,MV==0
		break;
		case 3:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<3)|(1<<7)|(1<<5));//BGR==1,MY==1,MX==0,MV==1
		break;	
		default:break;
	}		
	
}	 






static void tftd_task(void*arg)
{
    ESP_LOGI(TAG,"tftd_task START \n");
	//vTaskDelay(100 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	//i2s_adc_enable(SFZX_I2S_NUM);
	uint16_t pos=12;

	LCD_Init();
		vTaskDelay(100 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	LCD_Clear(0);
		vTaskDelay(500 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	LCD_Clear(0xFFFF);
		vTaskDelay(500 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	LCD_Clear(RED);
		vTaskDelay(500 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	LCD_Clear(GREEN);
	LCD_Clear(0);
		vTaskDelay(500 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
    ESP_LOGI(TAG,"tftd_task START \n");
    while(true){
	   	//ESP_LOGI(TAG,"ledmx_task line \n");
		vTaskDelay(200 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
		LCD_DrawPoint(pos,pos);
		pos=(pos+10)&0x7f;

		LCD_SetWindows(320-pos,pos,320-pos+7,pos+7);	
		for(uint16_t i=0;i<64;i++)
		{
				LCD_WR_DATA_16Bit(RED);
		}


    }
}
