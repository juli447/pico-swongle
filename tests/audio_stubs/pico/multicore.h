#pragma once
#include <stdint.h>
#include <stddef.h>
void multicore_launch_core1_with_stack(void (*)(void),uint32_t *,size_t);
