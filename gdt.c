#include "gdt.h"

// Each GDT entry is 8 bytes long
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

// The pointer structure we pass to the 'lgdt' instruction
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

static gdt_entry_t gdt[7]; // 0: Null, 1: KCode, 2: KData, 3: UData, 4: UCode, 5: TSS Low, 6: TSS High
static gdtr_t gdtr;

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[index].base_low    = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high   = (base >> 24) & 0xFF;

    gdt[index].limit_low   = (limit & 0xFFFF);
    gdt[index].granularity = (limit >> 16) & 0x0F;
    gdt[index].granularity |= gran & 0xF0;
    
    gdt[index].access      = access;
}

// Special function to set the 16-byte TSS descriptor in AMD64
void gdt_set_tss(uint32_t index, uint64_t base, uint32_t limit) {
    gdt_set_entry(index, (uint32_t)base, limit, 0x89, 0x00); // Access: 0x89 (Present, Ring 0, TSS)
    // The upper 8 bytes of the TSS descriptor
    uint32_t base_upper = (uint32_t)(base >> 32);
    // Overwrite the next GDT entry with the upper 32 bits of the base
    *(uint32_t*)&gdt[index + 1] = base_upper;
    *((uint32_t*)&gdt[index + 1] + 1) = 0;
}

void gdt_init(void) {
    gdtr.limit = (sizeof(gdt_entry_t) * 7) - 1;
    gdtr.base  = (uint64_t)&gdt[0];

    // Descriptor 0: Null Segment
    gdt_set_entry(0, 0, 0, 0, 0);

    // Descriptor 1: Kernel Code Segment (Offset 0x08)
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);

    // Descriptor 2: Kernel Data Segment (Offset 0x10)
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);

    // Descriptor 3: User Data Segment (Offset 0x18)
    // Access: 0xF2 -> Present, Ring 3, Data, Read/Write
    gdt_set_entry(3, 0, 0xFFFFF, 0xF2, 0xC0);

    // Descriptor 4: User Code Segment (Offset 0x20)
    // Access: 0xFA -> Present, Ring 3, Executable, Read/Write
    gdt_set_entry(4, 0, 0xFFFFF, 0xFA, 0xA0);

    // TSS will be loaded later at offsets 5 and 6 (0x28, 0x30)
    
    // Push the GDT pointer structure into the CPU
    asm volatile ("lgdt %0" : : "m"(gdtr));

    // Reload the CPU segment registers
    asm volatile (
        "push $0x08\n"
        "lea .reload_cs(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"                        // Far return into the new CS
        ".reload_cs:\n"                  // CPU resumes execution here with new CS
        "mov $0x10, %%ax\n"              // Load our new DS selector (offset 0x10)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "rax", "memory"
    );
}
