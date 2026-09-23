#ifndef ACPI_H
#define ACPI_H

#include "io.h"

typedef struct {
    char sig[8];
    u8 checksum;
    char oem[6];
    u8 rev;
    u32 rsdt;
} __attribute__((packed)) rsdp_t;

typedef struct {
    char sig[4];
    u32 len;
    u8 rev;
    u8 checksum;
    char oem[6];
    char oem_table[8];
    u32 oem_rev;
    u32 creator;
    u32 creator_rev;
} __attribute__((packed)) sdt_hdr_t;

int acpi_checksum(const u8 *p, u32 len);
u32 acpi_find_rsdp(void);
int acpi_count_tables(u32 rsdt);
u32 acpi_find_table(u32 rsdt, const char *sig4);
u32 acpi_madt_lapic(u32 madt, u8 *cpu_count);

#endif
