#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_FLAG_PRESENT (1ULL << 0)
#define PAGE_FLAG_WRITE   (1ULL << 1)
#define PAGE_FLAG_USER    (1ULL << 2)
#define PAGE_FLAG_NX      (1ULL << 63)

// Initialize the Virtual Memory Manager (Paging)
void vmm_init(uint64_t hhdm_offset);

// Map a physical page to a virtual page
void vmm_map_page(uint64_t* pml4, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);

// Unmap a virtual page
void vmm_unmap_page(uint64_t* pml4, uint64_t virtual_addr);

// Get the current PML4 table (from CR3)
uint64_t* vmm_get_current_pml4(void);

// Switch the active PML4 table
void vmm_switch_pml4(uint64_t* pml4);

// Translate physical to virtual and vice-versa
void* vmm_phys_to_virt(uint64_t physical_addr);
uint64_t vmm_virt_to_phys(void* virtual_addr);
