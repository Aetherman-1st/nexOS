#include "net.h"

static u8 eth_buf[ETH_BUF_SIZE];
static u32 gw_ip = GW_ADDR;
static u32 my_ip = IP_ADDR;
static u8 mac_addr[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static u8 pkt_bufs[PKT_BUF_COUNT][PKT_BUF_SIZE];
static u8 pkt_ready = 0;

u32 net_get_gw(void) { return gw_ip; }

static u16 cksum(u16 *data, int len) {
    u32 sum = 0;
    for (int i = 0; i < len; i += 2) {
        sum += data[i];
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

static void outl(u16 port, u32 v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static u32 inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void net_init(void) {    outw(RTL8139_BASE + 0x38, 0x0000);
    outw(RTL8139_BASE + 0x3C, 0x0000);
    outl(RTL8139_BASE + 0x40, (u32)pkt_bufs[0]);
    outl(RTL8139_BASE + 0x44, (u32)pkt_bufs[1]);
    outl(RTL8139_BASE + 0x48, (u32)pkt_bufs[2]);
    outl(RTL8139_BASE + 0x4C, (u32)pkt_bufs[3]);
    outw(RTL8139_BASE + 0x38, 0xC000);
    outw(RTL8139_BASE + 0x3C, 0x2000);
}

void net_arp_request(u32 gw) {
    u8 *buf = eth_buf;
    memset(buf, 0, ETH_BUF_SIZE);

    u16 *h = (u16 *)buf;
    h[0] = 0xFFFF;
    h[1] = 0xFFFF;
    h[2] = 0x0001;
    h[3] = 0x0800;
    h[4] = 0x06;
    h[5] = 0x04;
    h[6] = 0x0001;

    u32 *ip = (u32 *)(buf + 28);
    ip[0] = my_ip;
    ip[1] = gw;

    outw(RTL8139_BASE + 0x30, (u16)((u32)buf & 0xFFFF));
    outw(RTL8139_BASE + 0x32, (u16)((u32)buf >> 16));
    outw(RTL8139_BASE + 0x34, 64);
    outw(RTL8139_BASE + 0x34, 0);
}

static void send_tcp_syn(u32 local_ip, u32 remote_ip, u16 local_port, u16 remote_port, u32 seq) {
    u8 *buf = eth_buf;
    memset(buf, 0, ETH_BUF_SIZE);

    u32 *src_ip = (u32 *)(buf + 14);
    u32 *dst_ip = (u32 *)(buf + 14 + 4);
    src_ip[0] = local_ip;
    dst_ip[0] = remote_ip;
    buf[14 + 9] = 0x06;

    u16 *tcp = (u16 *)(buf + 14 + 20);
    tcp[0] = local_port;
    tcp[1] = remote_port;
    tcp[2] = seq & 0xFFFF;
    tcp[3] = (seq >> 16) & 0xFFFF;
    tcp[4] = 0;
    tcp[5] = 0x0012;
    tcp[6] = 0x7000;
    tcp[7] = 0;
    tcp[8] = 0;

    u32 pseudo[4];
    pseudo[0] = local_ip;
    pseudo[1] = remote_ip;
    pseudo[2] = 0x06;
    pseudo[3] = 0x14;
    u16 csum_val = cksum((u16 *)pseudo, 8);
    tcp[9] = csum_val;

    outw(RTL8139_BASE + 0x30, (u16)((u32)buf & 0xFFFF));
    outw(RTL8139_BASE + 0x32, (u16)((u32)buf >> 16));
    outw(RTL8139_BASE + 0x34, 40 + 20 + 14);
}

static void send_tcp_ack(u32 local_ip, u32 remote_ip, u16 local_port, u16 remote_port, u32 seq, u32 ack) {
    u8 *buf = eth_buf;
    memset(buf, 0, ETH_BUF_SIZE);

    u32 *src_ip = (u32 *)(buf + 14);
    u32 *dst_ip = (u32 *)(buf + 14 + 4);
    src_ip[0] = local_ip;
    dst_ip[0] = remote_ip;
    buf[14 + 9] = 0x06;

    u16 *tcp = (u16 *)(buf + 14 + 20);
    tcp[0] = local_port;
    tcp[1] = remote_port;
    tcp[2] = seq & 0xFFFF;
    tcp[3] = (seq >> 16) & 0xFFFF;
    tcp[4] = ack & 0xFFFF;
    tcp[5] = (ack >> 16) & 0xFFFF;
    tcp[6] = 0x0010;
    tcp[7] = 0x7000;
    tcp[8] = 0;

    outw(RTL8139_BASE + 0x30, (u16)((u32)buf & 0xFFFF));
    outw(RTL8139_BASE + 0x32, (u16)((u32)buf >> 16));
    outw(RTL8139_BASE + 0x34, 40 + 20 + 14);
}

int net_http_get(u32 ip, const char *path, u8 *buf, u32 *sz) {
    send_tcp_syn(my_ip, ip, 0x1234, PORT_HTTP, 0x1000);
    for (int i = 0; i < 100000; i++) { u8 s = inb(RTL8139_BASE + 0x3C); (void)s; }
    send_tcp_ack(my_ip, ip, 0x1234, PORT_HTTP, 0x1000, 0x1001);

    u8 *pkt = (u8 *)"GET ";
    u8 *p = pkt;
    while (*path) *p++ = *path++;
    u8 *hdr = " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    memcpy(p, hdr, 32);
    u32 len = 4 + 32;

    memset(buf, 0, ETH_BUF_SIZE);
    u32 *src_ip = (u32 *)(buf + 14);
    u32 *dst_ip = (u32 *)(buf + 14 + 4);
    src_ip[0] = my_ip;
    dst_ip[0] = ip;
    buf[14 + 9] = 0x06;

    u16 *tcp = (u16 *)(buf + 14 + 20);
    tcp[0] = 0x1234;
    tcp[1] = PORT_HTTP;
    tcp[2] = 0x1001 & 0xFFFF;
    tcp[3] = (0x1001 >> 16) & 0xFFFF;
    tcp[4] = 0x1001 & 0xFFFF;
    tcp[5] = (0x1001 >> 16) & 0xFFFF;
    tcp[6] = 0x0018;
    tcp[7] = 0x7000;

    *sz = len;
    return 0;
}

int net_dns_resolve(const char *host, u32 *ip) {
    *ip = IP_ADDR;
    return 0;
}

int net_poll(void) {
    u16 status = inw(RTL8139_BASE + 0x3C);
    if (status & 0x2000) {
        pkt_ready = 1;
        return 1;
    }
    return 0;
}
