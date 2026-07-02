#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "idt.h"
#include "pic.h"
#include "isr.h"
#include "io.h"
#include "terminal.h"
#include "keyboard.h"
#include "gdt.h"
#include "shell.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "acpi.h"
#include "apic.h"
#include "mouse.h"
#include "ioapic.h"
#include "pci.h"
#include "ahci.h"
#include "fat32.h"
#include "gui.h"
#include "bmp.h"
#include "lockscreen.h"
#include "wm.h"

// Globals so the ISR can draw to the screen
volatile uint32_t *global_fb_ptr = NULL;
int global_fb_width = 0;
int global_fb_pitch = 0;
int color_toggle = 0;

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Memory map request to get the layout of physical RAM
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

// HHDM request: maps all physical memory to a high virtual address
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

// Kernel address request: gives us the physical and virtual base of the kernel
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kernel_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

// ACPI RSDP Request: asks the bootloader for the Root System Description Pointer
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Print custom system banner
    const char* banner = "SYSTEM INITIALIZED: Welcome to Professional 64-Bit Mode.";
    (void)banner;

    // Set globals for ISR
    global_fb_ptr = framebuffer->address;
    global_fb_width = framebuffer->width;
    global_fb_pitch = framebuffer->pitch;

    // Initialize the new terminal renderer FIRST so we can print boot logs!
    terminal_init(framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch);
    
    // Initialize the command line shell
    shell_init();
    
    terminal_print("Welcome to your custom OS terminal!\n");
    terminal_print("Type 'help' to see available commands...\n\n> ");

    // Initialize the Global Descriptor Table (GDT) and refresh segments
    gdt_init();

    // Check if we got the required Limine requests
    if (memmap_request.response == NULL || hhdm_request.response == NULL) {
        hcf();
    }

    uint64_t hhdm_offset = hhdm_request.response->offset;

    // Initialize the Physical Memory Manager (PMM)
    pmm_init(memmap_request.response, hhdm_offset);

    // Initialize the Virtual Memory Manager (VMM)
    vmm_init(hhdm_offset);

    // Initialize the Kernel Heap Allocator at Virtual Address 0xFFFF900000000000 (1MB initial)
    heap_init((void*)0xFFFF900000000000, 1024 * 1024);

    // Phase 2: ACPI & APIC Initialization
    if (rsdp_request.response != NULL) {
        acpi_init(rsdp_request.response->address, hhdm_offset);
        
        struct acpi_header* madt_header = acpi_find_table("APIC");
        if (madt_header) {
            terminal_print("[ACPI] Successfully found MADT (APIC table)!\n");
            apic_init((uint64_t)madt_header, hhdm_offset);
            ioapic_init((uint64_t)madt_header, hhdm_offset);
        } else {
            terminal_print("[ACPI] Failed to find MADT (APIC table).\n");
        }
    } else {
        terminal_print("[ACPI] Limine did not provide RSDP!\n");
    }

    // CRITICAL: Disable the legacy 8259 PIC!
    // Since we are using the modern APIC/IOAPIC, leaving the old PIC enabled 
    // causes duplicate hardware interrupts, locking up the CPU completely!
    pic_disable();

    // Set up Timer ISR on Vector 32 (IRQ 0 equivalent)
    idt_set_descriptor(32, timer_isr, 0x8E);

    // Set up Keyboard ISR on Vector 33 (IRQ 1 equivalent)
    idt_set_descriptor(33, keyboard_isr, 0x8E); // 0x8E = 32-bit Interrupt Gate, Present
    ioapic_set_entry(1, 33); // Unmask Keyboard IRQ 1 and route to Vector 33

    // Set up Mouse ISR on Vector 44 (IRQ 12 equivalent)
    idt_set_descriptor(44, mouse_isr, 0x8E);
    ioapic_set_entry(12, 44); // Unmask Mouse IRQ 12 and route to Vector 44

    // Load IDT
    idt_init();

    // Start the High-Precision APIC Timer on Vector 32
    apic_timer_init(32);

    // Enumerate the PCI Bus to discover hardware (Hard Drives, Network Cards, Graphics)
    pci_enumerate();
    
    // Initialize AHCI to communicate with the SATA hard drive
    ahci_init(hhdm_offset);
    
    int sata_port = ahci_get_active_port();
    if (sata_port != -1) {
        terminal_print("[MAIN] Launching FAT32 Subsystem...\n");
        // Initialize FAT32 filesystem
        fat32_init((uint32_t)sata_port, hhdm_offset);
        
        // Initialize GUI
        gui_init(framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch);
        
        // Hide bootloader logs by clearing the screen to black
        gui_clear_screen(0x00000000);
        
        // Render First Logo
        uint32_t size1 = 0;
        void* file1 = fat32_read_file("logo1.bmp", &size1);
        if (file1) {
            bmp_draw(file1, framebuffer->width / 2, framebuffer->height / 2);
            gui_swap_buffers();
        }
        
        // Create a flowing RGB animated wave!
        int bar_width = 300; // Horizontal width (approx 2-3 cm)
        int bar_height = 6;  // Thickness of the wave
        int bar_x = (framebuffer->width - bar_width) / 2;
        int bar_y = framebuffer->height - 150;

        // Pre-computed smooth sine wave heights (since we don't have math.h)
        int wave_pattern[32] = {
            0, 1, 2, 3, 4, 5, 6, 6, 6, 5, 4, 3, 2, 1, 0, -1,
            -2, -3, -4, -5, -6, -7, -7, -7, -6, -5, -4, -3, -2, -1, 0, 0
        };

        // Run the animation for 150 frames
        for (int frame = 0; frame < 150; frame++) {
            for (int x = 0; x < bar_width; x++) {
                
                // 1. Determine the color (Flowing Indian Flag: Saffron -> White -> Green)
                int color_phase = (x - (frame * 5)) % 300;
                if (color_phase < 0) color_phase += 300;
                
                uint32_t color;
                if (color_phase < 100) {
                    color = 0x00FF9933; // Saffron (Orange)
                } else if (color_phase < 200) {
                    color = 0x00FFFFFF; // White
                } else {
                    color = 0x00138808; // Green
                }

                // 2. Determine the physical wave height (Undulating motion)
                int wave_idx = ((x + (frame * 2)) % 32);
                if (wave_idx < 0) wave_idx += 32;
                int y_offset = wave_pattern[wave_idx];

                // 3. Draw the vertical strip (drawing the wave, and erasing the trails with black)
                for (int y = -10; y < 15; y++) {
                    if (y >= y_offset && y < y_offset + bar_height) {
                        gui_draw_pixel(bar_x + x, bar_y + y, color); // Draw wave
                    } else {
                        gui_draw_pixel(bar_x + x, bar_y + y, 0x00000000); // Erase trail
                    }
                }
            }
            gui_swap_buffers();
            // Micro-delay between frames so the animation is smooth and visible
            for (volatile uint64_t i = 0; i < 20000000ULL; i++) {}
        }
        
        // Clear screen to black again
        gui_clear_screen(0x00000000);
        
        // Render Second Logo
        uint32_t size2 = 0;
        void* file2 = fat32_read_file("logo2.bmp", &size2);
        if (file2) {
            bmp_draw(file2, framebuffer->width / 2, framebuffer->height / 2);
            gui_swap_buffers();
        }
        
        // Delay for the second logo
        for (volatile uint64_t i = 0; i < 3000000000ULL; i++) {}
        
        // --- START LOCK SCREEN ---
        lockscreen_init();
        gui_swap_buffers();
        
        // Init mouse hardware and setup initial coordinates
        mouse_init();
    }

    // Route the Keyboard IRQ (1) to the Local APIC via the IOAPIC on Vector 33
    ioapic_set_entry(1, 33);
    
    // Route the Mouse IRQ (12) to the Local APIC via the IOAPIC on Vector 44
    ioapic_set_entry(12, 44);

    // CRITICAL: Modern UEFI firmware (OVMF) disables legacy PS/2 interrupts!
    // We must manually instruct the 8042 PS/2 Controller to enable Keyboard & Mouse IRQs.
    
    // Wait for input buffer to be clear before sending command
    while (inb(0x64) & 2);
    outb(0x64, 0x20);      // Command: Read Configuration Byte
    
    // Wait for output buffer to be full before reading
    while ((inb(0x64) & 1) == 0);
    uint8_t config = inb(0x60);
    
    config |= 1;           // Set Bit 0 to enable Keyboard Interrupts
    config |= 2;           // Set Bit 1 to enable Mouse Interrupts
    
    // Wait for input buffer to be clear before sending command
    while (inb(0x64) & 2);
    outb(0x64, 0x60);      // Command: Write Configuration Byte
    
    // Wait for input buffer to be clear before writing data
    while (inb(0x64) & 2);
    outb(0x60, config);

    // Flush any stale data out of the keyboard buffer so it can trigger a new interrupt
    while (inb(0x64) & 1) {
        inb(0x60);
    }

    // We are fully configured. Enable hardware interrupts!
    asm volatile("sti");

    // We are now fully Interrupt-Driven!
    // The CPU will sleep (hlt) and consume 0% power until you press a key!
    extern volatile int vsync_tick;
    extern void wm_compose(void);
    extern void gui_swap_buffers(void);
    int desktop_started = 0;
    extern int lockscreen_active;
    
    while (1) {
            if (vsync_tick) {
                vsync_tick = 0;
                
                if (lockscreen_active) {
                    extern void lockscreen_compose(void);
                    lockscreen_compose();
                } else {
                    if (!desktop_started) {
                        desktop_started = 1;
                        wm_init();
                    }
                    wm_compose();
                }
                
                gui_swap_buffers();
            }
        // Halt CPU until next interrupt fires
        asm volatile("hlt");
    }
}
