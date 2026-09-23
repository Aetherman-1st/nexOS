#ifndef USB_H
#define USB_H

#include "io.h"

#define USB_MAX_CTRL 2
#define USB_MAX_DEV 4

typedef struct {
    int present;
    int lowspeed;
    int port;
    int addr;
    u16 vid;
    u16 pid;
    u8 nconf;
    u8 hid_iface;
    u8 hid_ep;
    u8 hid_pkt;
    u8 fail_step;
} usb_dev_t;

typedef struct {
    int present;
    u16 iobase;
    u8 irq;
    usb_dev_t devs[USB_MAX_DEV];
    int ndev;
} uhci_t;

int usb_init(void);
void usb_rescan(void);

int usb_count(void);
usb_dev_t *usb_get(int idx);
int usb_hid_poll(int idx, u8 *report8);

#endif
