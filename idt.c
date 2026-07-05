#include "idt.h"

__attribute__((aligned(0x10))) 
static idt_entry_t idt[IDT_MAX_DESCRIPTORS];
static idtr_t idtr;

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags) {
    idt_entry_t *descriptor = &idt[vector];
    uint64_t isr_addr = (uint64_t)isr;

    descriptor->isr_low    = (uint16_t)(isr_addr & 0xFFFF);
    descriptor->kernel_cs  = 0x08; // Pointing to our new GDT Kernel Code Segment!
    
    // Use IST1 for Double Fault (Vector 8)
    descriptor->ist        = (vector == 8) ? 1 : 0;
    
    descriptor->attributes = flags;
    descriptor->isr_mid    = (uint16_t)((isr_addr >> 16) & 0xFFFF);
    descriptor->isr_high   = (uint32_t)((isr_addr >> 32) & 0xFFFFFFFF);
    descriptor->reserved   = 0;
}

void idt_init(void) {
    idtr.base = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1;

    extern void exception_isr(void);
    for (int i = 0; i < 32; i++) {
        idt_set_descriptor(i, exception_isr, 0x8E);
    }

    asm volatile ("lidt %0" : : "m"(idtr));
    // Do NOT enable interrupts (sti) yet! We are still using the polling loop.
    // If we enable interrupts before the APIC is fully wired to the IOAPIC, 
    // stray unmapped interrupts will instantly triple-fault the CPU!
}
