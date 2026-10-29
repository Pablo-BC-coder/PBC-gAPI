// PBC-gAPI — Copyright (c) Pablo B. C. All rights reserved.
#pragma once
// Arduino IDE: edit here. PlatformIO: overrides are supplied to ALL units.
#ifndef PBC_USE_TFT_ESPI
#define PBC_USE_TFT_ESPI 0
#endif
// 1 SPI ILI9341 240x320; 2 parallel ILI9341 240x320;
// 3 SPI ST7789 240x240; 4 SPI ST7789 240x320; 5 SPI ILI9488 320x480;
// 6 SPI ST7735 128x160; 7 SPI GC9A01 240x240;
// 8 parallel ILI9486 320x480; 9 parallel ILI9488 320x480.
#ifndef PBC_DISPLAY_PROFILE
#define PBC_DISPLAY_PROFILE 1
#endif
#ifndef PBC_TOUCH_XPT2046
#define PBC_TOUCH_XPT2046 1
#endif
// Four-wire shield touch is opt-in: verify XP/YP/XM/YM on YOUR board first.
#ifndef PBC_TOUCH_4WIRE
#define PBC_TOUCH_4WIRE 0
#endif
#if PBC_TOUCH_XPT2046 && PBC_TOUCH_4WIRE
#error Choose XPT2046 OR four-wire resistive touch
#endif
#define PBC_TOUCH_XP 13
#define PBC_TOUCH_YP 26
#define PBC_TOUCH_XM 27
#define PBC_TOUCH_YM 14
#define PBC_TOUCH_CS 22
#define PBC_TOUCH_IRQ 34
#define PBC_SPI_SCLK 18
#define PBC_SPI_MOSI 23
#define PBC_SPI_MISO 19
#define PBC_SPI_CS 27
#define PBC_SPI_DC 26
#define PBC_SPI_RST 25
// PARALLEL: custom jumper wiring, not the ESP32 UNO shield socket mapping.
#define PBC_PAR_CS 26
#define PBC_PAR_DC 27
#define PBC_PAR_RST 33
#define PBC_PAR_WR 25
#define PBC_PAR_RD 32
#define PBC_PAR_D0 13
#define PBC_PAR_D1 14
#define PBC_PAR_D2 16
#define PBC_PAR_D3 17
#define PBC_PAR_D4 18
#define PBC_PAR_D5 19
#define PBC_PAR_D6 21
#define PBC_PAR_D7 22
#if PBC_DISPLAY_PROFILE==2 || PBC_DISPLAY_PROFILE==8 || PBC_DISPLAY_PROFILE==9
#define PBC_PARALLEL 1
#else
#define PBC_PARALLEL 0
#endif
#if PBC_PARALLEL && PBC_TOUCH_XPT2046
#error Parallel pins overlap default SPI touch pins. Disable XPT2046 or remap buses.
#endif
#if PBC_TOUCH_4WIRE && !PBC_PARALLEL
#error Default four-wire touch mapping is only for the parallel shield
#endif
#if PBC_DISPLAY_PROFILE==1 || PBC_DISPLAY_PROFILE==2 || PBC_DISPLAY_PROFILE==4
#define PBC_PANEL_WIDTH 240
#define PBC_PANEL_HEIGHT 320
#elif PBC_DISPLAY_PROFILE==3 || PBC_DISPLAY_PROFILE==7
#define PBC_PANEL_WIDTH 240
#define PBC_PANEL_HEIGHT 240
#elif PBC_DISPLAY_PROFILE==5 || PBC_DISPLAY_PROFILE==8 || PBC_DISPLAY_PROFILE==9
#define PBC_PANEL_WIDTH 320
#define PBC_PANEL_HEIGHT 480
#elif PBC_DISPLAY_PROFILE==6
#define PBC_PANEL_WIDTH 128
#define PBC_PANEL_HEIGHT 160
#else
#error Unknown PBC_DISPLAY_PROFILE
#endif
#ifndef PBC_STRIP_ROWS
#define PBC_STRIP_ROWS 16
#endif
// 1 = native display resolution; 2 = half each axis, nearest 2x output.
#ifndef PBC_RENDER_SCALE
#define PBC_RENDER_SCALE 1
#endif
#ifndef PBC_SPI_FREQUENCY
#define PBC_SPI_FREQUENCY 27000000
#endif
#ifndef PBC_PANEL_INVERT
#define PBC_PANEL_INVERT 0
#endif

// TFT_eSPI compiles separately: include THIS file before its User_Setup_Select.h
// in every translation unit. PIO -include does so automatically. See README.
#if PBC_USE_TFT_ESPI
#define USER_SETUP_LOADED 1
#define USER_SETUP_INFO "PBC-gAPI ESP32-WROOM-32"
#if PBC_DISPLAY_PROFILE==1 || PBC_DISPLAY_PROFILE==2
#define ILI9341_DRIVER
#elif PBC_DISPLAY_PROFILE==3 || PBC_DISPLAY_PROFILE==4
#define ST7789_DRIVER
#elif PBC_DISPLAY_PROFILE==5 || PBC_DISPLAY_PROFILE==9
#define ILI9488_DRIVER
#elif PBC_DISPLAY_PROFILE==6
#define ST7735_DRIVER
#define ST7735_BLACKTAB
#elif PBC_DISPLAY_PROFILE==7
#define GC9A01_DRIVER
#elif PBC_DISPLAY_PROFILE==8
#define ILI9486_DRIVER
#endif
#if PBC_DISPLAY_PROFILE==3 || PBC_DISPLAY_PROFILE==4 || PBC_DISPLAY_PROFILE==6
#define TFT_WIDTH PBC_PANEL_WIDTH
#define TFT_HEIGHT PBC_PANEL_HEIGHT
#endif
#if PBC_PARALLEL
#define TFT_PARALLEL_8_BIT
#define TFT_CS PBC_PAR_CS
#define TFT_DC PBC_PAR_DC
#define TFT_RST PBC_PAR_RST
#define TFT_WR PBC_PAR_WR
#define TFT_RD PBC_PAR_RD
#define TFT_D0 PBC_PAR_D0
#define TFT_D1 PBC_PAR_D1
#define TFT_D2 PBC_PAR_D2
#define TFT_D3 PBC_PAR_D3
#define TFT_D4 PBC_PAR_D4
#define TFT_D5 PBC_PAR_D5
#define TFT_D6 PBC_PAR_D6
#define TFT_D7 PBC_PAR_D7
#else
#define TFT_MISO PBC_SPI_MISO
#define TFT_MOSI PBC_SPI_MOSI
#define TFT_SCLK PBC_SPI_SCLK
#define TFT_CS PBC_SPI_CS
#define TFT_DC PBC_SPI_DC
#define TFT_RST PBC_SPI_RST
#define SPI_FREQUENCY PBC_SPI_FREQUENCY
#define SPI_READ_FREQUENCY 10000000
#endif
#if PBC_TOUCH_XPT2046
#define TOUCH_CS PBC_TOUCH_CS
#define SPI_TOUCH_FREQUENCY 2500000
#endif
#if PBC_PANEL_INVERT
#define TFT_INVERSION_ON
#else
#define TFT_INVERSION_OFF
#endif
#define LOAD_GLCD
#endif
