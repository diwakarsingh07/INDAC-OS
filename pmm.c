#include "pmm.h"
#include "terminal.h"

// The Bitmap that tracks physical pages
static uint8_t* pmm_bitmap = NULL;
static uint64_t pmm_bitmap_size = 0; // Size of bitmap in bytes
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_free_pages = 0;
static uint64_t pmm_last_allocated_index = 0; // Speed optimization for next allocation

// Helper functions to manage the bitmap
static inline void bitmap_set(uint64_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint64_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int bitmap_test(uint64_t bit) {
    return (pmm_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

// Ensure an address is page-aligned
static inline uint64_t align_up(uint64_t addr, uint64_t align) {
    return (addr + align - 1) & ~(align - 1);
}

static inline uint64_t align_down(uint64_t addr, uint64_t align) {
    return addr & ~(align - 1);
}

void pmm_init(struct limine_memmap_response* memmap, uint64_t hhdm_offset) {
    uint64_t top_address = 0;
    
    // Pass 1: Find the highest usable address to determine how much RAM we have
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE || 
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            
            uint64_t entry_top = entry->base + entry->length;
            if (entry_top > top_address) {
                top_address = entry_top;
            }
        }
    }

    pmm_total_pages = top_address / PAGE_SIZE;
    pmm_bitmap_size = align_up(pmm_total_pages / 8, PAGE_SIZE); // Bitmap size in bytes, aligned to a page

    // Pass 2: Find a usable chunk of memory large enough to hold the bitmap itself!
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length >= pmm_bitmap_size) {
                // Apply the HHDM offset to convert the physical address to a virtual one!
                pmm_bitmap = (uint8_t*)(entry->base + hhdm_offset);
                break;
            }
        }
    }

    if (pmm_bitmap == NULL) {
        // We cannot use terminal_print here yet because the terminal is not initialized
        return; // Hang or triple fault later
    }

    // Initialize all pages to USED (1) by default for safety
    for (uint64_t i = 0; i < pmm_bitmap_size; i++) {
        pmm_bitmap[i] = 0xFF; 
    }

    // Pass 3: Free only the USABLE memory regions in the bitmap
    uint64_t pmm_bitmap_phys = (uint64_t)pmm_bitmap - hhdm_offset;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t base = entry->base;
            uint64_t length = entry->length;
            
            // Lock the region where we placed the bitmap!
            if (pmm_bitmap_phys >= base && pmm_bitmap_phys < base + length) {
                // First part: before the bitmap
                if (pmm_bitmap_phys > base) {
                    uint64_t len1 = pmm_bitmap_phys - base;
                    for (uint64_t b = base; b < base + len1; b += PAGE_SIZE) {
                        if (b == 0) continue; // NEVER free physical page 0
                        bitmap_clear(b / PAGE_SIZE);
                        pmm_free_pages++;
                    }
                }
                // Second part: after the bitmap
                uint64_t bitmap_end_phys = pmm_bitmap_phys + pmm_bitmap_size;
                if (base + length > bitmap_end_phys) {
                    uint64_t len2 = (base + length) - bitmap_end_phys;
                    for (uint64_t b = bitmap_end_phys; b < bitmap_end_phys + len2; b += PAGE_SIZE) {
                        if (b == 0) continue;
                        bitmap_clear(b / PAGE_SIZE);
                        pmm_free_pages++;
                    }
                }
            } else {
                // Free the entire region
                for (uint64_t b = base; b < base + length; b += PAGE_SIZE) {
                    if (b == 0) continue; // NEVER free physical page 0
                    bitmap_clear(b / PAGE_SIZE);
                    pmm_free_pages++;
                }
            }
        }
    }
}

void* pmm_alloc_page(void) {
    for (uint64_t i = pmm_last_allocated_index; i < pmm_total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_free_pages--;
            pmm_last_allocated_index = i;
            return (void*)(i * PAGE_SIZE);
        }
    }
    
    // If we hit the top, wrap around and search from the start
    for (uint64_t i = 0; i < pmm_last_allocated_index; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_free_pages--;
            pmm_last_allocated_index = i;
            return (void*)(i * PAGE_SIZE);
        }
    }

    return NULL; // Out of Memory!
}

void pmm_free_page(void* ptr) {
    uint64_t addr = (uint64_t)ptr;
    if (addr % PAGE_SIZE != 0) return; // Must be page aligned
    
    uint64_t page_index = addr / PAGE_SIZE;
    if (bitmap_test(page_index)) {
        bitmap_clear(page_index);
        pmm_free_pages++;
        // If we freed something below the last allocated index, we can search here next time
        if (page_index < pmm_last_allocated_index) {
            pmm_last_allocated_index = page_index;
        }
    }
}

uint64_t pmm_get_total_memory(void) {
    return pmm_total_pages * PAGE_SIZE;
}

uint64_t pmm_get_free_memory(void) {
    return pmm_free_pages * PAGE_SIZE;
}
