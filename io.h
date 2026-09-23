#ifndef IO_H
#define IO_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outw(u16 port, u16 v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline void insw(u16 port, u16 *buf, int cnt) {
    __asm__ volatile("cld; rep; insw" : "+D"(buf), "+c"(cnt) : "d"(port) : "memory");
}

static inline void outsw(u16 port, const u16 *buf, int cnt) {
    __asm__ volatile("cld; rep; outsw" : "+S"(buf), "+c"(cnt) : "d"(port) : "memory");
}

static inline void outl(u16 port, u32 v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void memset(void *s, int c, u32 n) {
    u8 *p = (u8 *)s;
    for (u32 i = 0; i < n; i++) p[i] = (u8)c;
}

static inline void memcpy(void *dst, const void *src, u32 n) {
    u8 *d = (u8 *)dst;
    u8 *s = (u8 *)src;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(u8 *)a - *(u8 *)b;
}

static inline int strncmp(const char *a, const char *b, u32 n) {
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == 0) return (u8)a[i] - (u8)b[i];
    }
    return 0;
}

static inline u32 strlen(const char *s) {
    u32 l = 0;
    while (s[l]) l++;
    return l;
}

static inline void *memset_ptr(void *s, int c, u32 n) {
    u8 *p = (u8 *)s;
    for (u32 i = 0; i < n; i++) p[i] = (u8)c;
    return s;
}

#define MEMSET(s, c, n) memset((void *)(s), (c), (n))

static inline void strcat(char *dst, const char *src) {
    u32 i = strlen(dst);
    u32 j = 0;
    while (src[j]) dst[i++] = src[j++];
    dst[i] = 0;
}

static inline char *strcpy(char *dst, const char *src) {
    u32 i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

#endif
