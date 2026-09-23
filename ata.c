#include "ata.h"

static u8 ata_buf[512];

int ata_probe(void) {
    outb(0x1F6, 0xA0);
    outb(0x1F7, 0xEC);
    for (int i = 0; i < 10000; i++) {
        u8 s = inb(0x1F7);
        if (s == 0x00) return 1;
    }
    return 0;
}

void ata_read_sector_data(u32 lba, u8 *buf) {
    outb(0x1F2, 1);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F7, 0x20);

    for (;;) {
        u8 s = inb(0x1F7);
        if (s & 0x08) break;
        if (s & 0x01) return;
    }

    insw(0x1F0, (u16 *)buf, 256);
}
