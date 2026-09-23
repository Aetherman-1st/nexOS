	/*
 * nexOS - a totally real operating system.
 * written by Yazeed Omari, one bug at a time.
 * if you're reading this you either found the source, or you're lost.
 * either way, welcome. good luck understanding it. i don't.
 */

typedef   unsigned	int	u32   ;
    typedef  unsigned short	u16  ;
typedef  unsigned	char  u8 ;

#include <stdarg.h>
#include "ata.h"
#include "fat32.h"
#include "net.h"
#include "pci.h"
#include "acpi.h"
#include "usb.h"

extern fat32_fs_t g_fs;
extern u8 g_disk_buf[];
extern int g_disk_init;

static volatile u32 pit_ticks;
static u32 switches;

         void   main	(	void	) ;

/* ── VBE boot parameter block at 0x5000 ── */
#define VBE_LFB       (*(u32 volatile *)0x5000)
#define VBE_WIDTH     (*(u16 volatile *)0x5004)
#define VBE_HEIGHT    (*(u16 volatile *)0x5006)
#define VBE_PITCH     (*(u16 volatile *)0x5008)
#define VBE_BPP       (*(u8  volatile *)0x500A)
#define VBE_RED_POS   (*(u8  volatile *)0x500B)
#define VBE_GRN_POS   (*(u8  volatile *)0x500C)
#define VBE_BLU_POS   (*(u8  volatile *)0x500D)

/* ── Paging & Memory Allocator ── */

#define PAGE_SIZE 0x1000
#define NUM_PAGES 3840
#define HEAP_START_PAGE 1024

static u32 page_directory[1024] __attribute__((aligned(4096)));
static u32 pt0[1024] __attribute__((aligned(4096)));
static u32 pt1[1024] __attribute__((aligned(4096)));
static u32 pt2[1024] __attribute__((aligned(4096)));
static u32 pt3[1024] __attribute__((aligned(4096)));
static u32 pt_lfb[1024] __attribute__((aligned(4096)));
static u32 pt_win0[1024] __attribute__((aligned(4096)));
static u32 pt_win1[1024] __attribute__((aligned(4096)));
#define WIN0_VADDR 0x1000000
#define WIN1_VADDR 0x1400000
static u8 mem_bitmap[NUM_PAGES / 8] __attribute__((aligned(4096)));

static void page_init(void) {
    for (int i = 0; i < 1024; i++) {
        pt0[i] = (i * PAGE_SIZE) | 0x03;
        pt1[i] = (0x400000 + i * PAGE_SIZE) | 0x03;
        pt2[i] = (0x800000 + i * PAGE_SIZE) | 0x03;
        pt3[i] = (0xC00000 + i * PAGE_SIZE) | 0x03;
        pt_lfb[i] = 0;
        page_directory[i] = 0;
    }
    page_directory[0] = ((u32)pt0) | 0x03;
    page_directory[1] = ((u32)pt1) | 0x03;
    page_directory[2] = ((u32)pt2) | 0x03;
    page_directory[3] = ((u32)pt3) | 0x03;

    u32 vbe_lfb = (u32)VBE_LFB;
    if (vbe_lfb >= 0x1000000) {
        u32 base = vbe_lfb & 0xFFC00000;
        u32 dir = (vbe_lfb >> 22) & 0x3FF;
        for (int i = 0; i < 1024; i++) pt_lfb[i] = (base + i * PAGE_SIZE) | 0x03;
        page_directory[dir] = ((u32)pt_lfb) | 0x03;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"((u32)page_directory));
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

	void *map_window(int which, u32 phys) {
	u32 base = phys & 0xFFC00000;
	u32 *pt = which ? pt_win1 : pt_win0;
	u32 dir = which ? 5 : 4;
	for (int i = 0; i < 1024; i++) pt[i] = (base + i * PAGE_SIZE) | 0x03;
	page_directory[dir] = ((u32)pt) | 0x03;
	u32 cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	__asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
	return (void *)(WIN0_VADDR + (which ? 0x400000 : 0) + (phys - base));
	}

static void mem_init(void) {
    for (int i = 0; i < NUM_PAGES / 8; i++) mem_bitmap[i] = 0;
    for (int i = 0; i < HEAP_START_PAGE; i++) {
        mem_bitmap[i / 8] |= (1 << (i % 8));
    }
}

static void *kmalloc(u32 size) {
    u32 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;
    for (int i = HEAP_START_PAGE; i + (int)pages <= NUM_PAGES; i++) {
        u32 j;
        for (j = 0; j < pages; j++) {
            if (mem_bitmap[(i + j) / 8] & (1 << ((i + j) % 8))) break;
        }
        if (j == pages) {
            for (j = 0; j < pages; j++)
                mem_bitmap[(i + j) / 8] |= (1 << ((i + j) % 8));
            return (void *)(i * PAGE_SIZE);
        }
    }
    return 0;
}

static void kfree(void *ptr) {
    if (!ptr) return;
    u32 addr = (u32)ptr;
    u32 page = addr / PAGE_SIZE;
    if (page < HEAP_START_PAGE || page >= NUM_PAGES) return;
    mem_bitmap[page / 8] &= ~(1 << (page % 8));
}

	typedef struct blk { u32 size; int free; struct blk *next; } blk_t;
	static blk_t *heap_head = 0;

	static void *malloc(u32 size) {
	size = (size + 7) & ~7u;
	if (size == 0) size = 8;
	for (blk_t *b = heap_head; b; b = b->next) {
	    if (b->free && b->size >= size) {
	        if (b->size >= size + sizeof(blk_t) + 8) {
	            blk_t *nb = (blk_t *)((u8 *)b + sizeof(blk_t) + size);
	            nb->size = b->size - size - sizeof(blk_t);
	            nb->free = 1;
	            nb->next = b->next;
	            b->size = size;
	            b->next = nb;
	        }
	        b->free = 0;
	        return (u8 *)b + sizeof(blk_t);
	    }
	}
	u32 need = size + sizeof(blk_t);
	u32 pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;
	u8 *mem = (u8 *)kmalloc(pages * PAGE_SIZE);
	if (!mem) return 0;
	blk_t *b = (blk_t *)mem;
	b->size = pages * PAGE_SIZE - sizeof(blk_t);
	b->free = 0;
	b->next = heap_head;
	heap_head = b;
	if (b->size >= size + sizeof(blk_t) + 8) {
	    blk_t *nb = (blk_t *)((u8 *)b + sizeof(blk_t) + size);
	    nb->size = b->size - size - sizeof(blk_t);
	    nb->free = 1;
	    nb->next = b->next;
	    b->size = size;
	    b->next = nb;
	}
	return (u8 *)b + sizeof(blk_t);
	}

	static void free(void *ptr) {
	if (!ptr) return;
	blk_t *b = (blk_t *)((u8 *)ptr - sizeof(blk_t));
	b->free = 1;
	for (blk_t *c = heap_head; c && c->next; ) {
	    blk_t *n = c->next;
	    if (c->free && n->free &&
	        (u8 *)c + sizeof(blk_t) + c->size == (u8 *)n) {
	        c->size += sizeof(blk_t) + n->size;
	        c->next = n->next;
	    } else c = n;
	}
	}


/* ── String formatting ── */

static int int_to_str(u32 n, char *buf, u32 base) {
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return 1; }
    char tmp[32]; int len = 0;
    while (n) { tmp[len++] = "0123456789ABCDEF"[n % base]; n /= base; }
    for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
    buf[len] = 0;
    return len;
}

static int snprintf(char *buf, u32 size, const char *fmt, ...) {
    int pos = 0;
    va_list args; va_start(args, fmt);
    for (int i = 0; fmt[i] && pos < (int)size - 1; i++) {
        if (fmt[i] == '%') {
            i++;
            if (fmt[i] == 'x') {
                u32 v = va_arg(args, u32);
                pos += int_to_str(v, buf + pos, 16);
            } else if (fmt[i] == 'd') {
                int v = va_arg(args, int);
                if (v < 0) { buf[pos++] = '-'; v = -v; }
                pos += int_to_str((u32)v, buf + pos, 10);
            } else if (fmt[i] == 's') {
                const char *s = va_arg(args, const char *);
                while (*s && pos < (int)size - 1) buf[pos++] = *s++;
            } else if (fmt[i] == 'c') {
                buf[pos++] = (char)va_arg(args, int);
            } else {
                buf[pos++] = fmt[i];
            }
        } else {
            buf[pos++] = fmt[i];
        }
    }
    buf[pos] = 0;
    va_end(args);
    return pos;
}

  static  u8	*	lfb   ;
        static   u8 *   front_lfb	;
		/* Reserved RAM below the usual kernel/stack area for an off-screen frame. */
static	u8	*	backbuf	=	0	;
static   int  scr_w  ,   scr_h , pitch ,	bpp ,	bpp_bytes ;
 static int   r_pos  ,	g_pos   ,   b_pos	;

	static	void	vbe_init  (	void   )   {
	lfb  =   (	u8	*	) VBE_LFB ;
      front_lfb =	lfb  ;
scr_w =  VBE_WIDTH  ;
scr_h = VBE_HEIGHT ;
       pitch	=   VBE_PITCH   ;
       bpp   =  VBE_BPP   ;
    bpp_bytes = bpp	/  8 ;
     if   (	bpp_bytes	<  1 ) bpp_bytes  =  1 ;
		r_pos = VBE_RED_POS	;
 g_pos =  VBE_GRN_POS   ;
b_pos   = VBE_BLU_POS	;
       }

	/* ── Drawing — use color mask positions from VESA mode info ── */
static   void  draw_pixel (	int   x ,   int   y  ,   u32 color	)   {
if (  x   <  0	|| x >= scr_w  || y	<   0	||  y   >=	scr_h  )  return ;
      u8  *  base	=  lfb +	y	*   pitch  ;
       u8	r  = (	color   >>  16 )  &	0xFF  ,	g   = (	color  >>   8  )	&	0xFF ,	b =  color   &  0xFF   ;
  switch (	bpp	)  {
    case   32 :
	case  24   : {
		int   off   = x *	bpp_bytes	;
	u32  p  =  (	(  u32   ) r  <<	r_pos  ) |   ( (  u32	) g   <<  g_pos  )   | ( (  u32 )   b	<<  b_pos	)  ;
base  [  off	]   =  p & 0xFF   ;
     base [   off  +	1   ]  =	(   p   >> 8 )  & 0xFF	;
		base	[   off   + 2	]  =   (	p  >>	16  ) &	0xFF ;
		break   ;
       }
		case  16   :	{
   u16  p =  (	u16  )	(	( (	color  >>   8	)	&	0xF800  )	|
  (   (  color  >>	5   ) &	0x07E0   )  |
( (	color	>>	3	) &  0x001F  ) )   ;
		* (  u16   *	)	(  base	+	x  *	2  )	=   p	;
        break ;
	}
 case   8 :
  base [	x   ] = ( u8 )  (	color &	0xFF	)	;
    break ;
    }
	}

	static  u32   read_pixel  (  int   x   , int	y   ) {
        if ( x   <  0 ||  x	>=	scr_w ||	y  <   0 ||  y >= scr_h ) return 0  ;
		u8  * base	= lfb + y *	pitch ;
 int  off   =  x	* bpp_bytes ;
		return (   u32	)	base [	off  ]	|   ( (   u32 ) base  [	off  +   1   ]   <<	8	)   |	(  (	u32	)  base   [	off	+   2  ]	<<   16   )	;
		}

static  void fill_rect   (	int  x  , int  y   , int	w	,	int	h   , u32 color	)	{
/* Dragging used to repaint the whole screen one pixel at a time.  On
       QEMU that is slow enough to expose every partial frame as flicker.
       Write complete scanlines for the common 32-bit VBE mode instead. */
int x2   =   x	+   w   ;  if  (   x2  >   scr_w )   x2 =  scr_w   ;
	int	y2  = y   +	h   ; if  (  y2 >   scr_h  ) y2 =  scr_h   ;
if	(	x <   0 )  x	=	0  ;
if   (   y	<   0   ) y   = 0  ;
 if (  x	>=   x2 ||   y	>=  y2  )   return ;

		/* 32bpp gets the fast lane. everyone else can suffer in per-pixel hell. */
	if	(  bpp ==  32   ) {
u32 pixel =	(	(	color	>>  16	)   & 0xFF	)	<<  r_pos  |
	(	( color	>>  8  ) &  0xFF	)   <<	g_pos |
   (	color   &  0xFF  )   <<	b_pos  ;
       for   (	int	row	=  y  ;  row  < y2 ;	row  ++  )   {
       u32	*	dst =   (	u32	*	) ( lfb	+  row *	pitch   +   x   *  4  )	;
        for (	int   col  = x  ;  col	<	x2  ; col   ++   )	* dst   ++   = pixel	;
        }
  return   ;
		}

		/* 24bpp fast lane: pack once, then blast 3 bytes per pixel
		   with no per-pixel call, bounds check, or mode switch. */
	if	(  bpp ==  24   ) {
	u8 r  = (	color   >>  16 )  &	0xFF  ,	g   = (	color  >>   8  )	&	0xFF ,	b =  color   &  0xFF   ;
	u32  p  =  (	(  u32   ) r  <<	r_pos  ) |   ( (  u32	) g   <<  g_pos  )   | ( (  u32 )   b	<<  b_pos	)  ;
	u8 b0 = p & 0xFF, b1 = (p >> 8) & 0xFF, b2 = (p >> 16) & 0xFF;
        for   (	int	row	=  y  ;  row  < y2 ;	row  ++  )   {
       u8	*	dst =   lfb	+  row *	pitch   +   x   *  3  ;
        for (	int   col  = x  ;  col	<	x2  ; col   ++   )	{
		dst[0] = b0; dst[1] = b1; dst[2] = b2; dst += 3;
	}
        }
  return   ;
		}

		for  (	int row = y   ;	row  < y2   ;   row ++  )
        for (	int  col =  x ;  col  < x2   ;  col ++ )  draw_pixel	(   col	,  row   ,   color )   ;
      }

static	void   clear_screen ( u32 color   )	{
fill_rect (   0 , 0   ,  scr_w ,  scr_h   , color   )   ;
        }

/* ── Softer shapes: rounded rects, gradients, dithered shadows ── */
	static int  isqrt_int ( int n ) {
	int r = 0;
	while ((r + 1) * (r + 1) <= n) r++;
	return r;
	}

	static u32 mix_color(u32 a, u32 b, int t) {
	int r = (((a >> 16) & 0xFF) * (256 - t) + ((b >> 16) & 0xFF) * t) >> 8;
	int g = (((a >> 8) & 0xFF) * (256 - t) + ((b >> 8) & 0xFF) * t) >> 8;
	int bl = ((a & 0xFF) * (256 - t) + (b & 0xFF) * t) >> 8;
	return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
	}

	static void fill_rounded(int x, int y, int w, int h, int r, u32 color) {
	if (r < 1) { fill_rect(x, y, w, h, color); return; }
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	for (int row = 0; row < h; row++) {
	    int dy = row < r ? r - 1 - row : (row >= h - r ? row - (h - r) : -1);
	    int x0 = x, x1 = x + w;
	    if (dy >= 0) {
	        int dx = r - isqrt_int(r * r - dy * dy);
	        x0 = x + dx; x1 = x + w - dx;
	    }
	    if (x1 > x0) fill_rect(x0, y + row, x1 - x0, 1, color);
	}
	}

	static void fill_vgrad(int x, int y, int w, int h, u32 top, u32 bottom) {
	if (h < 1) return;
	for (int row = 0; row < h; row++) {
	    int t = h < 2 ? 0 : (row * 255) / (h - 1);
	    fill_rect(x, y + row, w, 1, mix_color(top, bottom, t));
	}
	}

	static void draw_shadow(int x, int y, int w, int h) {
	for (int row = 0; row < h; row++) {
	    for (int col = 0; col < w; col++) {
	        if ( ((row + col) & 1) == 0 ) continue;
	        int px = x + 6 + col, py = y + 8 + row;
	        if (px < 0 || py < 0 || px >= scr_w || py >= scr_h) continue;
	        u32 bg = read_pixel(px, py);
	        draw_pixel(px, py, mix_color(bg, 0x000000, 110));
	    }
	}
	}

/* ── 8x8 bitmap font ── */
    static	const  u8	font_unk [  8  ]  =	{	0xFF	,   0x81	,  0x81 , 0x81   ,   0x81  ,  0x81	,	0x81  ,	0xFF  } ;
		static	const   u8	font_S   [	8   ]	=	{   0x3C ,   0x66  ,  0x60   , 0x3C	,  0x06  ,	0x66  ,   0x3C   ,	0x00   }  ;
	static  const u8   font_T   [  8  ]  =	{	0x7E	,  0x18 ,  0x18  ,  0x18	,   0x18	,	0x18  ,  0x18 ,	0x00   }	;
static	const   u8 font_A [   8 ] =	{ 0x18   ,   0x3C	, 0x66   , 0x7E ,	0x66	,   0x66   ,   0x66  ,   0x00   } ;
	static  const	u8   font_R   [  8  ]   =	{ 0x7C ,	0x66  ,  0x66   , 0x7C	,  0x78 ,   0x6C ,  0x66	,	0x00 }   ;
static const  u8	font_n [   8   ]	=   { 0x00   , 0x00 ,  0x66	,	0x66 ,  0x66	, 0x66	,   0x66   , 0x00  } ;
     static	const	u8 font_E	[ 8	]	= {   0x7E   ,   0x60  ,  0x60  ,  0x7C , 0x60 , 0x60   ,   0x7E  , 0x00 }	;
	static   const u8	font_X  [	8	]  =  { 0x66	,	0x66  ,   0x3C ,  0x18 , 0x3C  ,   0x66  ,   0x66	,	0x00  } ;
        static const	u8  font_O [   8  ]  =	{  0x3C  ,   0x66   ,   0x66 ,	0x66 , 0x66   ,  0x66 ,   0x3C  , 0x00   } ;
      static   const	u8	font_v   [   8 ]  =	{ 0x00  ,	0x00  ,	0x66 ,	0x66  ,	0x66   ,  0x3C	,  0x18	,	0x00 } ;
      static  const   u8 font_0   [ 8   ]	=	{	0x3C   ,	0x66   , 0x6E	,   0x7E , 0x76   , 0x66 ,  0x3C   ,  0x00	} ;
		static const u8 font_1  [ 8	]	= {	0x18	,	0x38  ,  0x18	, 0x18  ,  0x18 ,  0x18 ,   0x7E ,	0x00   }	;
 static	const u8  font_dot [	8   ]   =   {   0x00  ,	0x00  , 0x00	,	0x00   ,   0x00	,   0x18  , 0x18  , 0x00   }	;
		static	const  u8	font_e	[  8	]	=  { 0x00	,  0x00 ,	0x3C  , 0x06 ,   0x3E   ,  0x66 ,	0x3C	,	0x00   }	;
     static	const   u8	font_x	[  8  ]	=	{ 0x00  ,   0x00  , 0x66	,	0x3C  ,	0x18 ,	0x3C  , 0x66   ,	0x00   }  ;
        static  const	u8 font_I  [  8  ]	=   {	0x7E ,	0x18 ,	0x18 ,   0x18   ,	0x18 ,	0x18 ,  0x7E ,   0x00	}	;
     static   const  u8	font_N   [  8	] = { 0x66	,  0x66  , 0x76  , 0x7E ,   0x6E  ,   0x66	,   0x66	,	0x00   }  ;
        static const	u8   font_U [	8   ]   =	{   0x66  ,   0x66  , 0x66   ,   0x66	,   0x66	,	0x66  , 0x3C ,  0x00  } ;
   static	const   u8  font_L  [ 8	]	= {	0x60 ,  0x60	,	0x60 ,  0x60	, 0x60 ,	0x60	,	0x7E  ,  0x00	}  ;
static	const u8   font_D	[   8 ]  =   {	0x7C	, 0x66 , 0x66   ,  0x66   ,  0x66 ,  0x66   ,  0x7C	,   0x00 }	;
	static  const  u8 font_W  [   8	] = {   0x63 ,  0x63  ,	0x63   ,  0x6B   ,  0x7F	,   0x77  ,	0x63	,   0x00  }	;
		static  const u8  font_space	[	8  ]  =	{	0x00   , 0x00  ,  0x00	, 0x00   ,	0x00   ,   0x00   ,   0x00 ,  0x00	}  ;

static   const	u8   font_B [  8   ]   =	{  0x7C	, 0x62	, 0x62  ,	0x7C , 0x62  ,	0x62 , 0x7C  ,   0x00   }   ;
  static const   u8	font_C	[ 8	]   =   {	0x3C	,	0x62	,	0x60 ,   0x60	,  0x60 ,  0x62  , 0x3C  ,  0x00   }	;
    static  const   u8  font_F	[   8	] = {  0x7E  , 0x60	, 0x60   ,  0x7C	,  0x60   ,	0x60  ,   0x60   ,  0x00	}   ;
static  const u8	font_G [   8	]   =  { 0x3C ,	0x62  ,  0x60  ,	0x6E ,	0x62	,	0x62  ,	0x3C	,	0x00  }	;
		static	const	u8   font_H	[  8 ] =	{  0x62	,	0x62  ,	0x62   ,   0x7E	, 0x62  ,   0x62	, 0x62	,	0x00 }   ;
static	const	u8   font_J [ 8   ]	= {	0x1E ,  0x0C ,	0x0C ,	0x0C	,	0x0C   ,  0x6C	,	0x38  ,  0x00 }	;
        static const   u8 font_K [   8 ] =	{ 0x62  ,  0x64  ,   0x68   ,	0x70  ,   0x68 ,   0x64   ,   0x62   ,   0x00   }	;
 static	const   u8  font_M [   8 ] =	{	0x63  ,   0x77 ,  0x7F   ,	0x6B  ,  0x63 ,  0x63  ,  0x63	,  0x00  }	;
    static const	u8	font_P [	8 ]	=	{ 0x7C ,	0x62 ,	0x62  , 0x7C   ,	0x60	, 0x60	,  0x60  ,  0x00   } ;
 static  const u8 font_Q   [ 8 ]	=	{	0x3C , 0x62  , 0x62 ,	0x62   , 0x6A	, 0x64 ,	0x3A	,  0x00   }   ;
static	const   u8 font_V	[	8	]   =  {   0x63 ,	0x63   ,   0x63  , 0x63  ,   0x63	,	0x3E  ,  0x1C  ,   0x00 }  ;
        static   const	u8	font_Y [	8  ]  =	{  0x63	,   0x63  ,	0x63   ,	0x3E ,	0x1C  , 0x1C   ,  0x1C  , 0x00   } ;
       static	const u8   font_Z [   8   ]	=  {	0x7E  ,   0x06  ,	0x0C   ,	0x18   ,	0x30   ,	0x60	,   0x7E  ,   0x00  }	;
static const u8 font_2 [ 8 ] = { 0x3C , 0x66 , 0x06 , 0x0C , 0x18 , 0x30 , 0x7E , 0x00 } ;
static const u8 font_3 [ 8 ] = { 0x3C , 0x66 , 0x06 , 0x1C , 0x06 , 0x66 , 0x3C , 0x00 } ;
static const u8 font_4 [ 8 ] = { 0x0C , 0x1C , 0x3C , 0x6C , 0x7E , 0x0C , 0x0C , 0x00 } ;
static const u8 font_5 [ 8 ] = { 0x7E , 0x60 , 0x7C , 0x06 , 0x06 , 0x66 , 0x3C , 0x00 } ;
static const u8 font_6 [ 8 ] = { 0x3C , 0x60 , 0x60 , 0x7C , 0x66 , 0x66 , 0x3C , 0x00 } ;
static const u8 font_7 [ 8 ] = { 0x7E , 0x66 , 0x0C , 0x18 , 0x18 , 0x18 , 0x18 , 0x00 } ;
static const u8 font_8 [ 8 ] = { 0x3C , 0x66 , 0x66 , 0x3C , 0x66 , 0x66 , 0x3C , 0x00 } ;
static const u8 font_9 [ 8 ] = { 0x3C , 0x66 , 0x66 , 0x3E , 0x06 , 0x06 , 0x3C , 0x00 } ;
static const u8 font_bang [ 8 ] = { 0x18 , 0x18 , 0x18 , 0x18 , 0x18 , 0x00 , 0x18 , 0x00 } ;
static const u8 font_at [ 8 ] = { 0x3C , 0x66 , 0x6E , 0x6A , 0x6E , 0x60 , 0x3C , 0x00 } ;
static const u8 font_hash [ 8 ] = { 0x6C , 0x6C , 0xFE , 0x6C , 0xFE , 0x6C , 0x6C , 0x00 } ;
static const u8 font_dollar [ 8 ] = { 0x18 , 0x3E , 0x60 , 0x3C , 0x06 , 0x7C , 0x18 , 0x00 } ;
static const u8 font_pct [ 8 ] = { 0x62 , 0x64 , 0x08 , 0x10 , 0x20 , 0x26 , 0x46 , 0x00 } ;
static const u8 font_caret [ 8 ] = { 0x18 , 0x3C , 0x66 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 } ;
static const u8 font_amp [ 8 ] = { 0x36 , 0x49 , 0x55 , 0x22 , 0x50 , 0x48 , 0x34 , 0x00 } ;
static const u8 font_star [ 8 ] = { 0x00 , 0x66 , 0x3C , 0xFF , 0x3C , 0x66 , 0x00 , 0x00 } ;
static const u8 font_lparen [ 8 ] = { 0x0C , 0x18 , 0x30 , 0x30 , 0x30 , 0x18 , 0x0C , 0x00 } ;
static const u8 font_rparen [ 8 ] = { 0x30 , 0x18 , 0x0C , 0x0C , 0x0C , 0x18 , 0x30 , 0x00 } ;
static const u8 font_minus [ 8 ] = { 0x00 , 0x00 , 0x00 , 0x7E , 0x00 , 0x00 , 0x00 , 0x00 } ;
static const u8 font_eq [ 8 ] = { 0x00 , 0x00 , 0x7E , 0x00 , 0x7E , 0x00 , 0x00 , 0x00 } ;
static const u8 font_lbrk [ 8 ] = { 0x3C , 0x30 , 0x30 , 0x30 , 0x30 , 0x30 , 0x3C , 0x00 } ;
static const u8 font_rbrk [ 8 ] = { 0x3C , 0x0C , 0x0C , 0x0C , 0x0C , 0x0C , 0x3C , 0x00 } ;
static const u8 font_semi [ 8 ] = { 0x00 , 0x18 , 0x18 , 0x00 , 0x18 , 0x18 , 0x30 , 0x00 } ;
static const u8 font_quote [ 8 ] = { 0x18 , 0x18 , 0x30 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 } ;
static const u8 font_btick [ 8 ] = { 0x30 , 0x18 , 0x0C , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 } ;
static const u8 font_bslash [ 8 ] = { 0x40 , 0x60 , 0x30 , 0x18 , 0x0C , 0x06 , 0x02 , 0x00 } ;
static const u8 font_comma [ 8 ] = { 0x00 , 0x00 , 0x00 , 0x00 , 0x18 , 0x18 , 0x30 , 0x00 } ;
static const u8 font_slash [ 8 ] = { 0x02 , 0x06 , 0x0C , 0x18 , 0x30 , 0x60 , 0x40 , 0x00 } ;
static const u8 font_colon [ 8 ] = { 0x00 , 0x18 , 0x18 , 0x00 , 0x18 , 0x18 , 0x00 , 0x00 } ;
static const u8 font_plus [ 8 ] = { 0x00 , 0x18 , 0x18 , 0x7E , 0x18 , 0x18 , 0x00 , 0x00 } ;
static const u8 font_lt [ 8 ] = { 0x0C , 0x18 , 0x30 , 0x60 , 0x30 , 0x18 , 0x0C , 0x00 } ;
static const u8 font_gt [ 8 ] = { 0x30 , 0x18 , 0x0C , 0x06 , 0x0C , 0x18 , 0x30 , 0x00 } ;
static const u8 font_quest [ 8 ] = { 0x3C , 0x66 , 0x06 , 0x0C , 0x18 , 0x00 , 0x18 , 0x00 } ;
static const u8 font_dquote [ 8 ] = { 0x66 , 0x66 , 0x24 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 } ;
static const u8 font_uscore [ 8 ] = { 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x7E , 0x00 , 0x00 } ;
static const u8 font_lbrace [ 8 ] = { 0x0E , 0x18 , 0x18 , 0x70 , 0x18 , 0x18 , 0x0E , 0x00 } ;
static const u8 font_rbrace [ 8 ] = { 0x70 , 0x18 , 0x18 , 0x0E , 0x18 , 0x18 , 0x70 , 0x00 } ;
static const u8 font_pipe [ 8 ] = { 0x18 , 0x18 , 0x18 , 0x18 , 0x18 , 0x18 , 0x18 , 0x00 } ;

      static  const	u8 *   font_map   [ 256   ]	;
      static void init_font   ( void   )   {
	for (	int i =	0  ;  i <  256  ; i	++  )  font_map	[  i ]	=  font_unk ;
	font_map	[	'A'   ]	=   font_A  ;   font_map	[  'B'	]  = font_B	;  font_map  [  'C'  ]   =	font_C  ;
       font_map [ 'D'  ]	=	font_D   ;   font_map	[  'E'	] =	font_E	;  font_map [ 'F' ]  =	font_F  ;
font_map [  'G'   ]   =	font_G	;   font_map [  'H'	] =	font_H  ;  font_map [	'I'   ] = font_I	;
		font_map	[  'J'  ]	=  font_J  ;	font_map	[ 'K'	]	=   font_K   ;   font_map   [	'L' ]	= font_L  ;
  font_map  [ 'M' ]	=  font_M	;   font_map	[	'N'  ]  =	font_N	;   font_map   [ 'O'   ]	=	font_O	;
      font_map  [   'P'  ]	=   font_P ; font_map  [ 'Q'	]   =   font_Q ;   font_map  [  'R' ] = font_R	;
		font_map	[	'S'	]	= font_S ; font_map   [   'T'  ]   =  font_T  ;   font_map   [ 'U' ]   =   font_U ;
        font_map   [   'V'  ]	=	font_V ;	font_map   [   'W'  ]  =   font_W ;	font_map	[ 'X' ]   = font_X  ;
 font_map	[	'Y'   ]  =	font_Y  ;  font_map [  'Z'	] =   font_Z   ;
  font_map  [  '.'	]  =	font_dot ;
		font_map [	' ' ]  =  font_space ;
 for  ( int   c   =	'a'  ;  c   <=	'z'  ;	c   ++ )
    font_map	[	c   ]	=  font_map  [   c   - 32	]   ;
font_map [ '0' ] = font_0 ; font_map [ '1' ] = font_1 ;
font_map [ '2' ] = font_2 ; font_map [ '3' ] = font_3 ;
font_map [ '4' ] = font_4 ; font_map [ '5' ] = font_5 ;
font_map [ '6' ] = font_6 ; font_map [ '7' ] = font_7 ;
font_map [ '8' ] = font_8 ; font_map [ '9' ] = font_9 ;
font_map [ '!' ] = font_bang ; font_map [ '@' ] = font_at ;
font_map [ '#' ] = font_hash ; font_map [ '$' ] = font_dollar ;
font_map [ '%' ] = font_pct ; font_map [ '^' ] = font_caret ;
font_map [ '&' ] = font_amp ; font_map [ '*' ] = font_star ;
font_map [ '(' ] = font_lparen ; font_map [ ')' ] = font_rparen ;
font_map [ '-' ] = font_minus ; font_map [ '=' ] = font_eq ;
font_map [ '[' ] = font_lbrk ; font_map [ ']' ] = font_rbrk ;
font_map [ ';' ] = font_semi ;
font_map [ '\'' ] = font_quote ;
font_map [ '\\' ] = font_bslash ;
font_map [ '"' ] = font_dquote ;
font_map [ '`' ] = font_btick ;
font_map [ ',' ] = font_comma ; font_map [ '/' ] = font_slash ;
font_map [ ':' ] = font_colon ; font_map [ '+' ] = font_plus ;
font_map [ '<' ] = font_lt ; font_map [ '>' ] = font_gt ;
font_map [ '?' ] = font_quest ;
font_map [ '_' ] = font_uscore ;
font_map [ '{' ] = font_lbrace ; font_map [ '}' ] = font_rbrace ;
font_map [ '|' ] = font_pipe ;
     }

   static	void   draw_char   (  int   x   , int   y	,  const	u8	*	bitmap  , u32	color   )   {
if  (  !  bitmap	)   bitmap =   font_unk	;
	for  (   int	row  =   0	;   row	< 8	;  row	++   ) {
	u8 byte =	bitmap [   row  ] ;
   for	(   int	col = 0  ;  col <	8 ; col ++  )
	if	( (   byte >>	(   7  -  col   )	)	&   1	)
       draw_pixel	( x	+	col	, y	+	row  , color   ) ;
	}
}

       static   void  draw_str	(	int   x   ,  int  y   ,  const	char *	s	,  u32 color )	{
		while	(  *	s   )   {
		draw_char   (	x ,  y ,	font_map   [	( u8	)	*	s  ]   ,  color )  ;
	x	+=   10   ;	s	++	;
		}
     }

		/* ── 3D border ── */
static void  draw_3d_border	(	int  x ,  int	y  ,	int  w	, int  h ,	u32   base   ,  u32	light  ,	u32	dark  ) {
 fill_rect (   x ,  y   , w	,	h ,	base	)  ;
        for  (   int	i	=	0	;	i   <   w   ;	i   ++	) {
draw_pixel  (  x  +   i   ,	y   ,   light  )	;
        draw_pixel (   x  + i  ,	y	+ h - 1	,  dark  ) ;
		}
       for  (   int	i	=   0 ;  i	< h	;	i	++  )   {
draw_pixel  (   x   ,   y  + i ,  light	)  ;
  draw_pixel   (	x  + w   - 1  ,  y  +	i   ,	dark  ) ;
	}
  }

#define COL_BG      0x001E1E2E
#define COL_TASKBAR 0x00181825
#define COL_TITLE   0x00313244
#define COL_WINBG   0x00313244
#define COL_BTN     0x0045475A
#define COL_ACCENT  0x0089B4FA
#define COL_MUTED   0x006C7086
#define COL_WHITE   0x00CDD6F4
#define COL_TEXT    0x00CDD6F4
#define COL_BLACK   0x00CDD6F4
#define COL_SCRN    0x00000000
#define COL_RED     0x00F38BA8
#define COL_TERMBG  0x001E1E2E
#define COL_TERMFG  0x00CDD6F4

static int	win_x   ,  win_y  ,  win_w =	500   , win_h =   350  ;
static int	win_open	=	1	;
static int browser_open = 0;
static int painted_browser_open = 0;
static int browser_x = 100, browser_y = 80, browser_w = 400, browser_h = 300;
static	int   drag_target_x   ,	drag_target_y	;
        static int  dragging  =	0	, drag_off_x   , drag_off_y   ;
static int drag_win = 0;
        static   int	tb_h   = 40 ;
    static char text_buf	[ 512   ]  ;
		static	int  text_len =  0 ;
	static volatile  int	text_updated =   0   ;
  static	int  menu_open  =   0	;
		static int	paint_open	=	0	;
static int  terminal_open	=   0  ;
	static  int	file_open  =   0	;
     static int	file_x	=   120   ,  file_y =   80  ,   file_w = 430	,  file_h   =  280   ;
  static  int	terminal_x	=  120  , terminal_y   =	70 ,   terminal_w = 520  ,  terminal_h   =	300 ;
	static   char term_buf   [  160  ]   ;
    static	int	term_len =	0   ;
static   int  term_message  = 0  ;
		static	int paint_x =  170  ,   paint_y   = 90  , paint_w =	430  ,	paint_h =	300  ;
static   u8	paint_canvas	[ 80 *   50 ] ;
static   int paint_ready = 0	;
static	u32  paint_color =   0x0089B4FA ;
  static  int	desktop_ready	=  0 ;
       static   int   painted_win_x	,	painted_win_y  ;
      static	int	painted_paint_open	=	0  ;
	static  int  painted_terminal_open =	0 ;
		static   int	painted_file_open  = 0  ;
  static volatile	int	mouse_x  =   400	,   mouse_y	= 300 ;
     static  volatile   int	mouse_updated   =	0  ;
 		static   volatile   u8  mouse_buttons   =	0   ;
 	/* Press-edge queue: a quick press+release inside one render would
 	   otherwise collapse to "nothing happened". Handler records the
 	   edge + position; the main loop drains one per iteration. */
 	static	volatile	int	click_head	=	0	,	click_tail	=	0	;
 	static	volatile	int	click_qx	[	4	]	,	click_qy	[	4	]	;
 	static	volatile	u8	prev_mbtn	=	0	;
 	static	volatile	u32	sleep_req	=	0	;

 /* ── Mouse cursor save/restore (bitmap is 12 rows x 8 cols) ── */
#define CUR_W 8
#define CUR_H 12
  static   u32 cur_bak   [  CUR_W	*  CUR_H  ]  ;
static   int   cur_ox	=   -	1  ,	cur_oy  =  -   1 ;

   /* save the pixels we're about to scribble on, so we can put them back. polite. */
		static  void cur_save	(   int	mx  ,	int my  )   {
 for  (   int r	=	0	;  r   <	CUR_H   &&   my	+   r   < scr_h	;  r  ++  )
for  (  int   c = 0	;   c <   CUR_W   && mx	+ c	<   scr_w ;  c	++  )
	cur_bak [ r	*  CUR_W  +	c	] =  read_pixel   (   mx   +  c   , my	+ r  )  ;
		}

		static   void cur_restore	(  void	)  {
if   (	cur_ox   <   0	)  return  ;
	for  (  int  r	= 0   ;  r  < CUR_H   &&   cur_oy	+ r	<	scr_h   ;   r   ++ )
for   (	int  c  =   0  ; c	<   CUR_W	&&  cur_ox   +   c	<   scr_w  ;	c	++  )
	draw_pixel  (  cur_ox  + c  ,   cur_oy	+   r ,	cur_bak  [   r	* CUR_W   +   c	] )	;
}

static  const u8	mouse_cursor [  12   ] [   8	]  =	{
	{  0x00	, 0xFF   , 0xFF  ,  0xFF	,  0xFF ,	0xFF  ,  0xFF  ,  0xFF	}  ,
    {	0x00   ,	0x00 ,	0xFF ,  0xFF	,  0xFF	,  0xFF ,   0xFF ,	0xFF  }  ,
{	0x00 ,	0x00   ,	0x00 ,  0xFF  , 0xFF   ,  0xFF	, 0xFF   ,   0xFF }	,
	{  0x00 ,   0x00 ,  0x00	,	0x00   ,   0xFF	,  0xFF  , 0xFF   , 0xFF  }   ,
	{  0x00	,   0x00  ,   0x00 ,  0x00 ,	0x00  , 0xFF   , 0xFF  ,   0xFF   }   ,
  { 0x00	,   0x00	, 0x00	, 0x00 , 0x00 , 0x00  ,  0xFF  , 0xFF	}   ,
	{ 0x00	,	0x00  ,   0x00 ,  0x00  , 0x00	,	0x00  ,   0x00	,	0xFF	}   ,
		{   0x00	,  0x00	, 0x00 ,   0x00 ,  0x00	, 0xFF   ,	0xFF  ,   0xFF } ,
		{	0x00	,	0x00 ,  0xFF ,  0x00	,	0x00	,  0x00 , 0xFF	,   0xFF } ,
	{	0x00   ,   0xFF	, 0xFF  ,	0xFF	, 0x00	,   0x00	,	0xFF	,	0xFF   }  ,
	{  0xFF	,	0xFF   , 0xFF   ,   0xFF	,  0xFF   ,  0x00 ,	0x00	,  0xFF   }  ,
        { 0xFF , 0xFF   ,	0xFF   ,  0xFF   ,	0xFF  ,	0xFF   ,	0x00  ,   0x00  }	,
	}   ;

static	void cur_draw	( int  mx , int   my   )  {
for  (	int   r	=   0  ;  r	< CUR_H &&  my	+  r <	scr_h ; r  ++ )
		for   (   int	c  =  0	;  c   <  CUR_W &&	mx	+ c	<  scr_w  ;	c ++  )
     if	(   mouse_cursor   [	r ]	[ c  ]	!=	0xFF   )
draw_pixel	(   mx + c   ,  my	+ r   ,   COL_WHITE	) ;
   cur_ox  =	mx ;   cur_oy =	my  ;
       }

       static  void  cur_redraw  ( int	mx ,   int	my	)	{
       cur_restore  ( ) ;
		cur_save   (   mx	,	my	)	;
		cur_draw   (   mx   ,  my  )	;
 }

/* ── UI drawing ── */
   static   void  draw_taskbar	( void  ) {
 fill_rect	(	0 ,	scr_h  -  tb_h  ,   scr_w   ,	tb_h , COL_TASKBAR )  ;
   fill_rect (  0  ,	scr_h  -   tb_h   ,   scr_w ,   2	, COL_ACCENT  )	;
      fill_rounded  (  8	,	scr_h - tb_h	+	8  ,   70	,	tb_h -  16   ,	6	,	COL_ACCENT	) ;
 		draw_str   (   18  ,  scr_h  - tb_h  +  18 , "START"  , COL_BLACK )  ;
 		fill_rounded  (   86   ,	scr_h -	tb_h	+   8   ,   74  , tb_h	-   16   ,	6	,   paint_open  ?   COL_ACCENT	: COL_BTN	)   ;
      draw_str  (  97 ,	scr_h  - tb_h	+ 18  ,	"PAINT"	,  paint_open ?	COL_BLACK   :  COL_WHITE   )	;
draw_str   (	scr_w	-	100	, scr_h -   tb_h +  18  ,   "nexOS"   ,  COL_MUTED  )  ;
	char tickbuf[32];
	snprintf(tickbuf, sizeof(tickbuf), "T %d S %d", (int)pit_ticks, (int)switches);
	draw_str   (	scr_w	-	220	, scr_h -   tb_h +  18  ,   tickbuf   ,  COL_MUTED  )  ;
    }

   static   void	draw_desktop_icons	(  void	) {
 	fill_rounded  ( 22   , 286  ,	36	, 25	,  5	,  COL_ACCENT )	;
 fill_rounded   (	27  ,   281	,	16	,  7	,  3	,   COL_ACCENT )	;
     draw_str   (	9   ,  316 ,   "FILES"	, COL_WHITE	)	;
        /* Terminal icon */
  fill_rounded (   22  , 205 , 34	,  25 ,	5	,	COL_BTN	)	;
 	fill_rect   (	26  ,  209  ,  26	, 17   , 0x00000000	) ;
 draw_str	(   30 , 214  ,  ">_"	,   COL_ACCENT   )	;
 draw_str   (   13  , 234  ,  "TERMINAL" , COL_WHITE  )  ;
  /* Paint icon */
  fill_rounded   (   22  ,   42   ,	34 , 28	,  5	,  COL_WHITE  )   ;
        fill_rounded  (	28   ,   48  ,  22	, 16  ,  4	, COL_ACCENT  ) ;
   draw_str  (   16	,	76	,  "PAINT"   ,   COL_WHITE	) ;
   /* Recycle Bin icon */
 		fill_rounded (   26  , 120  ,   26  , 28	,  5	,	COL_BTN   )	;
 	fill_rect	(  23	,   116	, 32  , 5	,  COL_WHITE )   ;
         fill_rect   (   32	, 126	,   4   ,  17	,	COL_MUTED  )	;
    fill_rect (	42	,  126 ,  4	,   17   , COL_MUTED	) ;
  draw_str   ( 10	,	154 ,	"RECYCLE"   ,  COL_WHITE	)   ;
      draw_str   (	18   ,   164 , "BIN" ,	COL_WHITE )   ;
 }

static char fm_names[8][13];
static u32 fm_sizes[8];
static int fm_isdir[8];
static int fm_count = 0;
static int fm_cb(const char *name, u32 size, int is_dir) {
    if (fm_count >= 8) return 1;
    int i = 0;
    while (name[i] && i < 12) { fm_names[fm_count][i] = name[i]; i++; }
    fm_names[fm_count][i] = 0;
    fm_sizes[fm_count] = size;
    fm_isdir[fm_count] = is_dir;
    fm_count++;
    return 0;
}

static  void   draw_file_manager  (	void	) {
        draw_shadow  (   file_x  ,  file_y  ,   file_w  ,  file_h  )  ;
        fill_rounded (	file_x ,	file_y	,   file_w  ,  file_h	,  10	, COL_WINBG  )  ;
   fill_vgrad (   file_x , file_y   ,	file_w  ,	28   ,  mix_color(COL_TITLE, 0x00CDD6F4, 70)	,  COL_TITLE	)	;
      draw_str   ( file_x   +   10   ,   file_y  + 8  ,	"FILE MANAGER"   ,	COL_WHITE  )   ;
	fill_rect (	file_x   +   file_w   -   28 , file_y	+   5	,	18 , 17	,	COL_RED	)	;
      draw_str	( file_x   + file_w   -   25	,   file_y	+  6  ,	"X"	,	COL_WHITE	)	;
 draw_str	(  file_x  +  18  ,  file_y   +	48	,   "NAME" ,  COL_MUTED  )	;
      draw_str  (	file_x   +  270  ,   file_y +	48  ,  "TYPE"	,  COL_MUTED	)   ;
	for (int fi = 0; fi < fm_count && fi < 4; fi++) {
	draw_str	(  file_x + 18   , file_y +   74 + fi * 24  , fm_names[fi] ,  COL_BLACK	)   ;
	draw_str  (   file_x + 270	,	file_y +   74 + fi * 24	, fm_isdir[fi] ? "DIR" : "FILE"  , COL_MUTED )	;
	}
	if (fm_count == 0)
	draw_str	(  file_x + 18   , file_y +   74  , "(EMPTY - NO DISK)" ,  COL_MUTED	)   ;
    }

     /* secret replies. if you're reading the source, you already found them. */
static const char * term_msg [ ] =	{
""	, /* 0: silence */
		"HELP CLEAR ABOUT ECHO"   ,	/* 1 */
"nexOS offline terminal" ,   /* 2 */
"made by yazeed omari. yes, him."	, /* 3 */
 "that's me. you found the author."  , /* 4 */
	"the one and only." ,	/* 5 */
"nice try. no root here, this is nexOS."  ,  /* 6 */
"yes it's nexOS. no filesystem. on purpose."	,  /* 7 */
"hi. i've been awake since 2026." , /* 8 */
    "how? ...fine, held together by hope."	, /* 9 */
 	"sorry, i'm just a kernel. yazeed is closer."	,   /* 10 */
 	}  ;

static char term_lines[6][56];
static int term_nlines = 0;
static char browser_buf[2048];
static int browser_len = 0;
static u8 file_buf[4096];
static u8 http_buf[2048];

static void term_clear_lines(void) { term_nlines = 0; term_lines[0][0] = 0; }
static void term_add_line(const char *s) {
    if (term_nlines >= 6) return;
    int i = 0;
    while (s[i] && i < 55) { term_lines[term_nlines][i] = s[i]; i++; }
    term_lines[term_nlines][i] = 0;
    term_nlines++;
}
static int term_ls_cb(const char *name, u32 size, int is_dir) {    char line[56]; int i = 0, j = 0;
    while (name[j] && i < 40) { line[i] = name[j]; i++; j++; }
    if (is_dir && i < 54) { line[i++] = '/'; }
    line[i] = 0;
    (void)size;
    term_add_line(line);
    return 0;
}

static void fm_refresh(void) {
    fm_count = 0;
    if (g_fs.bs.bpb_sectors_per_cluster == 0) return;
    fat32_list_dir(&g_fs, 2, fm_cb);
}

		static void  draw_terminal (  void )  {
	draw_shadow  (	terminal_x	,	terminal_y	, terminal_w , terminal_h  ) ;
    fill_rounded   (	terminal_x	,	terminal_y	, terminal_w , terminal_h ,  10	,  COL_TERMBG  ) ;
    fill_vgrad	(  terminal_x  , terminal_y ,   terminal_w   ,	28	, mix_color(COL_TITLE, 0x00CDD6F4, 70)	, COL_TITLE  ) ;
    draw_str   (	terminal_x  +  10  , terminal_y	+  8  ,	"TERMINAL"  , COL_TERMFG   ) ;
    fill_rect   ( terminal_x	+ terminal_w   -	28  ,   terminal_y  +   5	, 18	,   17	,	COL_RED  )   ;
    draw_str ( terminal_x +	terminal_w - 25  ,   terminal_y  +   6	,	"X" ,	COL_TERMFG   ) ;
    draw_str	(   terminal_x  +   14	, terminal_y  +  48  ,   "nexOS terminal - type HELP"	,  COL_TERMFG   ) ;
	int tyy = terminal_y + 64;
	if   (   term_message >= 1  &&	term_message	<=  10	) {
      draw_str  (   terminal_x   +  14  ,	tyy  , term_msg [  term_message ]  ,	COL_TERMFG ) ;
      tyy += 16;
	}
	for (int li = 0; li < term_nlines && li < 4; li++) {
      draw_str  (   terminal_x   +  14  ,	tyy  , term_lines[li]  ,	COL_TERMFG ) ;
      tyy += 16;
	}
    draw_str  (	terminal_x +	14	, tyy	,	">" ,	COL_TERMFG   ) ;
	for  ( int  i =   0	;  i < term_len ;  i	++   )
    draw_char	( terminal_x  +	26  +	i   *	8	,	tyy	,
      font_map   [  (  u8  )	term_buf [   i   ] ] ,	COL_TERMFG )   ;
	}

	static void	draw_window   (   void	)  {
		draw_shadow	(  win_x  ,   win_y   , win_w   , win_h  )  ;
	fill_rounded   ( win_x  , win_y   ,  win_w ,  win_h ,  10 ,	COL_WINBG )  ;
	fill_vgrad   ( win_x  , win_y   ,  win_w ,   30 ,	mix_color(COL_TITLE, 0x00CDD6F4, 70) ,	COL_TITLE )  ;
draw_str ( win_x +   8  ,   win_y	+  8   , "nexOS v0.1"	,  COL_WHITE  )	;
   fill_rect  (   win_x + win_w   -	22	,	win_y  +  4	,	18  , 17   , COL_RED	)	;
		draw_str  (  win_x   +  win_w	-	19  ,   win_y	+  5  ,  "X"	,   COL_WHITE  )	;
        }

static   void   draw_text (	void )  {
       int	tx	=	win_x +  10 , ty =	win_y	+	32  ;
int	max_x  = win_x  +	win_w  - 10	;
		for   (   int i	=	0   ; i   <   text_len   ; i ++  ) {
		if	(	text_buf	[  i	] ==	'\n'	)   {	tx =   win_x + 10  ;   ty  += 12 ;  }
else   {
	draw_char (   tx   , ty	, font_map	[   (  u8  )   text_buf [ i   ]	] ,   COL_BLACK   )   ;
tx +=  10  ;
if	(	tx   + 10 >  max_x )	{  tx	=	win_x   +  10   ;  ty  +=   12   ;	}
}
		}
	fill_rect	(   tx  ,  ty	+ 1	,	6	,   8	,   COL_SCRN	)  ;
	}

	static   void draw_paint	(	void )  {
	draw_shadow  (   paint_x	,  paint_y ,   paint_w ,   paint_h  )  ;
	fill_rounded   (   paint_x	,  paint_y ,   paint_w ,   paint_h   ,  10	,   COL_WINBG   )  ;
      fill_vgrad	(  paint_x ,  paint_y   ,	paint_w  ,	27 ,   mix_color(COL_TITLE, 0x00CDD6F4, 70)	, COL_TITLE  )  ;
       draw_str (  paint_x +   10 , paint_y   +	8  , "PAINT"	,	COL_WHITE   )   ;
		fill_rect   (   paint_x   +  paint_w -  28 ,	paint_y	+   5  ,	18   ,   17 ,	COL_RED )	;
draw_str  (	paint_x  +	paint_w   -  25   ,	paint_y	+ 6	,  "X" ,	COL_WHITE	)   ;
		u32	colors	[	6	]   = {	0x0089B4FA  , 0x00A6E3A1  ,  0x0089B4FA ,  0x00A6E3A1	,	0x00F38BA8   , 0x00000000	}  ;
	for   ( int  ci   =  0   ; ci  <	6 ;   ci	++   )
   fill_rect   (  paint_x	+  12	+ ci  *	22   ,  paint_y	+   30   ,	18 ,	6  ,	colors	[  ci ]  ) ;
fill_rect  ( paint_x  + 10  ,   paint_y	+ 38 , 400	,	250	,   COL_WHITE	) ;
	draw_3d_border   (   paint_x	+  9  ,  paint_y +  37  ,	402	,	252 ,   COL_WINBG	,
      COL_WHITE  , COL_MUTED   ) ;
       for	(	int	py =	0   ;	py	<   50  ;  py  ++  )
		for  (  int  px  = 0   ;   px	<   80   ; px ++ )
		if  (	paint_canvas	[	py *  80   +	px ] )
      fill_rect   (   paint_x   +	12	+   px	*  5 ,   paint_y	+  40  + py  *	5   ,
	5   ,	5 ,   paint_color )   ;
draw_str   (  paint_x  +  10   , paint_y +  paint_h	- 9 ,   "DRAW WITH LEFT MOUSE"  ,	COL_BLACK   )  ;
  }

	static  void draw_menu ( void	)	{
      int	mx  =	6   ,	my   = scr_h	-  tb_h  - 152 ;
  draw_shadow   (   mx   , my ,   152 ,  152  )  ;
  fill_rounded   (   mx   , my ,   152 ,  152  ,  10	,  COL_WINBG  )  ;
       draw_str   (  mx	+	8   ,   my	+  10	,  "TERMINAL" ,	COL_BLACK	) ;
      draw_str  (	mx   +   8	,  my  +   24  ,  "NOTEPAD"  ,	COL_BLACK )   ;
 draw_str ( mx  +	8	, my   + 38	,   "ABOUT"   ,   COL_BLACK  ) ;
 draw_str ( mx + 8, my + 52, "BROWSER", COL_BLACK ) ;
 		fill_rect	(  mx + 6	, my +	76   ,  140  ,  26  ,   COL_RED  )   ;
 		draw_str	(	mx	+  18  ,   my +  85 ,	"SHUTDOWN"   ,	COL_WHITE	)  ;
   }

static  void  boot_splash (  void )   {
	clear_screen	(   COL_SCRN  )	;
int	title_x  =   (	scr_w  -  128  ) /	2 ;
      int  bar_x	=	(  scr_w -  220   )	/	2   ;
int	bar_y  =	scr_h   /  2	+  24   ;
draw_str (   title_x ,   scr_h	/   2  -  18  ,   "WELCOME TO nexOS"   , COL_WHITE )   ;
	draw_str	(   (  scr_w	-	150	)  /   2   ,	scr_h /	2  - 4 ,	"BY YAZEED OMARI"   ,	COL_MUTED	) ;
	draw_3d_border (	bar_x  ,  bar_y  ,  220	, 12 , COL_BTN   , COL_MUTED , COL_BTN )	;
	for  ( int	i	=	0	;	i   <=	10	; i ++  )  {
		fill_rect   (   bar_x +	3   ,   bar_y +	3	,  i * 21  ,  6   ,	COL_ACCENT ) ;
		for  (   volatile   u32	wait	= 0  ;	wait	< 3000000 ; wait   ++   ) asm volatile  (   "nop"  )	;
}
}

static void draw_browser_window(void) {
    draw_shadow(browser_x, browser_y, browser_w, browser_h);
    fill_rounded(browser_x, browser_y, browser_w, browser_h, 10, COL_WINBG);
    fill_vgrad(browser_x, browser_y, browser_w, 24, mix_color(COL_TITLE, 0x00CDD6F4, 70), COL_TITLE);
    draw_str(browser_x + 8, browser_y + 6, "nexOS Browser", COL_TEXT);
    fill_rect(browser_x + browser_w - 32, browser_y + 3, 26, 18, COL_RED);
    draw_str(browser_x + browser_w - 29, browser_y + 4, "X", COL_TEXT);
    fill_rect(browser_x + 4, browser_y + 28, browser_w - 8, 2, COL_ACCENT);
    fill_rect(browser_x + 4, browser_y + 32, browser_w - 8, browser_h - 40, COL_SCRN);
    if (browser_len <= 0) {
        draw_str(browser_x + 8, browser_y + 36, "WELCOME TO nexOS WEB", COL_ACCENT);
        draw_str(browser_x + 8, browser_y + 52, "OPEN TERMINAL, TYPE:", COL_TERMFG);
        draw_str(browser_x + 8, browser_y + 68, "FETCH", COL_TERMFG);
        draw_str(browser_x + 8, browser_y + 84, "THEN CLICK BROWSER", COL_TERMFG);
        draw_str(browser_x + 8, browser_y + 100, "IN THE START MENU.", COL_TERMFG);
        return;
    }
    int bx = browser_x + 8, by = browser_y + 36, col = 0, row = 0, intag = 0;
    char line[48]; int li = 0;
    for (int i = 0; i < browser_len && row < 8; i++) {
        char c = browser_buf[i];
        if (c == '<') { intag = 1; continue; }
        if (c == '>') { intag = 0; continue; }
        if (intag) continue;
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        if (c < 32 || c > 126) continue;
        if (c == ' ' && col == 0) continue;
        line[li++] = c; col++;
        if (col >= 44 || i == browser_len - 1) {
            line[li] = 0;
            char out[48]; int k = 0;
            while (line[k] && k < 47) { out[k] = line[k]; k++; }
            out[k] = 0;
            draw_str(bx, by + row * 16, out, COL_TERMFG);
            row++; col = 0; li = 0;
        }
    }
}

     static void  render_desktop	( void )   {
	/* Do not clear the whole framebuffer while dragging.  That made QEMU
       show the intermediate blank frame as visible flashing. */
 /* Render against the previous complete frame, then present it once. */
 	lfb =   backbuf	;
       if  (   !   desktop_ready )	{
		clear_screen  (	COL_BG )   ;
	desktop_ready  =  1	;
    }	else   {
		fill_rect  ( painted_win_x	- 2	,	painted_win_y   -  2 ,
    win_w + 4 , win_h   +  4   , COL_BG   )  ;
if   (  painted_paint_open  )
fill_rect	(	paint_x  -	4  ,  paint_y -	4	,   paint_w  +   8  , paint_h   +  8   , COL_BG  )	;
	if	( painted_terminal_open   )
   fill_rect	(   terminal_x -	4   ,   terminal_y   -	4 ,
	terminal_w   +	8	,  terminal_h   +	8  ,	COL_BG )   ;
if (	painted_file_open   )
      fill_rect   ( file_x	-	4	,   file_y   -  4	,	file_w   +  8   ,  file_h   +  8	,   COL_BG ) ;
        if (	painted_browser_open	)
 fill_rect   ( browser_x -	4	,   browser_y   -  4	,	browser_w   +  8   ,  browser_h   +  8	,   COL_BG ) ;
        }
		draw_desktop_icons	(   )	;
 if ( win_open  ) {
 draw_window  ( )   ;
        draw_text	(	)   ;
   }
         if	(	paint_open   ) draw_paint  (  ) ;
		if	(	terminal_open	)  draw_terminal	(   ) ;
         if	(	file_open	)	draw_file_manager	(	)	;
         if	(	browser_open	)	draw_browser_window	(	)	;
        draw_taskbar  (   ) ;
		if   ( menu_open	) draw_menu	(  )  ;
   painted_win_x  = win_x  ;
	painted_win_y =   win_y  ;
painted_paint_open   = paint_open	;
 painted_terminal_open   =   terminal_open	;
 painted_file_open =	file_open  ;
 painted_browser_open = browser_open;
 	/* Present: one bulk rep movsl for the whole frame instead of
 	   ~2.3M byte writes through emulated MMIO. */
 	{
 	u32 total = (u32)scr_h * (u32)pitch;
 	u32 words = total / 4, tail = total % 4;
 	u8 *s = lfb, *d = front_lfb;
 	__asm__ volatile("cld; rep; movsl"
 		: "+S"(s), "+D"(d), "+c"(words) : : "memory");
 	for (u32 i = 0; i < tail; i++) d[i] = s[i];
 	}
 		lfb   =   front_lfb ;
 	/* Cursor lives only on the front buffer: the full copy above
 	   already wiped the old cursor, so forget it and draw fresh. */
 	cur_ox  =   -  1	;
         cur_save   (   mouse_x  ,	mouse_y  )	;
        cur_draw (	mouse_x	,	mouse_y	) ;
        }

	/* ── Interrupt & PS/2 ── */
		typedef   struct  {	u16   lo ;	u16 sel ; u8  zero ;  u8 fl ;   u16  hi	;   } __attribute__	(	(  packed	)   )	idt_e  ;
		typedef   struct  {	u16  lim   ;   u32  base   ;  }	__attribute__  (	(	packed ) ) idt_p  ;
	static  idt_e idt [	256 ]	; static	idt_p	idt_r	;

   static	void  set_gate  (	int   n	,   u32  h ,	u16   sel  ,	u8	fl )  {
idt	[  n ]  .  lo   =  h & 0xFFFF  ;  idt  [   n  ]   . sel =   sel	;  idt   [   n   ]  .	zero  =   0   ;
      idt   [  n ]  .  fl	= fl  ;   idt  [	n ]	. hi	=	(  h  >>   16	) &   0xFFFF	;
}
	/* the only thing this whole OS does politely. */
    static   void	shutdown_system	(	void   ) {
asm  volatile   (	"cli" )   ;
		outw	(   0x604	, 0x2000	)	;  /* QEMU/Bochs ACPI power-button port */
	for	(	;   ;   )	asm  volatile (  "hlt"   ) ;
    }
		/* wait for a mouse that may never answer. deeply relatable. */
static void	mw  ( u8   t	)	{   u32   to	=   100000	;  if (   t  ==   0	)	while  ( to --  && !  (	inb   (	0x64  ) &   1	)   )  ;	else	while	(   to	--	&&   ( inb   (	0x64   )  &	2 )	)	; }

     static  void	remap_pic (   void	)  {
outb   (	0x20  ,	0x11   ) ;  outb	(  0xA0   , 0x11  )  ;
		outb (   0x21 ,   0x20 ) ;	outb   (  0xA1  ,  0x28   )	;
		outb  (	0x21	,  0x04 )   ;	outb (	0xA1  ,	0x02	) ;
       outb (   0x21  ,   0x01   )  ;	outb	(	0xA1   ,	0x01   )   ;
	outb  (   0x21 ,  0xF8 )	;	outb	(  0xA1  ,  0xFF	)	;
		}

      extern void isr44   ( void	)	,  isr33	( void  )	,  isr32 ( void )  ,  isr43 ( void ) ;

	static	void	pic_unmask_irq	(	u8 irq	)	{
	if (irq < 8) outb(0x21, inb(0x21) & ~(1 << irq));
	else if (irq < 16) outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
	}
      extern void isr128  ( void  ) ;
      extern void isr0(void), isr1(void), isr2(void), isr3(void);
      extern void isr4(void), isr5(void), isr6(void), isr7(void);
      extern void isr8(void), isr9(void), isr10(void), isr11(void);
      extern void isr12(void), isr13(void), isr14(void), isr15(void);
      extern void isr16(void), isr17(void), isr18(void), isr19(void);
      extern void isr20(void), isr21(void), isr22(void), isr23(void);
      extern void isr24(void), isr25(void), isr26(void), isr27(void);
      extern void isr28(void), isr29(void), isr30(void), isr31(void);

	static const char *exc_msg[] = {
	"DIVIDE ERROR", "DEBUG", "NMI", "BREAKPOINT",
	"OVERFLOW", "BOUND RANGE", "INVALID OPCODE", "NO COPROCESSOR",
	"DOUBLE FAULT", "COPROCESSOR OVERRUN", "INVALID TSS", "SEGMENT MISSING",
	"STACK FAULT", "GENERAL PROTECTION", "PAGE FAULT", "RESERVED",
	"x87 FPU ERROR", "ALIGNMENT CHECK", "MACHINE CHECK", "SSE EXCEPTION",
	"RESERVED", "RESERVED", "RESERVED", "RESERVED",
	"RESERVED", "RESERVED", "RESERVED", "RESERVED",
	"RESERVED", "RESERVED", "RESERVED", "RESERVED",
	};

	void	exception_handler	(	u32 vec, u32 err	)	{
	asm volatile("cli");
	clear_screen(COL_SCRN);
	draw_str(scr_w/2 - 120, scr_h/2 - 40, "KERNEL PANIC", COL_RED);
	if (vec < 32) draw_str(scr_w/2 - 120, scr_h/2 - 24, exc_msg[vec], COL_WHITE);
	char ebuf[32];
	snprintf(ebuf, sizeof(ebuf), "VEC %d ERR %x", (int)vec, err);
	draw_str(scr_w/2 - 120, scr_h/2 - 8, ebuf, COL_MUTED);
	draw_str(scr_w/2 - 120, scr_h/2 + 8, "SYSTEM HALTED - REBOOT TO CONTINUE", COL_MUTED);
	for (;;) asm volatile("hlt");
	}

 		static	volatile	u32	pit_ticks	=	0	;

	void	timer_handler	(	void	)	{
		pit_ticks++;
		outb	(	0x20	,	0x20	)	;
	}

		static	void	pit_init	(	void	)	{
		u16 div = 11932;
		outb	(	0x43	,	0x36	)	;
		outb	(	0x40	,	div & 0xFF	)	;
		outb	(	0x40	,	(div >> 8) & 0xFF	)	;
	}

#define MAX_TASKS 8
#define TS_EMPTY 0
#define TS_READY 1
#define TS_RUNNING 2
#define TS_SLEEPING 3

	typedef struct {
	u32 esp;
	int state;
	int id;
	u32 wake;
	void (*fn)(void);
	char name[16];
	} task_t;

	static task_t tasks[MAX_TASKS];
	static int cur_task = 0;
	static u32 switches = 0;
	static int sched_on = 0;

	static void task_begin(void) {
		void (*f)(void) = tasks[cur_task].fn;
	if (f) f();
	tasks[cur_task].state = TS_EMPTY;
	for (;;) asm volatile("hlt");
	}

	static int task_create(void (*fn)(void), const char *name) {
	for (int i = 0; i < MAX_TASKS; i++) {
	    if (tasks[i].state != TS_EMPTY) continue;
	    u8 *stack = (u8 *)kmalloc(8192);
	    if (!stack) return -1;
	    u32 *sp = (u32 *)(stack + 8192);
	    *--sp = 0x202;
	    *--sp = 0x08;
	    *--sp = (u32)task_begin;
	    for (int r = 0; r < 8; r++) *--sp = 0;
	    tasks[i].esp = (u32)sp;
	    tasks[i].state = TS_READY;
	    tasks[i].id = i;
	    tasks[i].wake = 0;
	    tasks[i].fn = fn;
	    int k = 0;
	    while (name[k] && k < 15) { tasks[i].name[k] = name[k]; k++; }
	    tasks[i].name[k] = 0;
	    return i;
	}
	return -1;
	}

	static u32 sched_pick(u32 savesp) {
		tasks[cur_task].esp = savesp;
	int next = cur_task;
	for (int i = 1; i <= MAX_TASKS; i++) {
	    int c = (cur_task + i) % MAX_TASKS;
	    if (tasks[c].state == TS_SLEEPING && pit_ticks >= tasks[c].wake)
	        tasks[c].state = TS_READY;
	    if (tasks[c].state == TS_READY || (tasks[c].state == TS_RUNNING && c != cur_task)) {
	        next = c;
	        break;
	    }
	}
	if (next != cur_task) {
	    tasks[cur_task].state = TS_READY;
	    tasks[next].state = TS_RUNNING;
	    cur_task = next;
	    switches++;
	}	return tasks[cur_task].esp;
	}

	u32	sched_tick	(	u32 savesp	)	{
		pit_ticks++;
		outb	(	0x20	,	0x20	)	;
		if (!sched_on) return savesp;
		return sched_pick(savesp);
	}

	static void task_sleep(u32 ticks) {
	tasks[cur_task].state = TS_SLEEPING;
	tasks[cur_task].wake = pit_ticks + ticks;
	asm volatile("int $32");
	}

	static void idle_task(void) {
	for (;;) asm volatile("hlt");
	}

#define SYS_YIELD 0
#define SYS_GETTICKS 1
#define SYS_LOG 2
#define SYS_EXIT 3

	typedef struct { u32 eax, ecx, edx, ebx, esp, ebp, esi, edi; } regs_t;

	static char klog[2048];
	static u32 klog_pos = 0;

	static void klog_puts(const char *str) {
	while (*str) {
	    klog[klog_pos % sizeof(klog)] = *str++;
	    klog_pos++;
	}
	}

	void	syscall_handler	(	regs_t *r	)	{
	switch (r->eax) {
	case SYS_YIELD:
	    task_sleep(0);
	    r->eax = 0;
	    break;
	case SYS_GETTICKS:
	    r->eax = pit_ticks;
	    break;
	case SYS_LOG:
	    klog_puts((const char *)r->ebx);
	    r->eax = 0;
	    break;
	case SYS_EXIT:
	    tasks[cur_task].state = TS_EMPTY;
	    __asm__ volatile("int $32");
	    r->eax = 0;
	    break;
	default:
	    r->eax = (u32)-1;
	    break;
	}
	}

	static u32 sys_call(u32 num, u32 a, u32 b, u32 c) {
	u32 ret;
	__asm__ volatile("int $0x80"
	    : "=a"(ret) : "a"(num), "b"(a), "c"(b), "d"(c) : "memory");
	return ret;
	}

#define MAX_FD 16
	typedef struct { int used; u32 cluster; u32 pos; u32 size; } fd_t;
	static fd_t fd_tab[MAX_FD];

	static int vfs_open(const char *path) {
	if (g_fs.bs.bpb_sectors_per_cluster == 0) return -1;
	u32 cl = fat32_get_cluster(&g_fs, path);
	if (cl < 2) return -1;
	for (int i = 0; i < MAX_FD; i++) {
	    if (!fd_tab[i].used) {
	        fd_tab[i].used = 1;
	        fd_tab[i].cluster = cl;
	        fd_tab[i].pos = 0;
	        fd_tab[i].size = 0xFFFFFFFFu;
	        return i;
	    }
	}
	return -1;
	}

	static u32 vfs_next_cluster(u32 cur) {
	u8 fatb[512];
	u32 fs_ = g_fs.bs.bpb_fat_start_sector + (cur * 4) / 512;
	ata_read_sector_data(fs_, fatb);
	return *(u32 *)(fatb + (cur * 4) % 512);
	}

	static int vfs_read(int fd, u8 *buf, u32 n) {
	if (fd < 0 || fd >= MAX_FD || !fd_tab[fd].used) return -1;
	if (g_fs.bs.bpb_sectors_per_cluster == 0) return -1;
	u32 spc = g_fs.bs.bpb_sectors_per_cluster;
	u32 clbytes = spc * 512;
	u32 cur = fd_tab[fd].cluster;
	u32 skip = fd_tab[fd].pos;
	while (skip >= clbytes) {
	    cur = vfs_next_cluster(cur);
	    if (cur < 2 || cur >= 0x0FFFFFF8) return 0;
	    skip -= clbytes;
	}
	u32 total = 0;
	while (total < n) {
	    if (cur < 2 || cur >= 0x0FFFFFF8) break;
	    u32 sector = g_fs.bs.data_start + (cur - 2) * spc + skip / 512;
	    u32 off = skip % 512;
	    u8 sec[512];
	    ata_read_sector_data(sector, sec);
	    u32 cp = 512 - off;
	    if (cp > n - total) cp = n - total;
	    memcpy(buf + total, sec + off, cp);
	    total += cp;
	    skip += cp;
	    if (skip >= clbytes) { skip = 0; cur = vfs_next_cluster(cur); }
	}
	fd_tab[fd].pos += total;
	return (int)total;
	}

	static int vfs_close(int fd) {
	if (fd < 0 || fd >= MAX_FD || !fd_tab[fd].used) return -1;
	fd_tab[fd].used = 0;
	return 0;
	}

#define WQ_SIZE 8
	typedef struct { int head, tail; int q[WQ_SIZE]; } waitq_t;
	static waitq_t net_wq = {0, 0, {0}};

	static void wq_wake(waitq_t *wq) {
	if (wq->head != wq->tail) {
	    int t = wq->q[wq->tail & (WQ_SIZE - 1)];
	    wq->tail++;
	    if (t >= 0 && t < MAX_TASKS) tasks[t].state = TS_READY;
	}
	}

	static void wq_sleep(waitq_t *wq) {
	int me = cur_task;
	wq->q[wq->head & (WQ_SIZE - 1)] = me;
	wq->head++;
	tasks[me].state = TS_SLEEPING;
	tasks[me].wake = 0xFFFFFFFFu;
	__asm__ volatile("int $32");
	}

		static	void init_idt  (   void   ) {
		idt_r .	lim  =  sizeof   (	idt_e	)	*   256	-   1 ;  idt_r	.   base	= ( u32 ) &   idt ;
		for (	int   i   =  0	;	i <  256 ;   i   ++ )  set_gate (  i  ,   0  ,  0   ,   0  ) ;
  set_gate   (  44 , (   u32	)	isr44  , 0x08  ,	0x8E  ) ;	set_gate	(  33   ,	(	u32	)  isr33   ,  0x08   ,  0x8E )	;
  set_gate   (  0 , (   u32	)	isr0  , 0x08  ,	0x8E  ) ;
  set_gate   (  1 , (   u32	)	isr1  , 0x08  ,	0x8E  ) ;
  set_gate   (  2 , (   u32	)	isr2  , 0x08  ,	0x8E  ) ;
  set_gate   (  3 , (   u32	)	isr3  , 0x08  ,	0x8E  ) ;
  set_gate   (  4 , (   u32	)	isr4  , 0x08  ,	0x8E  ) ;
  set_gate   (  5 , (   u32	)	isr5  , 0x08  ,	0x8E  ) ;
  set_gate   (  6 , (   u32	)	isr6  , 0x08  ,	0x8E  ) ;
  set_gate   (  7 , (   u32	)	isr7  , 0x08  ,	0x8E  ) ;
  set_gate   (  8 , (   u32	)	isr8  , 0x08  ,	0x8E  ) ;
  set_gate   (  9 , (   u32	)	isr9  , 0x08  ,	0x8E  ) ;
  set_gate   (  10 , (   u32	)	isr10  , 0x08  ,	0x8E  ) ;
  set_gate   (  11 , (   u32	)	isr11  , 0x08  ,	0x8E  ) ;
  set_gate   (  12 , (   u32	)	isr12  , 0x08  ,	0x8E  ) ;
  set_gate   (  13 , (   u32	)	isr13  , 0x08  ,	0x8E  ) ;
  set_gate   (  14 , (   u32	)	isr14  , 0x08  ,	0x8E  ) ;
  set_gate   (  15 , (   u32	)	isr15  , 0x08  ,	0x8E  ) ;
  set_gate   (  16 , (   u32	)	isr16  , 0x08  ,	0x8E  ) ;
  set_gate   (  17 , (   u32	)	isr17  , 0x08  ,	0x8E  ) ;
  set_gate   (  18 , (   u32	)	isr18  , 0x08  ,	0x8E  ) ;
  set_gate   (  19 , (   u32	)	isr19  , 0x08  ,	0x8E  ) ;
  set_gate   (  20 , (   u32	)	isr20  , 0x08  ,	0x8E  ) ;
  set_gate   (  21 , (   u32	)	isr21  , 0x08  ,	0x8E  ) ;
  set_gate   (  22 , (   u32	)	isr22  , 0x08  ,	0x8E  ) ;
  set_gate   (  23 , (   u32	)	isr23  , 0x08  ,	0x8E  ) ;
  set_gate   (  24 , (   u32	)	isr24  , 0x08  ,	0x8E  ) ;
  set_gate   (  25 , (   u32	)	isr25  , 0x08  ,	0x8E  ) ;
  set_gate   (  26 , (   u32	)	isr26  , 0x08  ,	0x8E  ) ;
  set_gate   (  27 , (   u32	)	isr27  , 0x08  ,	0x8E  ) ;
  set_gate   (  28 , (   u32	)	isr28  , 0x08  ,	0x8E  ) ;
  set_gate   (  29 , (   u32	)	isr29  , 0x08  ,	0x8E  ) ;
  set_gate   (  30 , (   u32	)	isr30  , 0x08  ,	0x8E  ) ;
  set_gate   (  31 , (   u32	)	isr31  , 0x08  ,	0x8E  ) ;
  set_gate   (  32 , (   u32	)	isr32  , 0x08  ,	0x8E  ) ;
  set_gate   (  43 , (   u32	)	isr43  , 0x08  ,	0x8E  ) ;
  set_gate   (  128 , (   u32	)	isr128  , 0x08  ,	0xEE  ) ;
		u32   a   =	(   u32	)	&  idt_r	;   asm	volatile  (	"lidt (%0)"	:  :	"r"   (	a   )	)  ;
        }

	static   void	init_ps2	(  void  )	{
        u8	s	;  mw  (   1 ) ;   outb	(   0x64  ,  0xA8	)	;   mw   (	1 )   ;	outb	( 0x64	,   0x20	)	;
      mw (  0	)   ; s = inb	(	0x60	)	|  2 ;  mw  (	1   )   ;  outb (   0x64   ,  0x60	) ;  mw   (   1   )  ;  outb   (   0x60	,   s )	;
mw (   1	)   ;	outb	(   0x64	,   0xD4 )   ;   mw	(  1 ) ;	outb	(   0x60	,	0xF6   )   ;   mw  (	0	)	; inb (   0x60	) ;
	mw   (   1	)   ;  outb   ( 0x64   ,  0xD4	)	; mw  (	1	) ;   outb  (   0x60	,  0xF4	) ;  mw	(  0 ) ;  inb (  0x60 ) ;
	}

    static   u8	mc  =   0  ,   mp	[	3 ]	;
     /* three bytes of data and a prayer. that's the entire PS/2 protocol. */
void	mouse_handler ( void  ) {
u8	st   =   inb	(   0x64 )   ;
	if  (   st   &  1  )	{
    u8 d   = inb   ( 0x60 )  ;
		if	(	st  &	0x20   ) {
       if	( mc   ==  0	&& !   (	d  &  0x08  ) )  {   }
else   {   mp [   mc ++   ]   =  d	;   if ( mc   ==   3  )	{   mc = 0 ;
		if   ( !	(  (   mp   [   0 ]  &	0x40 )   ||  (	mp	[	0	] &  0x80 )  )	)	{
int	xd   = mp [	1 ]  ,   yd   =   mp   [	2	]  ;
   if  (  mp	[	0 ]   &	0x10	)   xd	-=   256	;	if (   mp   [  0 ] &  0x20	)  yd  -=   256	;
       /* 2x gain: full 1024x768 reachable with half the hand travel,
          so the host pointer never runs out of window first. */
       mouse_buttons =  mp	[   0  ]  &   7   ;	mouse_x  +=	xd * 2	;	mouse_y	-=  yd * 2 ;
        if ( mouse_x  <  0   )  mouse_x =   0	;  if (	mouse_x	> scr_w   -  1	)	mouse_x   =	scr_w	-	1   ;
 if  (   mouse_y  <   0   )   mouse_y =	0   ;  if	(   mouse_y  >	scr_h  - 1	) mouse_y =	scr_h	- 1	;
 	if ( (mouse_buttons & 1) && !(prev_mbtn & 1) ) {
 	if ( ((click_head + 1) & 3) != (click_tail & 3) ) {
 	click_qx[click_head & 3] = mouse_x; click_qy[click_head & 3] = mouse_y;
 	click_head++;
 	}
 	}
 	prev_mbtn = mouse_buttons;
 	mouse_updated  =  1   ;
} }	}  }  }
        outb	( 0xA0  ,   0x20 )	;   outb	(	0x20	, 0x20 )   ;
	}

/* scancode set 1, because who has time for set 2 anyway. */
		static const   char  kb [  128  ]	=	{
       0 ,  0 , '1'	,   '2'	,  '3' ,	'4'	, '5' ,  '6'   ,  '7'	,  '8'   ,	'9'  ,	'0'  ,   '-'   ,	'='  ,	8 ,   9	,
'q'	,	'w'	,	'e'  ,	'r' ,	't'	, 'y'   ,	'u'   ,  'i'   ,  'o'	,	'p'	,  '['  , ']'	,  10	,	0	,
'a'   , 's'  ,	'd'  ,	'f'	,   'g'	,	'h' , 'j'	, 'k'  ,  'l' , ';'   , '\''	, '`'   ,   0   ,  '\\'  ,
'z'  ,	'x' ,	'c'  ,   'v'	,	'b'	,	'n'   ,	'm'	, ','	,  '.'  ,	'/'  ,	0	,   '*'   ,  0 ,  ' '	,
	}	;
     static  volatile   int kbd_up	=	0  ;
     static  volatile   int shift_down	=	0  ;
     static  volatile   int caps_lock	=	0  ;

	static char kb_shift_digit(char c) {
	switch (c) {
	case '1': return '!'; case '2': return '@'; case '3': return '#';
	case '4': return '$'; case '5': return '%'; case '6': return '^';
	case '7': return '&'; case '8': return '*'; case '9': return '(';
	case '0': return ')'; case '-': return '_'; case '=': return '+';
	case '[': return '{'; case ']': return '}'; case ';': return ':';
	case '\'': return '"'; case '`': return '~'; case '\\': return '|';
	case ',': return '<'; case '.': return '>'; case '/': return '?';
	default: return c;
	}
	}

 	static	int  term_is	(   const   char   *   s   )   {
int   i =   0   ;   while  (   s	[ i ]	&&   term_buf [ i   ]   == s  [ i   ]  )  i   ++   ;
	return	s	[   i  ]	==  0   &&   i	==   term_len	;
}

	static int term_starts(const char *s) {
int i = 0; while (s[i] && i < term_len && term_buf[i] == s[i]) i++;
return s[i] == 0;
}
static void term_arg_after(const char *prefix, char *dst, int max) {
int p = 0; while (prefix[p]) p++;
int i = p, j = 0;
while (i < term_len && term_buf[i] == ' ') i++;
while (i < term_len && j < max - 1) { dst[j++] = term_buf[i++]; }
dst[j] = 0;
}
static void term_show_file(const u8 *data, int len) {
int li = 0, ci = 0, k = 0;
term_clear_lines();
static char line[56];
for (k = 0; k < len && li < 4; k++) {
    char c = (char)data[k];
    if (c == '\r') continue;
    if (c == '\n' || ci >= 55) { line[ci] = 0; term_add_line(line); li++; ci = 0; if (c != '\n') { line[ci++] = (c >= 32 && c < 127) ? c : '.'; } }
    else line[ci++] = (c >= 32 && c < 127) ? c : '.';
}
if (ci > 0 && li < 4) { line[ci] = 0; term_add_line(line); }
if (term_nlines == 0) term_add_line("(EMPTY)");
}

	void	keyboard_handler (	void	)	{
	u8  st  =   inb	( 0x64 ) ;
   if ( (  st  &	1	) && !	(	st & 0x20   )  )	{
   u8   sc  =  inb	(  0x60  ) ;
        if ( sc & 0x80 ) {
        sc &= 0x7F;
        if ( sc == 0x2A || sc == 0x36 ) shift_down = 0;
        } else if ( sc == 0x2A || sc == 0x36 ) {
        shift_down = 1;
        } else if ( sc == 0x3A ) {
        caps_lock = !caps_lock;
        } else if ( sc < 0x3A ) {
char	c   =  kb	[  sc	]  ;
if ( c >= 'a' && c <= 'z' ) {
if ( shift_down ^ caps_lock ) c -= 32;
} else if ( shift_down ) {
c = kb_shift_digit(c);
}
if ( terminal_open  )  {
if	(	c ==   8	)  { if	( term_len	>	0	)   term_len  --	;	text_updated  =	1	;  }
  else  if   ( c	==	10	)  {
 	term_clear_lines();
 	if (   term_is ( "HELP" ) )   term_message	=   1  ;
else if (   term_is  ( "ABOUT"   )  )   term_message   =   2   ;
        else	if ( term_is	(   "CLEAR"	)  )	term_message  =	0  ;
 	else  if   (  term_is	(	"ECHO" )   )	term_message	=	2 ;
else  if (   term_is	(  "CREDITS" )	)   term_message  =   3   ;
else   if	(  term_is	(  "YAZEED"  )	) term_message  = 4  ;
    else  if ( term_is  (   "OMARI"	) )   term_message  = 5   ;
   else  if  (	term_is	(	"SUDO"  )   )	term_message  =  6  ;
      else if (	term_is ( "NEXOS"   )   ) term_message	=   7  ;
 		else   if  (	term_is  (  "HELLO"	)	)   term_message =   8   ;
        else	if	(   term_is	( "SECRET"	)	)  term_message   = 9 ;
 	else	if (	term_is	(  "GOD"	)  ) term_message =	10 ;
	else if ( term_is ( "LS" ) ) {
	    term_message = 0;
	    if (g_fs.bs.bpb_sectors_per_cluster == 0) term_add_line("NO DISK IMAGE");
	    else if (fat32_list_dir(&g_fs, 2, term_ls_cb) != 0) term_add_line("READ ERR");
	    else if (term_nlines == 0) term_add_line("(EMPTY DIR)");
	}
	else if ( term_starts ( "CAT " ) ) {
	    term_message = 0;
	    char name[16]; term_arg_after("CAT ", name, sizeof(name));
	    if (name[0] == 0) term_add_line("USAGE: CAT FILE");
	    else if (g_fs.bs.bpb_sectors_per_cluster == 0) term_add_line("NO DISK IMAGE");
	    else {
	        int fd = vfs_open(name);
	        if (fd < 0) term_add_line("NOT FOUND");
	        else {
	            int n = vfs_read(fd, file_buf, sizeof(file_buf) - 1);
	            vfs_close(fd);
	            if (n <= 0) term_add_line("NOT FOUND");
	            else { file_buf[n] = 0; term_show_file(file_buf, n); }
	        }
	    }
	}
	else if ( term_is ( "PING" ) ) {
	    term_message = 0;
	    u8 mac[6];
	    net_arp_request(net_get_gw());
	    if (net_arp_lookup(net_get_gw(), mac) == 0) {
	        char line[56];
	        snprintf(line, sizeof(line), "GW MAC %x:%x:%x:%x:%x:%x",
	            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	        term_add_line(line);
	    } else term_add_line("ARP SENT TO GW");
	}
	else if ( term_starts ( "SLEEP " ) ) {
	    term_message = 0;
	    char num[12]; term_arg_after("SLEEP ", num, sizeof(num));
	    u32 t = 0;
	    for (int i = 0; num[i] >= '0' && num[i] <= '9'; i++) t = t * 10 + (u32)(num[i] - '0');
	    if (t == 0) t = 100;
	    if (t > 1000) t = 1000;
	    sleep_req = t;
	    term_add_line("SLEEPING...");
	}
	else if ( term_starts ( "FETCH" ) ) {
	    term_message = 0;
	    u32 sz = sizeof(http_buf);
	    int r = net_http_get(0x5DB8D822, "/", http_buf, &sz);
	    if (r == -1) term_add_line("NO ARP REPLY");
	    else if (r == -2) term_add_line("ARP OK, TCP IN PHASE D");
	    else term_add_line("FETCH FAILED");
	}
	else if ( term_is ( "CRASH" ) ) {
	    term_message = 0;
	    term_add_line("TRIGGERING #UD...");
	    text_updated = 1;
	    render_desktop();
	    asm volatile("ud2");
	}
	else if ( term_is ( "TASKS" ) ) {
	    term_message = 0;
	    for (int ti = 0; ti < MAX_TASKS; ti++) {
	        if (tasks[ti].state == TS_EMPTY) continue;
	        char line[56];
	        const char *st = "?";
	        if (tasks[ti].state == TS_READY) st = "READY";
	        else if (tasks[ti].state == TS_RUNNING) st = "RUN";
	        else if (tasks[ti].state == TS_SLEEPING) st = "SLEEP";
	        snprintf(line, sizeof(line), "%d %s %s", tasks[ti].id, st, tasks[ti].name);
	        term_add_line(line);
	    }
	    if (term_nlines == 0) term_add_line("(NO TASKS)");
	}
	else if ( term_is ( "DMSG" ) ) {
	    term_message = 0;
	    u32 total = klog_pos < sizeof(klog) ? klog_pos : sizeof(klog);
	    u32 start = klog_pos < sizeof(klog) ? 0 : klog_pos % sizeof(klog);
	    int li = 0, ci = 0;
	    static char dline[56];
	    for (u32 k = 0; k < total && li < 4; k++) {
	        char c = klog[(start + k) % sizeof(klog)];
	        if (c == '\n' || ci >= 55) { dline[ci] = 0; term_add_line(dline); li++; ci = 0; if (c != '\n' && li < 4) dline[ci++] = c; }
	        else dline[ci++] = c;
	    }
	    if (ci > 0 && li < 4) { dline[ci] = 0; term_add_line(dline); }
	    if (term_nlines == 0) term_add_line("(LOG EMPTY)");
	}
	else if ( term_is ( "HEAP" ) ) {
	    term_message = 0;
	    u8 *t = (u8 *)malloc(64);
	    int tok = 0;
	    if (t) {
	        for (int i = 0; i < 64; i++) t[i] = (u8)(i * 3 + 1);
	        tok = 1;
	        for (int i = 0; i < 64; i++) if (t[i] != (u8)(i * 3 + 1)) tok = 0;
	        free(t);
	    }
	    term_add_line(tok ? "ALLOC SELFTEST OK" : "ALLOC FAILED");
	    u32 ub = 0, fb = 0, nb = 0;
	    for (blk_t *b = heap_head; b; b = b->next) {
	        nb++;
	        if (b->free) fb += b->size; else ub += b->size;
	    }
	    char line[56];
	    if (!heap_head) term_add_line("HEAP EMPTY");
	    else {
	        snprintf(line, sizeof(line), "BLK %d USED %d FREE %d", (int)nb, (int)ub, (int)fb);
	        term_add_line(line);
	    }
	}
	else if ( term_is ( "LSPCI" ) ) {
	    term_message = 0;
	    pci_dev_t devs[16];
	    int n = pci_scan(devs, 16);
	    char line[56];
	    snprintf(line, sizeof(line), "PCI DEVICES %d", n);
	    term_add_line(line);
	    for (int i = 0; i < n; i++) {
	        snprintf(line, sizeof(line), "%x:%x %s IRQ %d",
	            devs[i].vendor, devs[i].device,
	            pci_class_name(devs[i].class_code, devs[i].subclass),
	            devs[i].irq);
	        term_add_line(line);
	        if (term_nlines >= 4) break;
	    }
	    if (n == 0) term_add_line("(NO PCI DEVICES)");
	}
	else if ( term_is ( "DISKINFO" ) ) {
	    term_message = 0;
	    if (ata_identify() != 0) term_add_line("NO DISK");
	    else {
	        char line[56];
	        char model[41];
	        for (int i = 0; i < 20; i++) {
	            model[i * 2] = ata_ident_buf[i * 2 + 1];
	            model[i * 2 + 1] = ata_ident_buf[i * 2];
	        }
	        model[40] = 0;
	        int e = 39;
	        while (e > 0 && model[e] == ' ') model[e--] = 0;
	        snprintf(line, sizeof(line), "%s", model);
	        term_add_line(line);
	        snprintf(line, sizeof(line), "LBA28 %x LBA48 %d",
	            ata_sectors_lo, ata_has_lba48);
	        term_add_line(line);
	    }
	}
	else if ( term_is ( "CPUID" ) ) {
	    term_message = 0;
	    u32 a, b, c, d;
	    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
	    char line[56];
	    char vend[13];
	    vend[0] = (b) & 0xFF; vend[1] = (b >> 8) & 0xFF;
	    vend[2] = (b >> 16) & 0xFF; vend[3] = (b >> 24) & 0xFF;
	    vend[4] = (d) & 0xFF; vend[5] = (d >> 8) & 0xFF;
	    vend[6] = (d >> 16) & 0xFF; vend[7] = (d >> 24) & 0xFF;
	    vend[8] = (c) & 0xFF; vend[9] = (c >> 8) & 0xFF;
	    vend[10] = (c >> 16) & 0xFF; vend[11] = (c >> 24) & 0xFF;
	    vend[12] = 0;
	    snprintf(line, sizeof(line), "CPU %s", vend);
	    term_add_line(line);
	    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
	    snprintf(line, sizeof(line), "FAM %d MOD %d APIC %d",
	        (int)((a >> 8) & 0xF), (int)((a >> 4) & 0xF), (int)((d >> 9) & 1));
	    term_add_line(line);
	}
	else if ( term_is ( "ACPI" ) ) {
	    term_message = 0;
	    u32 rsdp = acpi_find_rsdp();
	    char line[56];
	    if (!rsdp) term_add_line("NO RSDP");
	    else {
	        u32 rsdt = *(u32 *)(rsdp + 16);
	        int n = acpi_count_tables(rsdt);
	        snprintf(line, sizeof(line), "RSDP %x TBL %d", rsdp, n);
	        term_add_line(line);
	        u32 madt = acpi_find_table(rsdt, "APIC");
	        if (madt) {
	            u8 cpus = 0;
	            u32 lapic = acpi_madt_lapic(madt, &cpus);
	            snprintf(line, sizeof(line), "LAPIC %x CPU %d", lapic, cpus);
	            term_add_line(line);
	        } else term_add_line("NO MADT");
	    }
	}
	else if ( term_is ( "USB" ) ) {
	    term_message = 0;
	    usb_rescan();
	    int n = usb_count();
	    char line[56];
	    snprintf(line, sizeof(line), "USB DEVICES %d", n);
	    term_add_line(line);
	    for (int i = 0; i < n; i++) {
	        usb_dev_t *d = usb_get(i);
	        if (!d) break;
	        snprintf(line, sizeof(line), "%x:%x %s HID %d",
	            d->vid, d->pid, d->lowspeed ? "LOW" : "FULL",
	            d->hid_ep ? 1 : 0);
	        term_add_line(line);
	        if (term_nlines >= 4) break;
	    }
	    if (n == 0) term_add_line("(NO USB DEVICES)");
	}
	else if ( term_is ( "BROWSER" ) ) {
	    term_message = 0;
	    browser_open = !browser_open;
	    term_add_line(browser_open ? "BROWSER OPEN" : "BROWSER CLOSED");
	}
       else	term_message =	2   ;
      term_len	= 0  ;	text_updated  =   1 ;
   }  else if	( term_len  <  159  )  {
 	if ( c >= 'a' && c <= 'z' ) c -= 32;
 	if ( ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
 	     c == ' ' || c == '.' || c == '_' || c == '/' || c == '-' ) {
 	term_buf  [	term_len ++   ]   =	c  ;   text_updated   =	1  ;
 	}
 	}
  }	else	if   ( c   == 8   )	{	if  ( text_len >  0  )   text_len	--   ;	text_updated = 1	;  }
		else   if  (	c	==  10   )	{  if	(	text_len	<  511	)  text_buf   [	text_len	++ ]  =  '\n'  ;   text_updated =   1 ;  }
else  if (  c   >= ' '   &&  c   <=   '~' )	{ if  (	text_len   <  511   )   text_buf   [  text_len  ++  ] =	c   ; text_updated  =  1 ;	}
        }	}
	outb (	0x20 ,	0x20  )	;
}

	/* ── Entry ── */
	void main   (	void  )	{
	page_init  ( ) ;
	mem_init   ( ) ;
	ata_init   ( ) ;
    fat32_init (&g_fs, g_disk_buf, 32);
    g_disk_init = 1;
    net_init   ( ) ;
    usb_init   ( ) ;
    vbe_init  ( ) ;
	backbuf = (u8 *)kmalloc((u32)scr_h * (u32)pitch);
		init_font   (  )  ;
	boot_splash  (  ) ;
		win_x = (   scr_w	-   win_w )   / 2	;
        win_y   = (   scr_h	-  win_h -  50   )	/	2  ;
remap_pic   (	)  ;  init_idt	(	)   ;	init_ps2   (	)  ;  ;		pit_init	(  )  ;
	pic_unmask_irq(12);
	pic_unmask_irq(net_get_irq());
	if (net_get_irq() != 11) set_gate(32 + net_get_irq(), (u32)isr43, 0x08, 0x8E);
		for (int ti = 0; ti < MAX_TASKS; ti++) tasks[ti].state = TS_EMPTY;
		tasks[0].state = TS_RUNNING; tasks[0].id = 0;
		tasks[0].name[0] = 'k'; tasks[0].name[1] = 'e'; tasks[0].name[2] = 'r';
		tasks[0].name[3] = 'n'; tasks[0].name[4] = 'e'; tasks[0].name[5] = 'l';
		tasks[0].name[6] = 0;
		task_create(idle_task, "idle");
		asm  volatile   (	"sti" )	;
		sched_on = 1;
	klog_puts("nexOS boot ok\n");
		asm  volatile   (	"sti" )	;

		int   prev_lbtn   =	0	;
render_desktop   ( )  ;

	/* the main loop. it loops, mostly. */
static u32 last_poll = 0;
 if (sleep_req) { u32 s_ = sleep_req; sleep_req = 0; task_sleep(s_); }
while   (  1  )  {
int  redraw  =   0  ;
 if (pit_ticks - last_poll >= 10) { last_poll = pit_ticks; if (net_poll()) redraw = 1; }
 if (	text_updated   )  {	text_updated	= 0  ;  redraw   =	1   ; }

        if (	mouse_updated	)  {
	mouse_updated  =	0   ;
	int mx   =	mouse_x	,   my	=  mouse_y   ;
   int	lbtn   =	mouse_buttons   &  1	;
 	/* Drain one queued press edge as a synthetic press so fast
 	   clicks survive long renders. */
 	if ( click_tail != click_head ) {
 	mx = click_qx[click_tail & 3]; my = click_qy[click_tail & 3];
 	click_tail++;
 	lbtn = 1; prev_lbtn = 0;
 	}

      if (	lbtn && !   prev_lbtn	)	{
     if	( mx	>=	6  && mx	<   76   &&  my  >=  scr_h - tb_h   +	4	&&  my < scr_h  -	4   )   {
	menu_open =	!   menu_open ;  redraw   =  1   ;
       }	else if	(  mx  >=	84   &&	mx <	156   &&
		my	>= scr_h -	tb_h +	4  &&   my   < scr_h  -   4 )	{
   paint_open	=   !   paint_open  ;
   if  ( paint_open && !	paint_ready )	{
		for   (  int i = 0 ; i   <	80   *	50   ;	i  ++	)  paint_canvas  [  i   ]  =	0   ;
  paint_ready =	1   ;
}
		menu_open  =   0  ;	redraw	=  1   ;
}	else if   ( paint_open	&&   mx   >=	paint_x	+  12  &&
	mx	<	paint_x  +  412 &&	my   >= paint_y +   40 &&
		my	<  paint_y  +	290	)  {
      int   px =  (	mx  -	paint_x	- 12	)	/   5	;
		int py   =   ( my	-	paint_y  -	40	) /   5 ;
       if   (	px	>= 0	&&   px	<  80   && py >=   0  &&  py <  50   )	{
	paint_canvas   [ py *	80 +	px   ]   =   1	;
       redraw	=  1  ;
      }
       }   else if   (   paint_open &&	mx >= paint_x	+   12	&&   mx	<  paint_x	+   144  &&
     my	>=	paint_y +   30  && my <  paint_y	+	36 )	{
     u32 colors	[ 6	]  =	{  0x00E87511  ,  0x00D93434 ,	0x002B7FFF	,
0x001FA463 ,  0x008B48C7   ,   0x00000000	}   ;
		paint_color	= colors	[ ( mx	-	paint_x   -  12  )  /	22	]   ;
     redraw	=   1	;
}   else  if   (	paint_open   &&	mx >= paint_x  +	paint_w   -   28	&&
mx   <  paint_x	+	paint_w  -   10	&&  my  >=  paint_y  +	5 &&
		my <  paint_y  +  22	) {
  paint_open  =	0	;
		redraw  =	1  ;
} else if	(  mx >=	10	&&	mx < 68  && my >=   34  &&   my  <   104   ) {
      paint_open   =  1   ;
if  ( !  paint_ready	)   {
for   (  int i  =	0  ; i	<	80   *   50	;   i ++	)	paint_canvas [   i	] = 0	;
 paint_ready  =	1 ;
}
        menu_open  = 0	;  redraw  =	1  ;
		} else	if  ( mx   >=	10  && mx  <   70	&&	my	>= 198	&&	my	<  260   )  {
    terminal_open =  1   ;	menu_open	=  0  ;	redraw =  1   ;
} else if   (   mx   >=	8   && mx  <   72	&&	my  >=  276 &&   my <	340 )	{
 file_open = 1	;   menu_open  =   0   ;	fm_refresh();	redraw   =  1  ;
      }	else  if (	terminal_open &&  mx	>= terminal_x +   terminal_w	-   28	&&
mx <	terminal_x  +  terminal_w	-  10  &&
	my   >=	terminal_y +	5 &&	my  <  terminal_y  +	22   )  {
  terminal_open  =  0   ;   redraw	=	1   ;
}	else  if	( file_open  &&   mx  >=  file_x	+ file_w -  28   &&
		mx	< file_x	+ file_w   -   10  &&   my   >=	file_y	+  5	&&
		my	<   file_y   +	22  )   {
       file_open  = 0	;	redraw   =   1	;
  }  else  if	( menu_open   &&  mx	>=	6	&&   mx  <	158 &&
  my   >=  scr_h   - tb_h  -  152   &&	my	< scr_h   - tb_h ) {
 int menu_y = scr_h   -   tb_h  - 152	;
       if (	my  >=   menu_y + 76 && my	<   menu_y  +	102   )   {
 		shutdown_system ( )   ;
         }
         if (	my  >=   menu_y + 52 && my	<   menu_y  +	76   )   {
 		browser_open = !browser_open;
 		if (browser_open) draw_browser_window();
         }
         menu_open	=	0	;  redraw =  1 ;
	}  else   if	( browser_open && mx >= browser_x && mx < browser_x + browser_w &&
   my >= browser_y && my < browser_y + 24 ) {
       if   ( mx >= browser_x + browser_w - 32 && mx < browser_x + browser_w - 6 &&
   my >= browser_y + 3 && my < browser_y + 21 ) {
     browser_open = 0;
       dragging = 0; drag_win = 0;
 		redraw  =   1  ;
 		} else {
 dragging = 1; drag_win = 5;
 		drag_off_x = mx - browser_x;
       drag_off_y = my - browser_y;
 }
	}  else   if	( file_open && mx >= file_x && mx < file_x + file_w &&
   my >= file_y && my < file_y + 28 ) {
 dragging = 1; drag_win = 4;
 		drag_off_x = mx - file_x;
       drag_off_y = my - file_y;
 	}  else   if	( terminal_open && mx >= terminal_x && mx < terminal_x + terminal_w &&
   my >= terminal_y && my < terminal_y + 28 ) {
 dragging = 1; drag_win = 3;
 		drag_off_x = mx - terminal_x;
       drag_off_y = my - terminal_y;
 	}  else   if	( paint_open && mx >= paint_x && mx < paint_x + paint_w &&
   my >= paint_y && my < paint_y + 27 ) {
 dragging = 1; drag_win = 2;
 		drag_off_x = mx - paint_x;
       drag_off_y = my - paint_y;
	}  else   if	( win_open  &&  mx  >=	win_x   &&   mx	<	win_x	+   win_w	&&
   my	>= win_y  &&	my	<  win_y +  30	)  {
       if   ( mx  >=	win_x +	win_w   -  22  &&  mx   <   win_x   +  win_w  -   4  &&
   my   >=   win_y  +	4  &&   my   <   win_y   + 21 ) {
     win_open =   0 ;
       dragging   = 0 ; drag_win = 0 ;
 		redraw  =   1  ;
 		} else {
 dragging  =	1   ; drag_win = 1 ;
 		drag_off_x   =	mx	-	win_x   ;
       drag_off_y  =  my	-	win_y  ;
 }
	} else	if  (  menu_open   )  {
		menu_open =  0  ;   redraw   =  1  ;
}
  }

 if	( dragging   && drag_win != 0   && lbtn   )	{
     	int	nx	=	mx	- drag_off_x	;
  	int   ny =	my  -   drag_off_y ;
         if  (  nx < 0	) nx   =   0 ;
 if  (	ny  <	0   )  ny	= 0   ;
  	if ( drag_win == 1 && (nx != win_x || ny != win_y)) { win_x = nx; win_y = ny; redraw = 1; }
  	else if ( drag_win == 2 && (nx != paint_x || ny != paint_y)) { paint_x = nx; paint_y = ny; redraw = 1; }
  	else if ( drag_win == 3 && (nx != terminal_x || ny != terminal_y)) { terminal_x = nx; terminal_y = ny; redraw = 1; }
  	else if ( drag_win == 4 && (nx != file_x || ny != file_y)) { file_x = nx; file_y = ny; redraw = 1; }
  	else if ( drag_win == 5 && (nx != browser_x || ny != browser_y)) { browser_x = nx; browser_y = ny; redraw = 1; }
  	}

        if	(  paint_open   && lbtn  &&   mx >= paint_x  + 12  &&
	mx <  paint_x  +   412 &&   my >=  paint_y  +  40   &&
my   <	paint_y  +   290 )	{
int px =  (   mx  -  paint_x  -   12 )	/ 5   ;
      int	py   =  (   my	-   paint_y  -	40 )   / 5 ;
		if   ( px >=	0 &&	px  <  80  &&   py	>=   0	&&  py	<  50 )	{
	paint_canvas  [  py *   80  +   px  ]	=  1 ;
	redraw =  1 ;
    }
}

        if	(   ! lbtn	)	{ dragging   = 0; drag_win = 0; }
	prev_lbtn	=   lbtn   ;
if	( ! redraw	)  cur_redraw	(  mx	, my   )   ;
    }

 if	(  redraw   )	render_desktop	(  )   ;
      }
    }

        /* the assembly needs someone to call. don't ask why there are two mains. */
	void   _main  (   void   )   {   main	(   ) ;   }

	/* gcc insists on calling this from main(). it does nothing. like a meeting. */
	void   __main	(   void   )  {   }
