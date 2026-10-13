#pragma once
#include <stdint.h>
#define __not_in_flash_func(name) name
void sleep_us(uint64_t);
uint32_t time_us_32(void);
