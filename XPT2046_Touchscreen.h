#pragma once
#include <SPI.h>
struct TS_Point { int16_t x,y,z; };
struct XPT2046_Touchscreen { XPT2046_Touchscreen(uint8_t); bool begin(SPIClass&); void setRotation(uint8_t); bool touched(); TS_Point getPoint(); };
