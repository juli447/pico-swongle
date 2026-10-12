#pragma once
#include <stdint.h>
typedef uint32_t absolute_time_t;
absolute_time_t get_absolute_time(void);
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return t; }
uint32_t time_us_32(void);
