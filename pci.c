#include "pci.h"

u32 pci_cfg_read(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
               ((u32)fn << 8) | (off & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_cfg_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
               ((u32)fn << 8) | (off & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, v);
}

int pci_scan(pci_dev_t *out, int max) {
    int n = 0;
    for (u8 dev = 0; dev < 32 && n < max; dev++) {
      for (u8 fn = 0; fn < 8 && n < max; fn++) {
        u32 id = pci_cfg_read(0, dev, fn, 0x00);
        if (id == 0xFFFFFFFFu) continue;
        pci_dev_t *d = &out[n];
        d->bus = 0; d->dev = dev; d->fn = fn;
        d->vendor = id & 0xFFFF;
        d->device = (id >> 16) & 0xFFFF;
        u32 cc = pci_cfg_read(0, dev, fn, 0x08);
        d->prog_if = (cc >> 8) & 0xFF;
        d->subclass = (cc >> 16) & 0xFF;
        d->class_code = (cc >> 24) & 0xFF;
        d->irq = pci_cfg_read(0, dev, fn, 0x3C) & 0xFF;
        for (int i = 0; i < 6; i++)
            d->bar[i] = pci_cfg_read(0, dev, fn, 0x10 + i * 4);
        u32 cmd = pci_cfg_read(0, dev, fn, 0x04);
        pci_cfg_write(0, dev, fn, 0x04, cmd | 0x07);
        n++;
      }
    }
    return n;
}

int pci_find(u16 vendor, u16 device, pci_dev_t *out) {
    pci_dev_t tmp[PCI_MAX_DEVICES];
    int n = pci_scan(tmp, PCI_MAX_DEVICES);
    for (int i = 0; i < n; i++) {
        if (tmp[i].vendor == vendor && tmp[i].device == device) {
            *out = tmp[i];
            return 0;
        }
    }
    return -1;
}

u32 pci_bar_io(pci_dev_t *d, int idx) {
    if (idx < 0 || idx > 5) return 0;
    if ((d->bar[idx] & 1) == 0) return 0;
    return d->bar[idx] & 0xFFFC;
}

const char *pci_class_name(u8 class_code, u8 subclass) {
    (void)subclass;
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01: return "Mass storage";
    case 0x02: return "Network";
    case 0x03: return "Display";
    case 0x04: return "Multimedia";
    case 0x05: return "Memory";
    case 0x06: return "Bridge";
    case 0x07: return "Comm";
    case 0x08: return "System";
    case 0x09: return "Input";
    case 0x0A: return "Docking";
    case 0x0B: return "CPU";
    case 0x0C: return "Serial bus";
    case 0x0D: return "Wireless";
    case 0x0E: return "Intelligent IO";
    case 0x0F: return "Satellite";
    case 0x10: return "Crypto";
    case 0x11: return "Signal proc";
    default: return "Unknown";
    }
}
