#ifndef IDT_H
#define IDT_H

// Define an IDT entry structure aligned exactly to x86 requirements
struct idt_entry_struct {
    unsigned short base_low;  // The lower 16 bits of the address to jump to
    unsigned short sel;       // Kernel segment selector (usually 0x08)
    unsigned char  always0;   // This must always be zero
    unsigned char  flags;     // Flags (Presence, Privilege levels, Type)
    unsigned short base_high; // The upper 16 bits of the address to jump to
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

// Define the IDT pointer structure that tells the CPU where the table is
struct idt_ptr_struct {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

// Declare the table of 256 interrupt gates
idt_entry_t idt[256];
idt_ptr_t   idt_reg;

// External assembly function that actually loads the IDT pointer into the CPU register
extern void idt_flush(unsigned int);

// Helper to register an interrupt handler function to a specific gate index
void set_idt_gate(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel     = sel;
    idt[num].always0 = 0;
    idt[num].flags   = flags;
}

#endif
