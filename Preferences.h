#pragma once
#include <Arduino.h>
struct Preferences { bool begin(const char*, bool); void end(); uint8_t getUChar(const char*, uint8_t); size_t putUChar(const char*, uint8_t);
 float getFloat(const char*, float); size_t putFloat(const char*, float); };
