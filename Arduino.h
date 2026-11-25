#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define VSPI 3
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
unsigned long millis(); unsigned long micros(); void delay(unsigned long);
void pinMode(int,int); void digitalWrite(int,int);
void vTaskDelay(TickType_t);
typedef void (*TaskFunction_t)(void*);
int xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, int, void*, int);
struct SerialStub { void begin(long); } ; extern SerialStub Serial;
