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
        if (s & 0x01) return -1;
        if ((s & 0x80) == 0) return (s & 0x40) ? 0 : -1;
    }
    return -1;
}

static inline void ata_init(void) {
    for (int i = 0; i < 0x100000; i++) {
        u8 s = inb(ATA_STATUS);
        if ((s & 0x80) == 0 && (s & 0x40)) break;
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

    for (int t = 0; t < 1000000; t++) {
        u8 s = inb(ATA_STATUS);
        if (s & 0x01) return -1;
        if (s & 0x08) break;
    }

    if ((inb(ATA_STATUS) & 0x08) == 0) return -1;
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

    for (int t = 0; t < 1000000; t++) {
        u8 s = inb(ATA_STATUS);
        if (s & 0x01) return -1;
        if (s & 0x08) break;
    }

    if ((inb(ATA_STATUS) & 0x08) == 0) return -1;
    outsw(ATA_BASE, (u16 *)buf, 256);
    for (int t = 0; t < 1000000; t++) {
        u8 s = inb(ATA_STATUS);
        if (s & 0x01) return -1;
        if ((s & 0x80) == 0) break;
    }
    return 0;
}

void ata_read_sector_data(u32 lba, u8 *buf);

static u8 ata_ident_buf[512];
static int ata_has_lba48 = 0;
static u32 ata_sectors_lo = 0;
static u32 ata_sectors_hi = 0;

static inline int ata_identify(void) {
    outb(ATA_DEVICE, ATA_DEV_MASTER);
    if (ata_wait_ready() != 0) return -1;
    outb(ATA_COMMAND, ATA_IDENTIFY);
    for (int t = 0; t < 1000000; t++) {
        u8 st = inb(ATA_STATUS);
        if (st & 0x01) return -1;
        if (st & 0x08) break;
    }
    if ((inb(ATA_STATUS) & 0x08) == 0) return -1;
    insw(ATA_BASE, (u16 *)ata_ident_buf, 256);
    u16 *w = (u16 *)ata_ident_buf;
    ata_has_lba48 = (w[83] & (1 << 10)) ? 1 : 0;
    ata_sectors_lo = ((u32)w[61] << 16) | w[60];
    ata_sectors_hi = ((u32)w[103] << 16) | w[102];
    return 0;
}

static inline int ata_read_lba48(u32 lo, u32 hi, u8 *buf) {
    if (!ata_has_lba48) return -1;
    if (ata_wait_ready() != 0) return -1;
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW, (lo >> 24) & 0xFF);
    outb(ATA_LBA_MID, hi & 0xFF);
    outb(ATA_LBA_HIGH, (hi >> 8) & 0xFF);
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, lo & 0xFF);
    outb(ATA_LBA_MID, (lo >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lo >> 16) & 0xFF);
    outb(ATA_DEVICE, 0x40);
    outb(ATA_COMMAND, 0x24);
    for (int t = 0; t < 1000000; t++) {
        u8 st = inb(ATA_STATUS);
        if (st & 0x01) return -1;
        if (st & 0x08) break;
    }
    if ((inb(ATA_STATUS) & 0x08) == 0) return -1;
    insw(ATA_BASE, (u16 *)buf, 256);
    return 0;
}

#endif
