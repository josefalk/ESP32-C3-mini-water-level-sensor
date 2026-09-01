#include <U8g2lib.h>
#include <Wire.h>

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);
int width = 72;
int height = 40;

void setup(void)
{
     delay(1000);
     u8g2.begin();
     u8g2.setContrast(255); // set contrast to maximum 
     u8g2.setBusClock(400000); //400kHz I2C 
     u8g2.setFont(u8g2_font_ncenB10_tr);
}

void loop(void)
{
    u8g2.clearBuffer(); // clear the internal memory
    u8g2.drawFrame(0, 0, width, height); //draw a frame around the border
    u8g2.setCursor(15, 25);
    u8g2.printf("%dx%d", width, height);
    u8g2.sendBuffer(); // transfer internal memory to the display
}