#pragma once
#include <Arduino.h>
#define MC_DATUM 4
#define TL_DATUM 0
struct TFT_eSPI { TFT_eSPI(); void init(); void setRotation(uint8_t); void invertDisplay(bool); void fillScreen(uint32_t);
 void fillRect(int32_t,int32_t,int32_t,int32_t,uint32_t); void fillRoundRect(int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t);
 void drawRoundRect(int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t); void drawLine(int32_t,int32_t,int32_t,int32_t,uint32_t);
 void drawCircle(int32_t,int32_t,int32_t,uint32_t); void fillCircle(int32_t,int32_t,int32_t,uint32_t);
 void setTextColor(uint16_t); void setTextDatum(uint8_t); int16_t drawString(const char*,int32_t,int32_t,uint8_t); };
