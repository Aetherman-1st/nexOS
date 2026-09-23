#ifndef ATA_H
#define ATA_H

#include "io.h"

#define ATA_BASE 0x1F0
#define ATA_IRQ 14

#define ATA_READ  0x20
#define ATA_WRITE 0x30

#define ATA_STATUS 0x1F7
#define ATA_ERROR  0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA_LOW  0x1F3
#define ATA_LBA_MID  0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DEVICE   0x1F6
#define ATA_COMMAND  0x1F7

#define ATA_DEV_MASTER 0xE0
#define ATA_IDENTIFY   0xEC

#define ATA_STATUS_BUSY    0x80
#define ATA_STATUS_DRQ     0x08
#define ATA_STATUS_ERR     0x01
#define ATA_STATUS_READY   0x40

static inline int ata_wait_ready(void) {
    for (int i = 0; i < 1000000; i++) {
        u8 s = inb(ATA_STATUS);
        if ((s & 0x80) == 0 && (s & 0x40) == 0) return 0;
    }
    return -1;
}

static inline void ata_init(void) {
    for (int i = 0; i < 0x100000; i++) {
        if ((inb(ATA_STATUS) & 0x80) == 0 && (inb(ATA_STATUS) & 0x40) == 0) break;
    }
}

static inline int ata_read_sector(u32 lba, u8 *buf) {
    if (ata_wait_ready() != 0) return -1;
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  lba      & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH,(lba >> 16) & 0xFF);
    outb(ATA_DEVICE,  ATA_DEV_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_READ);

    for (;;) {
        u8 s = inb(ATA_STATUS);
        if (s & 0x01) return -1;
        if (s & 0x08) break;
    }

    insw(ATA_BASE, (u16 *)buf, 256);
    return 0;
}

static inline int ata_write_sector(u32 lba, const u8 *buf) {
    if (ata_wait_ready() != 0) return -1;
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  lba      & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH,(lba >> 16) & 0xFF);
    outb(ATA_DEVICE,  ATA_DEV_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_WRITE);

    for (;;) {
        u8 s = inb(ATA_STATUS);
        if (s & 0x01) return -1;
        if (s & 0x08) break;
    }

    outsw(ATA_BASE, (u16 *)buf, 256);
    return 0;
}

void ata_read_sector_data(u32 lba, u8 *buf);

#endif
