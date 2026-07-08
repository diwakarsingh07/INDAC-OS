#include "vmm.h"
#include "pmm.h"
#include "terminal.h"

static uint64_t hhdm_base = 0;
static uint64_t* kernel_pml4 = NULL;

void vmm_init(uint64_t hhdm_offset) {
    hhdm_base = hhdm_offset;
    
    // In Limine, the bootloader already gives us a fully working PML4 in CR3.
    // The safest architectural move is to take over the existing PML4 and manage it,
    // rather than building a new one from scratch which could unmap Limine structures.
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL; // CRITICAL: Mask out lower 12 bits (flags like PWT/PCD)
    
    // CR3 contains the PHYSICAL address of the PML4. We must access it via HHDM virtual address.
    kernel_pml4 = (uint64_t*)(cr3 + hhdm_base);
}

void* vmm_phys_to_virt(uint64_t physical_addr) {
    return (void*)(physical_addr + hhdm_base);
}

uint64_t vmm_virt_to_phys(void* virtual_addr) {
    return (uint64_t)virtual_addr - hhdm_base;
}

uint64_t* vmm_get_current_pml4(void) {
    return kernel_pml4;
}

void vmm_switch_pml4(uint64_t* pml4) {
    uint64_t phys = vmm_virt_to_phys((void*)pml4);
    asm volatile("mov %0, %%cr3" : : "r"(phys));
}

// Maps a single 4KB page.
void vmm_map_page(uint64_t* pml4, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags) {
    // 4-level paging uses 9 bits per level
    uint64_t pml4_idx = (virtual_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virtual_addr >> 30) & 0x1FF;
    uint64_t pd_idx   = (virtual_addr >> 21) & 0x1FF;
    uint64_t pt_idx   = (virtual_addr >> 12) & 0x1FF;

    // 1. PML4 -> PDPT
    if (!(pml4[pml4_idx] & PAGE_FLAG_PRESENT)) {
        // Allocate a new PDPT page
        void* pdpt_phys_ptr = pmm_alloc_page();
        if (!pdpt_phys_ptr) { terminal_print("[VMM PANIC] Out of memory allocating PDPT!\n"); return; }
        uint64_t pdpt_phys = (uint64_t)pdpt_phys_ptr;
        
        // Clear the page using HHDM
        uint64_t* pdpt_virt = (uint64_t*)vmm_phys_to_virt(pdpt_phys);
        for (int i = 0; i < 512; i++) pdpt_virt[i] = 0;
        
        pml4[pml4_idx] = pdpt_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;
    }
    
    uint64_t* pdpt = (uint64_t*)vmm_phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    // 2. PDPT -> PD
    if (!(pdpt[pdpt_idx] & PAGE_FLAG_PRESENT)) {
        // Allocate a new PD page
        void* pd_phys_ptr = pmm_alloc_page();
        if (!pd_phys_ptr) { terminal_print("[VMM PANIC] Out of memory allocating PD!\n"); return; }
        uint64_t pd_phys = (uint64_t)pd_phys_ptr;
        
        uint64_t* pd_virt = (uint64_t*)vmm_phys_to_virt(pd_phys);
        for (int i = 0; i < 512; i++) pd_virt[i] = 0;
        
        pdpt[pdpt_idx] = pd_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;
    }
    
    uint64_t* pd = (uint64_t*)vmm_phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    // 3. PD -> PT
    if (!(pd[pd_idx] & PAGE_FLAG_PRESENT)) {
        // Allocate a new PT page
        void* pt_phys_ptr = pmm_alloc_page();
        if (!pt_phys_ptr) { terminal_print("[VMM PANIC] Out of memory allocating PT!\n"); return; }
        uint64_t pt_phys = (uint64_t)pt_phys_ptr;
        
        uint64_t* pt_virt = (uint64_t*)vmm_phys_to_virt(pt_phys);
        for (int i = 0; i < 512; i++) pt_virt[i] = 0;
        
        pd[pd_idx] = pt_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;
    }
    
    uint64_t* pt = (uint64_t*)vmm_phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    // 4. Map the physical page in the PT
    pt[pt_idx] = (physical_addr & ~0xFFFULL) | flags;
    
    // Invalidate the TLB for this virtual address so the CPU sees the change
    asm volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

void vmm_unmap_page(uint64_t* pml4, uint64_t virtual_addr) {
    uint64_t pml4_idx = (virtual_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virtual_addr >> 30) & 0x1FF;
    uint64_t pd_idx   = (virtual_addr >> 21) & 0x1FF;
    uint64_t pt_idx   = (virtual_addr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_FLAG_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)vmm_phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PAGE_FLAG_PRESENT)) return;
    uint64_t* pd = (uint64_t*)vmm_phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PAGE_FLAG_PRESENT)) return;
    uint64_t* pt = (uint64_t*)vmm_phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0; // Clear the entry
    asm volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}
