#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096

// Initialize the Physical Memory Manager
void pmm_init(struct limine_memmap_response* memmap, uint64_t hhdm_offset);

// Allocate a single 4KB physical page
void* pmm_alloc_page(void);

// Free a 4KB physical page
void pmm_free_page(void* ptr);

// Get the amount of total and free RAM
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);
