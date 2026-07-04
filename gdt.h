#pragma once
#include <stdint.h>

void gdt_init(void);
void gdt_set_tss(uint32_t index, uint64_t base, uint32_t limit);
