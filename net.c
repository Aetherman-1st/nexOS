#include "net.h"
#include "pci.h"

static u16 rtl_base = 0;
static u8 rtl_irq = 11;
static u8 my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static u32 my_ip = IP_ADDR;
static u32 gw_ip = GW_ADDR;

static u8 tx_bufs[4][1536] __attribute__((aligned(4)));
static u8 rx_ring[8192 + 16] __attribute__((aligned(4)));
static u32 rx_offset = 0;
static int tx_cur = 0;

static u8 rx_pkt[2048];
static u32 rx_len = 0;
static volatile int rx_ready = 0;

static u8 eth_scratch[2048];

struct arp_ent { u32 ip; u8 mac[6]; int valid; };
static struct arp_ent arp_tab[8];

u32 net_get_gw(void) { return gw_ip; }
u8 net_get_irq(void) { return rtl_irq; }

static u32 pci_cfg_addr(u8 bus, u8 dev, u8 fn, u8 off) {
    return 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8) | (off & 0xFC);
}

static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 off) {
    outl(0xCF8, pci_cfg_addr(bus, dev, fn, off));
    return inl(0xCFC);
}

static void pci_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v) {
    outl(0xCF8, pci_cfg_addr(bus, dev, fn, off));
    outl(0xCFC, v);
}

static int rtl_pci_probe(void) {
    pci_dev_t d;
    if (pci_find(0x10EC, 0x8139, &d) != 0) {
        rtl_base = 0x300;
        return -1;
    }
    u32 io = pci_bar_io(&d, 0);
    rtl_base = io ? (u16)io : 0x300;
    if (d.irq != 0 && d.irq < 16) rtl_irq = d.irq;
    return 0;
}

void net_init(void) {
    rtl_pci_probe();
    if (rtl_base == 0) rtl_base = 0x300;
    for (int i = 0; i < 6; i++) my_mac[i] = inb(rtl_base + i);

    outb(rtl_base + 0x52, 0x00);
    outb(rtl_base + 0x37, 0x10);
    for (int t = 0; t < 100000; t++) {
        if ((inb(rtl_base + 0x37) & 0x10) == 0) break;
    }

    for (u32 i = 0; i < sizeof(rx_ring); i++) rx_ring[i] = 0;
    outl(rtl_base + 0x30, (u32)rx_ring);
    rx_offset = 0;

    outw(rtl_base + 0x3C, 0x0005);
    outl(rtl_base + 0x44, 0x00000F87);
    outb(rtl_base + 0x37, 0x0C);
    for (int i = 0; i < 8; i++) { arp_tab[i].valid = 0; arp_tab[i].ip = 0; }
}

static void rtl_tx_raw(const u8 *frame, u32 len) {
    if (len < 60) len = 60;
    if (len > 1536) len = 1536;
    for (u32 i = 0; i < len; i++) tx_bufs[tx_cur][i] = frame[i];
    outl(rtl_base + 0x20 + tx_cur * 4, (u32)tx_bufs[tx_cur]);
    outl(rtl_base + 0x10 + tx_cur * 4, len);
    tx_cur = (tx_cur + 1) & 3;
}

static void eth_hdr(u8 *buf, const u8 *dst) {
    for (int i = 0; i < 6; i++) { buf[i] = dst[i]; buf[i + 6] = my_mac[i]; }
    buf[12] = 0x08; buf[13] = 0x06;
}

static u16 cksum_raw(const u8 *data, u32 len) {
    u32 sum = 0;
    for (u32 i = 0; i + 1 < len; i += 2) sum += (u32)(((u16)data[i] << 8) | data[i + 1]);
    if (len & 1) sum += (u32)((u16)data[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

void net_arp_request(u32 gw) {
    u8 bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    u8 *buf = eth_scratch;
    for (u32 i = 0; i < 64; i++) buf[i] = 0;
    eth_hdr(buf, bcast);
    buf[14] = 0x00; buf[15] = 0x01;
    buf[16] = 0x08; buf[17] = 0x00;
    buf[18] = 0x06; buf[19] = 0x04;
    buf[20] = 0x00; buf[21] = 0x01;
    for (int i = 0; i < 6; i++) buf[22 + i] = my_mac[i];
    buf[28] = (my_ip >> 24) & 0xFF; buf[29] = (my_ip >> 16) & 0xFF;
    buf[30] = (my_ip >> 8) & 0xFF; buf[31] = my_ip & 0xFF;
    for (int i = 0; i < 6; i++) buf[32 + i] = 0x00;
    buf[38] = (gw >> 24) & 0xFF; buf[39] = (gw >> 16) & 0xFF;
    buf[40] = (gw >> 8) & 0xFF; buf[41] = gw & 0xFF;
    rtl_tx_raw(buf, 60);
}

static void arp_learn(const u8 *frame) {
    u32 sip = ((u32)frame[28] << 24) | ((u32)frame[29] << 16) | ((u32)frame[30] << 8) | frame[31];
    for (int i = 0; i < 8; i++) {
        if (arp_tab[i].valid && arp_tab[i].ip == sip) {
            for (int j = 0; j < 6; j++) arp_tab[i].mac[j] = frame[22 + j];
            return;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (!arp_tab[i].valid) {
            arp_tab[i].valid = 1; arp_tab[i].ip = sip;
            for (int j = 0; j < 6; j++) arp_tab[i].mac[j] = frame[22 + j];
            return;
        }
    }
}

int net_arp_lookup(u32 ip, u8 *mac_out) {
    for (int i = 0; i < 8; i++) {
        if (arp_tab[i].valid && arp_tab[i].ip == ip) {
            for (int j = 0; j < 6; j++) mac_out[j] = arp_tab[i].mac[j];
            return 0;
        }
    }
    return -1;
}

static void net_rx_ring(void) {
    for (int guard = 0; guard < 8; guard++) {
        u16 cur = inw(rtl_base + 0x3A) % 8192;
        u32 off = rx_offset % 8192;
        if (off == cur) break;
        u16 status = rx_ring[off] | ((u16)rx_ring[(off + 1) % 8192] << 8);
        u16 len = rx_ring[(off + 2) % 8192] | ((u16)rx_ring[(off + 3) % 8192] << 8);
        u32 adv;
        if ((status & 0x01) && len >= 60 && len <= 1540) {
            u32 n = (u32)len - 4;
            if (n > sizeof(rx_pkt)) n = sizeof(rx_pkt);
            for (u32 i = 0; i < n; i++) rx_pkt[i] = rx_ring[(off + 4 + i) % 8192];
            rx_len = n;
            rx_ready = 1;
            if (n >= 42 && rx_pkt[12] == 0x08 && rx_pkt[13] == 0x06 &&
                rx_pkt[20] == 0x00 && rx_pkt[21] == 0x02) {
                arp_learn(rx_pkt + 14);
            }
            adv = ((u32)len + 4 + 3) & ~3u;
        } else {
            adv = 64;
        }
        if (adv == 0) adv = 64;
        rx_offset = (off + adv) % 8192;
        outw(rtl_base + 0x38, (u16)((rx_offset - 16) & 0xFFFF));
    }
}

void net_handler(void) {
    u16 isr = inw(rtl_base + 0x3E);
    if (isr == 0) { outb(0xA0, 0x20); outb(0x20, 0x20); return; }
    outw(rtl_base + 0x3E, isr);
    if (isr & 0x01) net_rx_ring();
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static void send_tcp_syn(u32 local_ip, u32 remote_ip, u16 local_port, u16 remote_port, u32 seq) {
    u8 *buf = eth_scratch;
    for (u32 i = 0; i < 74; i++) buf[i] = 0;
    u8 bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (int i = 0; i < 6; i++) { buf[i] = bcast[i]; buf[i + 6] = my_mac[i]; }
    buf[12] = 0x08; buf[13] = 0x00;
    buf[14] = 0x45; buf[23] = 0x06;
    u16 tot = 40;
    buf[16] = (tot >> 8) & 0xFF; buf[17] = tot & 0xFF;
    buf[26] = (local_ip >> 24) & 0xFF; buf[27] = (local_ip >> 16) & 0xFF;
    buf[28] = (local_ip >> 8) & 0xFF; buf[29] = local_ip & 0xFF;
    buf[30] = (remote_ip >> 24) & 0xFF; buf[31] = (remote_ip >> 16) & 0xFF;
    buf[32] = (remote_ip >> 8) & 0xFF; buf[33] = remote_ip & 0xFF;
    u16 ipc = cksum_raw(buf + 14, 20);
    buf[24] = (ipc >> 8) & 0xFF; buf[25] = ipc & 0xFF;
    u8 *tcp = buf + 34;
    tcp[0] = (local_port >> 8) & 0xFF; tcp[1] = local_port & 0xFF;
    tcp[2] = (remote_port >> 8) & 0xFF; tcp[3] = remote_port & 0xFF;
    tcp[4] = (seq >> 24) & 0xFF; tcp[5] = (seq >> 16) & 0xFF;
    tcp[6] = (seq >> 8) & 0xFF; tcp[7] = seq & 0xFF;
    tcp[13] = 0x02;
    tcp[14] = 0x70; tcp[15] = 0x00;
    rtl_tx_raw(buf, 60);
}

static void send_tcp_ack(u32 local_ip, u32 remote_ip, u16 local_port, u16 remote_port, u32 seq, u32 ack) {
    (void)local_ip; (void)remote_ip; (void)local_port; (void)remote_port; (void)seq; (void)ack;
}

int net_http_get(u32 ip, const char *path, u8 *buf, u32 *sz) {
    (void)path;
    u8 mac[6];
    if (net_arp_lookup(ip, mac) != 0) {
        net_arp_request(ip);
        for (volatile int t = 0; t < 5000000; t++) {
            if (net_arp_lookup(ip, mac) == 0) break;
        }
        if (net_arp_lookup(ip, mac) != 0) return -1;
    }
    send_tcp_syn(my_ip, ip, 0x1234, PORT_HTTP, 0x1000);
    *sz = 0;
    if (buf && sz) { (void)buf; }
    return -2;
}

int net_dns_resolve(const char *host, u32 *ip) {
    (void)host;
    *ip = IP_ADDR;
    return 0;
}

int net_poll(void) {
    if (rx_ready) { rx_ready = 0; return 1; }
    u16 isr = inw(rtl_base + 0x3E);
    if (isr & 0x01) { net_rx_ring(); return 1; }
    return 0;
}
