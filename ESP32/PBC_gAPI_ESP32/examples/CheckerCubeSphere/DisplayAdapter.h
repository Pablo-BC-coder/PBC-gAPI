// PBC-gAPI — Copyright (c) Pablo B. C. All rights reserved.
#pragma once
#include "DisplayConfig.h"
#include <Arduino.h>
#if PBC_TOUCH_4WIRE
#include <soc/gpio_struct.h>
#endif
#if PBC_USE_TFT_ESPI
#include <TFT_eSPI.h>
using PBCDisplay=TFT_eSPI;
#else
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
class PBCDisplay: public lgfx::LGFX_Device {
#if PBC_PARALLEL
    lgfx::Bus_Parallel8 bus;
#else
    lgfx::Bus_SPI bus;
#endif
#if PBC_DISPLAY_PROFILE==1 || PBC_DISPLAY_PROFILE==2
    lgfx::Panel_ILI9341 panel;
#elif PBC_DISPLAY_PROFILE==3 || PBC_DISPLAY_PROFILE==4
    lgfx::Panel_ST7789 panel;
#elif PBC_DISPLAY_PROFILE==5 || PBC_DISPLAY_PROFILE==9
    lgfx::Panel_ILI9488 panel;
#elif PBC_DISPLAY_PROFILE==6
    lgfx::Panel_ST7735S panel;
#elif PBC_DISPLAY_PROFILE==7
    lgfx::Panel_GC9A01 panel;
#elif PBC_DISPLAY_PROFILE==8
    lgfx::Panel_ILI9486 panel;
#endif
#if PBC_TOUCH_XPT2046
    lgfx::Touch_XPT2046 touch;
#endif
public:
    PBCDisplay() {
        auto b=bus.config();
#if PBC_PARALLEL
        b.freq_write=15000000;
        b.pin_wr=PBC_PAR_WR; b.pin_rd=PBC_PAR_RD; b.pin_rs=PBC_PAR_DC;
        b.pin_d0=PBC_PAR_D0; b.pin_d1=PBC_PAR_D1; b.pin_d2=PBC_PAR_D2; b.pin_d3=PBC_PAR_D3;
        b.pin_d4=PBC_PAR_D4; b.pin_d5=PBC_PAR_D5; b.pin_d6=PBC_PAR_D6; b.pin_d7=PBC_PAR_D7;
#else
        b.spi_host=VSPI_HOST; b.spi_mode=0;
        b.freq_write=PBC_SPI_FREQUENCY; b.freq_read=10000000;
        b.spi_3wire=false; b.use_lock=true; b.dma_channel=1;
        b.pin_sclk=PBC_SPI_SCLK; b.pin_mosi=PBC_SPI_MOSI;
        b.pin_miso=PBC_SPI_MISO; b.pin_dc=PBC_SPI_DC;
#endif
        bus.config(b); panel.setBus(&bus);
        auto p=panel.config();
        p.pin_cs=PBC_PARALLEL?PBC_PAR_CS:PBC_SPI_CS;
        p.pin_rst=PBC_PARALLEL?PBC_PAR_RST:PBC_SPI_RST;
        p.panel_width=PBC_PANEL_WIDTH; p.panel_height=PBC_PANEL_HEIGHT;
        // ST7789 240x240 still has 240x320 controller memory.
        p.memory_width=PBC_PANEL_WIDTH;
        p.memory_height=(PBC_DISPLAY_PROFILE==3)?320:PBC_PANEL_HEIGHT;
        p.offset_x=0; p.offset_y=0; p.offset_rotation=0;
        p.readable=false; p.invert=PBC_PANEL_INVERT; p.rgb_order=false; p.bus_shared=true;
        panel.config(p);
#if PBC_TOUCH_XPT2046
        auto t=touch.config();
        t.spi_host=VSPI_HOST; t.freq=2500000;
        t.pin_sclk=PBC_SPI_SCLK; t.pin_mosi=PBC_SPI_MOSI; t.pin_miso=PBC_SPI_MISO;
        t.pin_cs=PBC_TOUCH_CS; t.pin_int=PBC_TOUCH_IRQ;
        t.x_min=200; t.x_max=3900; t.y_min=200; t.y_max=3900;
        t.bus_shared=true; touch.config(t); panel.setTouch(&touch);
#endif
        setPanel(&panel);
    }
};
#endif

// Only contact is needed to change the model, so coordinate calibration is
// unnecessary. Sample BETWEEN display transactions, never while writing a band.
inline bool pbcTouched(PBCDisplay& display) {
#if PBC_TOUCH_4WIRE
    // Common MCUfriend shield: XP=D0, YM=D1, XM=LCD_RS, YP=LCD_CS.
    // Other shields differ. GPIO14/27 ADC2 works here because Wi-Fi is off.
    const int touchPins[4]={PBC_TOUCH_XP,PBC_TOUCH_YP,PBC_TOUCH_XM,PBC_TOUCH_YM};
    uint32_t savedMatrix[4];
    for(int i=0;i<4;++i) savedMatrix[i]=GPIO.func_out_sel_cfg[touchPins[i]].val;
    pinMode(PBC_TOUCH_YP,OUTPUT); digitalWrite(PBC_TOUCH_YP,HIGH);
    pinMode(PBC_TOUCH_XP,OUTPUT); digitalWrite(PBC_TOUCH_XP,LOW);
    pinMode(PBC_TOUCH_XM,INPUT); pinMode(PBC_TOUCH_YM,INPUT);
    delayMicroseconds(30);
    const int z1=analogRead(PBC_TOUCH_XM),z2=analogRead(PBC_TOUCH_YM);
    // Restore shared LCD data and control pins, including CS inactive.
    pinMode(PBC_TOUCH_XP,OUTPUT); pinMode(PBC_TOUCH_YM,OUTPUT);
    digitalWrite(PBC_PAR_CS,HIGH); pinMode(PBC_PAR_CS,OUTPUT);
    pinMode(PBC_PAR_DC,OUTPUT); digitalWrite(PBC_PAR_DC,HIGH);
    // pinMode/ADC detach the GPIO matrix. Reattach the saved I2S signals as
    // well as GPIO mode; otherwise LovyanGFX's parallel bus would stop here.
    for(int i=0;i<4;++i) GPIO.func_out_sel_cfg[touchPins[i]].val=savedMatrix[i];
    const int pressure=4095-(z2-z1);
    return pressure>300 && pressure<4050;
#elif PBC_TOUCH_XPT2046
#if PBC_USE_TFT_ESPI
    // TFT_eSPI getTouchRaw() ALWAYS returns true; pressure detects contact.
    return display.getTouchRawZ()>350;
#else
    uint16_t x=0,y=0;
    return display.getTouchRaw(&x,&y);
#endif
#else
    (void)display; return false;
#endif
}
