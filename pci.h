#ifndef PCI_H
#define PCI_H

#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVICES 32

typedef struct {
    u8 bus;
    u8 dev;
    u8 fn;
    u16 vendor;
    u16 device;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 irq;
    u32 bar[6];
} pci_dev_t;

u32 pci_cfg_read(u8 bus, u8 dev, u8 fn, u8 off);
void pci_cfg_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v);
int pci_scan(pci_dev_t *out, int max);
int pci_find(u16 vendor, u16 device, pci_dev_t *out);
u32 pci_bar_io(pci_dev_t *d, int idx);
const char *pci_class_name(u8 class_code, u8 subclass);

#endif
