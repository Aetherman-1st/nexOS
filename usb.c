#include "usb.h"
#include "pci.h"

#define UHCI_CMD     0x00
#define UHCI_STS     0x02
#define UHCI_INTR    0x04
#define UHCI_FRNUM   0x06
#define UHCI_FLBASE  0x08
#define UHCI_SOF     0x0C
#define UHCI_PORT1   0x10
#define UHCI_PORT2   0x12

#define TD_TERMINATE 0x01
#define TD_QSELECT   0x02
#define TD_DEPTH     0x04
#define TD_ACTIVE    (1 << 23)
#define TD_IOC       (1 << 24)
#define TD_STALLED   (1 << 22)
#define TD_BABBLE    (1 << 20)
#define TD_NAK       (1 << 19)
#define TD_CRC       (1 << 18)
#define TD_BITSTUFF  (1 << 17)

#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

typedef struct {
    u32 link;
    u32 status;
    u32 token;
    u32 buffer;
} __attribute__((packed)) uhci_td_t;

typedef struct {
    u32 head;
    u32 elem;
} __attribute__((packed)) uhci_qh_t;


static uhci_t ctrls[USB_MAX_CTRL];
static int nctrl = 0;
static u32 frame_list[1024] __attribute__((aligned(4096)));
static uhci_qh_t qh_mem __attribute__((aligned(16)));
static uhci_td_t td_mem[16] __attribute__((aligned(16)));
static uhci_qh_t *qh = &qh_mem;
static uhci_td_t *tds = td_mem;
static u8 td_toggle = 0;

static void udelay(u32 us) {
    for (u32 i = 0; i < us * 20; i++) inb(0x80);
}

static void uhci_reset_hc(uhci_t *c) {
    outw(c->iobase + UHCI_CMD, 0x0004);
    udelay(200);
    outw(c->iobase + UHCI_CMD, 0x0002);
    for (int t = 0; t < 1000; t++) {
        if ((inw(c->iobase + UHCI_CMD) & 0x0002) == 0) break;
        udelay(100);
    }
    outw(c->iobase + UHCI_STS, 0xFFFF);
}

static int uhci_exec(uhci_t *c, uhci_td_t *chain, int ntd) {
    for (int i = 0; i < ntd; i++) {
        chain[i].link = (i + 1 < ntd) ? (u32)&chain[i + 1] : TD_TERMINATE;
        chain[i].status |= TD_ACTIVE;
    }
    qh->elem = (u32)&chain[0];
    for (volatile int t = 0; t < 2000000; t++) {
        int done = 1;
        for (int i = 0; i < ntd; i++) {
            u32 st = chain[i].status;
            if (st & TD_ACTIVE) {
                if (st & (TD_STALLED | TD_BABBLE | TD_CRC | TD_BITSTUFF)) return -1;
                done = 0;
            }
        }
        if (done) {
            qh->elem = TD_TERMINATE;
            return 0;
        }
    }
    qh->elem = TD_TERMINATE;
    return -2;
}

static void uhci_token(uhci_td_t *td, u8 pid, u8 addr, u8 ep, u32 len, u8 toggle, u8 *buf) {
    td->link = TD_TERMINATE;
    td->status = 0;
    td->token = ((u32)(len ? len - 1 : 0x7FF) << 21) |
                ((u32)(toggle ? 1 : 0) << 19) |
                ((u32)ep << 15) | ((u32)addr << 8) | pid;
    td->buffer = (u32)buf;
}

static u8 setup_pkt[8];
static u8 data_pkt[256];

static int uhci_control(uhci_t *c, u8 addr, u8 reqtype, u8 req,
                        u16 value, u16 index, u16 len, u8 *data, int dir_in) {
    (void)c;
    setup_pkt[0] = reqtype; setup_pkt[1] = req;
    setup_pkt[2] = value & 0xFF; setup_pkt[3] = (value >> 8) & 0xFF;
    setup_pkt[4] = index & 0xFF; setup_pkt[5] = (index >> 8) & 0xFF;
    setup_pkt[6] = len & 0xFF; setup_pkt[7] = (len >> 8) & 0xFF;
    uhci_td_t *t = tds;
    uhci_token(&t[0], PID_SETUP, addr, 0, 8, 0, setup_pkt);
    int ntd = 1;
    u32 done = 0;
    u8 tog = 1;
    if (len && data) {
        while (done < len && ntd < 14) {
            u32 chunk = len - done > 64 ? 64 : len - done;
            if (dir_in) {
                uhci_token(&t[ntd], PID_IN, addr, 0, chunk, tog, data + done);
            } else {
                for (u32 i = 0; i < chunk; i++) data_pkt[i] = data[done + i];
                uhci_token(&t[ntd], PID_OUT, addr, 0, chunk, tog, data_pkt);
            }
            tog ^= 1;
            done += chunk;
            ntd++;
        }
    }
    uhci_token(&t[ntd], dir_in ? PID_OUT : PID_IN, addr, 0, 0, 1, data_pkt);
    ntd++;
    for (int r = 0; r < 3; r++) {
        int rc = uhci_exec(c, t, ntd);
        if (rc == 0) return (int)done;
        if (rc == -1) return -1;
        udelay(1000);
    }
    return -1;
}

static int uhci_get_desc(uhci_t *c, u8 addr, u8 type, u8 idx, u8 *buf, u16 len) {
    return uhci_control(c, addr, 0x80, 0x06, (u16)((type << 8) | idx), 0, len, buf, 1);
}

static int uhci_set_addr(uhci_t *c, u8 addr) {
    u8 dummy = 0;
    int r = uhci_control(c, 0, 0x00, 0x05, addr, 0, 0, &dummy, 0);
    udelay(2000);
    return r >= 0 ? 0 : -1;
}

static int uhci_set_config(uhci_t *c, u8 addr, u8 cfg) {
    u8 dummy = 0;
    int r = uhci_control(c, addr, 0x00, 0x09, cfg, 0, 0, &dummy, 0);
    udelay(2000);
    return r >= 0 ? 0 : -1;
}

static int uhci_set_protocol(uhci_t *c, usb_dev_t *d) {
    u8 dummy = 0;
    int r = uhci_control(c, d->addr, 0x21, 0x0B, 0, d->hid_iface, 0, &dummy, 0);
    return r >= 0 ? 0 : -1;
}

static void uhci_parse_config(usb_dev_t *d, u8 *cfg, u32 len) {
    u32 i = 0;
    u8 cur_iface = 0xFF;
    while (i + 2 <= len) {
        u8 l = cfg[i], t = cfg[i + 1];
        if (l < 2 || i + l > len) break;
        if (t == 4 && l >= 9) {
            cur_iface = cfg[i + 2];
            if (cfg[i + 6] == 3 && (cfg[i + 7] == 1)) {
                d->hid_iface = cur_iface;
            }
        } else if (t == 5 && l >= 7 && cur_iface == d->hid_iface) {
            u8 ep = cfg[i + 2] & 0x0F;
            u8 dir_in = (cfg[i + 2] & 0x80) ? 1 : 0;
            u8 attr = cfg[i + 3] & 0x03;
            if (dir_in && attr == 3) {
                d->hid_ep = ep;
                d->hid_pkt = cfg[i + 4] ? cfg[i + 4] : 8;
            }
        }
        i += l;
    }
}

static int uhci_enum_port(uhci_t *c, int port, usb_dev_t *d) {
    u16 base = c->iobase + (port ? UHCI_PORT2 : UHCI_PORT1);
    u16 ps = inw(base);
    if ((ps & 0x0001) == 0) { d->fail_step = 1; return -1; }
    d->present = 1;
    d->port = port;
    d->lowspeed = (ps & 0x0100) ? 1 : 0;
    outw(base, 0x0200);
    udelay(50000);
    outw(base, 0x0000);
    udelay(10000);
    for (int t = 0; t < 1000; t++) {
        ps = inw(base);
        if (ps & 0x0004) break;
        udelay(1000);
    }
    ps = inw(base);
    if ((ps & 0x0004) == 0) {
        outw(base, (ps & ~0x000A) | 0x0004);
        udelay(10000);
    }
    ps = inw(base);
    if ((ps & 0x0001) == 0 || (ps & 0x0004) == 0) { d->fail_step = 2; return -1; }

    u8 desc[18];
    if (uhci_get_desc(c, 0, 1, 0, desc, 8) < 0) { d->fail_step = 3; return -1; }
    if (uhci_set_addr(c, 5 + (u8)(d - c->devs)) < 0) { d->fail_step = 4; return -1; }
    d->addr = 5 + (u8)(d - c->devs);
    if (uhci_get_desc(c, d->addr, 1, 0, desc, 18) < 0) { d->fail_step = 5; return -1; }
    d->vid = desc[8] | ((u16)desc[9] << 8);
    d->pid = desc[10] | ((u16)desc[11] << 8);
    d->nconf = desc[17];
    u8 cfg[256];
    int n = uhci_get_desc(c, d->addr, 2, 0, cfg, 9);
    if (n >= 9) {
        u16 total = cfg[2] | ((u16)cfg[3] << 8);
        if (total > sizeof(cfg)) total = sizeof(cfg);
        if (uhci_get_desc(c, d->addr, 2, 0, cfg, total) >= 0)
            uhci_parse_config(d, cfg, total);
    }
    if (uhci_set_config(c, d->addr, 1) < 0) { d->fail_step = 6; return -1; }
    if (d->hid_ep) uhci_set_protocol(c, d);
    return 0;
}

static void uhci_start(uhci_t *c) {
    for (int i = 0; i < 1024; i++) frame_list[i] = TD_TERMINATE;
    qh->head = TD_TERMINATE;
    qh->elem = TD_TERMINATE;
    outl(c->iobase + UHCI_FLBASE, (u32)frame_list);
    frame_list[0] = (u32)qh | TD_QSELECT;
    outw(c->iobase + UHCI_FRNUM, 0);
    outb(c->iobase + UHCI_SOF, 64);
    outw(c->iobase + UHCI_INTR, 0);
    outw(c->iobase + UHCI_CMD, 0x0001 | 0x0040 | 0x0080);
    udelay(5000);
}


void usb_rescan_ports(uhci_t *c) {
    for (int p = 0; p < 2 && c->ndev < USB_MAX_DEV; p++) {
        usb_dev_t *d = &c->devs[c->ndev];
        d->addr = 0; d->hid_ep = 0; d->hid_iface = 0xFF; d->hid_pkt = 8;
        d->fail_step = 0;
        if (uhci_enum_port(c, p, d) == 0) c->ndev++;
        else d->present = 0;
    }
}

void usb_rescan(void) {
    for (int i = 0; i < nctrl; i++) {
        ctrls[i].ndev = 0;
        usb_rescan_ports(&ctrls[i]);
    }
}

int usb_init(void) {
    nctrl = 0;
    pci_dev_t found[32];
    int n = pci_scan(found, 32);
    for (int fi = 0; fi < n && nctrl < USB_MAX_CTRL; fi++) {
        if (found[fi].class_code != 0x0C || found[fi].subclass != 0x03 ||
            found[fi].prog_if != 0x00)
            continue;
        uhci_t *c = &ctrls[nctrl];
        u32 bar = found[fi].bar[4];
        c->iobase = (u16)(bar & 0xFFE0);
        c->irq = found[fi].irq;
        c->present = 1;
        c->ndev = 0;
        uhci_reset_hc(c);
        uhci_start(c);
        usb_rescan_ports(c);
        nctrl++;
    }
    return nctrl;
}

int usb_count(void) {
    int n = 0;
    for (int i = 0; i < nctrl; i++) n += ctrls[i].ndev;
    return n;
}

usb_dev_t *usb_get(int idx) {
    for (int i = 0; i < nctrl; i++) {
        if (idx < ctrls[i].ndev) return &ctrls[i].devs[idx];
        idx -= ctrls[i].ndev;
    }
    return 0;
}

int usb_hid_poll(int idx, u8 *report8) {
    usb_dev_t *d = usb_get(idx);
    uhci_t *c = 0;
    if (!d || !d->hid_ep) return -1;
    for (int i = 0; i < nctrl; i++) {
        for (int j = 0; j < ctrls[i].ndev; j++) {
            if (&ctrls[i].devs[j] == d) c = &ctrls[i];
        }
    }
    if (!c) return -1;
    static u8 inbuf[16];
    uhci_td_t *t = tds;
    uhci_token(t, PID_IN, d->addr, d->hid_ep, d->hid_pkt, td_toggle, inbuf);
    int rc = uhci_exec(c, t, 1);
    if (rc != 0) return -1;
    td_toggle ^= 1;
    for (int i = 0; i < 8; i++) report8[i] = inbuf[i];
    return 0;
}
