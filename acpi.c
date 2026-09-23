#include "acpi.h"

void *map_window(int which, u32 phys);

int acpi_checksum(const u8 *p, u32 len) {
    u8 sum = 0;
    for (u32 i = 0; i < len; i++) sum += p[i];
    return sum == 0 ? 0 : -1;
}

u32 acpi_find_rsdp(void) {
    for (u32 addr = 0xE0000; addr < 0x100000; addr += 16) {
        const u8 *p = (const u8 *)addr;
        if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
            p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
            if (acpi_checksum(p, 20) == 0) return addr;
        }
    }
    return 0;
}

int acpi_count_tables(u32 rsdt) {
    if (!rsdt) return -1;
    const sdt_hdr_t *h = (const sdt_hdr_t *)rsdt;
    if (rsdt >= 0x1000000) h = (const sdt_hdr_t *)map_window(0, rsdt);
    if (h->sig[0] != 'R' || h->sig[1] != 'S' || h->sig[2] != 'D' || h->sig[3] != 'T')
        return -1;
    if (h->len < sizeof(sdt_hdr_t)) return -1;
    return (h->len - sizeof(sdt_hdr_t)) / 4;
}

u32 acpi_find_table(u32 rsdt, const char *sig4) {
    if (!rsdt) return 0;
    if (rsdt >= 0x1000000) rsdt = (u32)map_window(0, rsdt);
    int n = acpi_count_tables(rsdt);
    if (n <= 0) return 0;
    const u32 *ent = (const u32 *)(rsdt + sizeof(sdt_hdr_t));
    for (int i = 0; i < n && i < 32; i++) {
        if (ent[i] == 0) continue;
        const sdt_hdr_t *h = (const sdt_hdr_t *)ent[i];
        if (ent[i] >= 0x1000000) h = (const sdt_hdr_t *)map_window(1, ent[i]);
        if (h->sig[0] == sig4[0] && h->sig[1] == sig4[1] &&
            h->sig[2] == sig4[2] && h->sig[3] == sig4[3])
            return ent[i];
    }
    return 0;
}

u32 acpi_madt_lapic(u32 madt, u8 *cpu_count) {
    *cpu_count = 0;
    if (!madt) return 0;
    const sdt_hdr_t *h = (const sdt_hdr_t *)madt;
    if (h->sig[0] != 'A' || h->sig[1] != 'P' || h->sig[2] != 'I' || h->sig[3] != 'C')
        return 0;
    if (h->len > 4096) return 0;
    if (madt >= 0x1000000) {
        u8 hdr[64];
        const u8 *mp = (const u8 *)map_window(1, madt);
        for (int i = 0; i < 64; i++) hdr[i] = mp[i];
        if (hdr[0] != 'A' || hdr[1] != 'P' || hdr[2] != 'I' || hdr[3] != 'C')
            return 0;
        madt = (u32)map_window(1, madt);
        h = (const sdt_hdr_t *)madt;
    }
    u32 lapic = *(const u32 *)(madt + 36);
    u32 off = 44;
    while (off + 2 <= h->len && off < h->len) {
        const u8 *e = (const u8 *)(madt + off);
        u8 type = e[0], len = e[1];
        if (len < 2) break;
        if (type == 0 && len >= 8 && (e[4] & 1)) (*cpu_count)++;
        off += len;
    }
    return lapic;
}
