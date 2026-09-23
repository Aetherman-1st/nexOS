#ifndef NET_H
#define NET_H

#include "io.h"

#define RTL8139_BASE 0x300
#define RTL8139_IRQ 11
#define RTL8139_MEM 0xFFF0

#define PKT_BUF_SIZE 1536
#define PKT_BUF_COUNT 4
#define ETH_BUF_SIZE 2048

#define IP_ADDR 0x5DB8D822
#define GW_ADDR  0x0A000202
#define DNS_ADDR 0x0A000203

#define PORT_HTTP 80
#define PORT_DNS  53

#define ETH_HDR_SIZE 14
#define IP_HDR_SIZE 20
#define TCP_HDR_SIZE 20
#define UDP_HDR_SIZE 8

struct tcp_sock {
    u32 local_ip;
    u32 remote_ip;
    u16 local_port;
    u16 remote_port;
    u32 seq;
    u32 ack;
    u8 state;
    u8 buf[2048];
    u32 buf_len;
};

void net_init(void);
u32 net_get_gw(void);
void net_arp_request(u32 gw);
int net_http_get(u32 ip, const char *path, u8 *buf, u32 *sz);
int net_dns_resolve(const char *host, u32 *ip);
void net_poll(void);

#endif
