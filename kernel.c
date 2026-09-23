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

extern fat32_fs_t g_fs;
extern u8 g_disk_buf[];
extern int g_disk_init;

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
#define NUM_PAGES 4096
#define HEAP_START_PAGE 1024

static u32 page_directory[1024] __attribute__((aligned(4096)));
static u32 pt0[1024] __attribute__((aligned(4096)));
static u32 pt1[1024] __attribute__((aligned(4096)));
static u32 pt2[1024] __attribute__((aligned(4096)));
static u32 pt3[1024] __attribute__((aligned(4096)));
static u32 pt_lfb[1024] __attribute__((aligned(4096)));
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
#define BACKBUFFER_ADDR 0x200000   /* free real estate. i hope nothing lives here. */
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

		for  (	int row = y   ;	row  < y2   ;   row ++  )
        for (	int  col =  x ;  col  < x2   ;  col ++ )  draw_pixel	(   col	,  row   ,   color )   ;
      }

static	void   clear_screen ( u32 color   )	{
fill_rect (   0 , 0   ,  scr_w ,  scr_h   , color   )   ;
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
/* numerals? we don't do numerals here. O is close enough. */
		for  (   int	c  = '0'   ;  c   <=	'9'	;   c ++   )
		font_map  [   c  ]   =  font_map  [	'O' ] ;
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
     fill_rect  (  8	,	scr_h  - tb_h	+	8  ,   70	,	tb_h -  16   ,	COL_ACCENT	) ;
		draw_str   (   18  ,  scr_h  - tb_h  +  18 , "START"  , COL_BLACK )  ;
		fill_rect  (   86   ,	scr_h -	tb_h	+   8   ,   74  , tb_h	-   16  ,   paint_open  ?   COL_ACCENT	: COL_BTN	)   ;
     draw_str  (  97 ,	scr_h  - tb_h	+ 18  ,	"PAINT"	,  paint_open ?	COL_BLACK   :  COL_WHITE   )	;
draw_str   (	scr_w	-	100	, scr_h -   tb_h +  18  ,   "nexOS"   ,  COL_MUTED  )	;
    }

   static   void	draw_desktop_icons	(  void	) {
	fill_rect  ( 22   , 286  ,	36	, 25	,  COL_ACCENT )	;
fill_rect   (	27  ,   281	,	16	,  7	,   COL_ACCENT )	;
    draw_str   (	9   ,  316 ,   "FILES"	, COL_WHITE	)	;
       /* Terminal icon */
 fill_rect (   22  , 205 , 34	,  25 ,	COL_BTN	)	;
	fill_rect   (	26  ,  209  ,  26	, 17   , 0x00000000	) ;
draw_str	(   30 , 214  ,  ">_"	,   COL_ACCENT   )	;
draw_str   (   13  , 234  ,  "TERMINAL" , COL_WHITE  )  ;
 /* Paint icon */
 fill_rect   (   22  ,   42   ,	34 , 28	,  COL_WHITE  )   ;
       fill_rect  (	28   ,   48  ,  22	, 16  , COL_ACCENT  ) ;
  draw_str  (   16	,	76	,  "PAINT"   ,   COL_WHITE	) ;
  /* Recycle Bin icon */
		fill_rect (   26  , 120  ,   26  , 28	,	COL_BTN   )	;
	fill_rect	(  23	,   116	, 32  , 5	,  COL_WHITE )   ;
        fill_rect   (   32	, 126	,   4   ,  17	,	COL_MUTED  )	;
   fill_rect (	42	,  126 ,  4	,   17   , COL_MUTED	) ;
 draw_str   ( 10	,	154 ,	"RECYCLE"   ,  COL_WHITE	)   ;
     draw_str   (	18   ,   164 , "BIN" ,	COL_WHITE )   ;
}

static  void   draw_file_manager  (	void	) {
        fill_rect  (   file_x  +   3	,   file_y + 4 , file_w   ,	file_h  , COL_TERMBG ) ;
       fill_rect (	file_x ,	file_y	,   file_w  ,  file_h	, COL_WINBG  )  ;
  fill_rect (   file_x , file_y   ,	file_w  ,	28   ,  COL_TITLE	)	;
      draw_str   ( file_x   +   10   ,   file_y  + 8  ,	"FILE MANAGER"   ,	COL_WHITE  )   ;
	fill_rect (	file_x   +   file_w   -   28 , file_y	+   5	,	18 , 17	,	COL_RED	)	;
      draw_str	( file_x   + file_w   -   25	,   file_y	+  6  ,	"X"	,	COL_WHITE	)	;
 draw_str	(  file_x  +  18  ,  file_y   +	48	,   "NAME" ,  COL_MUTED  )	;
      draw_str  (	file_x   +  270  ,   file_y +	48  ,  "TYPE"	,  COL_MUTED	)   ;
	draw_str	(  file_x + 18   , file_y +   74  , "README.TXT" ,  COL_BLACK	)   ;
draw_str  (   file_x + 270	,	file_y +  74	, "TEXT"  , COL_MUTED )	;
	draw_str  (	file_x +   18	,  file_y	+  98   , "NOTES.TXT"   ,	COL_BLACK  ) ;
		draw_str (	file_x + 270   ,	file_y +	98	,  "TEXT" ,   COL_MUTED )	;
		draw_str	(   file_x	+	18	,	file_y +  122 ,  "PAINT.DAT"   ,   COL_BLACK   )   ;
draw_str (	file_x	+	270  ,	file_y + 122	,   "APP DATA" ,   COL_MUTED  )   ;
	draw_str (	file_x   + 18   ,   file_y + 146	,   "TERMINAL"  ,	COL_BLACK )  ;
draw_str ( file_x   +  270	,	file_y +   146	,	"APP" , COL_MUTED   ) ;
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

		static void  draw_terminal (  void )  {
	fill_rect  (	terminal_x	+  3  ,	terminal_y + 4	,   terminal_w	,   terminal_h  ,   COL_TERMBG  ) ;
    fill_rect   (	terminal_x	,	terminal_y	, terminal_w , terminal_h ,  COL_TERMBG  ) ;
    fill_rect	(  terminal_x  , terminal_y ,   terminal_w   ,	28	, COL_TITLE  ) ;
    draw_str   (	terminal_x  +  10  , terminal_y	+  8  ,	"TERMINAL"  , COL_TERMFG   ) ;
    fill_rect   ( terminal_x	+ terminal_w   -	28  ,   terminal_y  +   5	, 18	,   17	,	COL_RED  )   ;
    draw_str ( terminal_x +	terminal_w - 25  ,   terminal_y  +   6	,	"X" ,	COL_TERMFG   ) ;
    draw_str	(   terminal_x  +   14	, terminal_y  +  48  ,   "nexOS terminal - type HELP"	,  COL_TERMFG   ) ;
		if   (   term_message >= 1  &&	term_message	<=  10	)
      draw_str  (   terminal_x   +  14  ,	terminal_y   +	64  , term_msg [  term_message ]  ,	COL_TERMFG ) ;
    draw_str  (	terminal_x +	14	, terminal_y  +  88	,	">" ,	COL_TERMFG   ) ;
	for  ( int  i =   0	;  i < term_len ;  i	++   )
    draw_char	( terminal_x  +	26  +	i   *	8	,	terminal_y	+   88	,
      font_map   [  (  u8  )	term_buf [   i   ] ] ,	COL_TERMFG )   ;
	}

	static void	draw_window   (   void	)  {
		draw_3d_border	(  win_x  ,   win_y   , win_w   , win_h  ,	COL_WINBG	,   COL_WHITE   ,   COL_MUTED   )  ;
	fill_rect   ( win_x  , win_y   ,  win_w ,   30 ,	COL_TITLE )  ;
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
	draw_3d_border  (   paint_x	,  paint_y ,   paint_w ,   paint_h   ,   COL_WINBG   ,
	COL_WHITE ,	COL_MUTED )  ;
      fill_rect	(  paint_x ,  paint_y   ,	paint_w  ,	27 ,   COL_TITLE  )  ;
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
 draw_3d_border   (   mx   , my ,   152 ,  152  ,  COL_WINBG  ,  COL_WHITE  , COL_MUTED   ) ;
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
		for  (   volatile   u32	wait	= 0  ;	wait	< 33000000 ; wait   ++   ) asm volatile  (   "nop"  )	;
}
}

static void draw_browser_window(void) {
    fill_rect(browser_x, browser_y, browser_w, browser_h, COL_WINBG);
    fill_rect(browser_x, browser_y, browser_w, 24, COL_TITLE);
    draw_str(browser_x + 8, browser_y + 6, "nexOS Browser", COL_TEXT);
    fill_rect(browser_x + browser_w - 32, browser_y + 3, 26, 18, COL_RED);
    draw_str(browser_x + browser_w - 29, browser_y + 4, "X", COL_TEXT);
    fill_rect(browser_x + 4, browser_y + 28, browser_w - 8, 2, COL_ACCENT);
    fill_rect(browser_x + 4, browser_y + 32, browser_w - 8, browser_h - 40, COL_SCRN);
    draw_str(browser_x + 8, browser_y + 36, "http://", COL_ACCENT);
}

     static void  render_desktop	( void )   {
	/* Do not clear the whole framebuffer while dragging.  That made QEMU
       show the intermediate blank frame as visible flashing. */
 /* Render against the previous complete frame, then present it once. */
	lfb =   (	u8	*  )  BACKBUFFER_ADDR	;
cur_restore	( )	;
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
	cur_ox  =   -  1	;
        cur_save   (   mouse_x  ,	mouse_y  )	;
       cur_draw (	mouse_x	,	mouse_y	) ;

        for	(   int  row =	0	;  row	<	scr_h	; row	++ )	{
     u8  *	src  = lfb	+ row	*   pitch   ;
u8   * dst	=  front_lfb +  row	*   pitch   ;
      for  (   int	i   =  0 ;	i  <	pitch   ;	i  ++	)  dst  [  i   ]  =	src [   i	] ;
 }
		lfb   =   front_lfb ;
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
	outb  (   0x21 ,  0xF9 )	;	outb	(  0xA1  ,  0xEF	)	;
		}

      extern void isr44   ( void	)	,  isr33	( void  )	;

		static	void init_idt  (   void   ) {
		idt_r .	lim  =  sizeof   (	idt_e	)	*   256	-   1 ;  idt_r	.   base	= ( u32 ) &   idt ;
		for (	int   i   =  0	;	i <  256 ;   i   ++ )  set_gate (  i  ,   0  ,  0   ,   0  ) ;
  set_gate   (  44 , (   u32	)	isr44  , 0x08  ,	0x8E  ) ;	set_gate	(  33   ,	(	u32	)  isr33   ,  0x08   ,  0x8E )	;
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
       mouse_buttons =  mp	[   0  ]  &   7   ;	mouse_x  +=	xd	;	mouse_y	-=  yd ;
        if ( mouse_x  <  0   )  mouse_x =   0	;  if (	mouse_x	> scr_w   -  9	)	mouse_x   =	scr_w	-	9   ;
 if  (   mouse_y  <   0   )   mouse_y =	0   ;  if	(   mouse_y  >	scr_h  - 13	) mouse_y =	scr_h	- 13	;
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

	static	int  term_is	(   const   char   *   s   )   {
int   i =   0   ;   while  (   s	[ i ]	&&   term_buf [ i   ]   == s  [ i   ]  )  i   ++   ;
	return	s	[   i  ]	==  0   &&   i	==   term_len	;
}

	void	keyboard_handler (	void	)	{
	u8  st  =   inb	( 0x64 ) ;
   if ( (  st  &	1	) && !	(	st & 0x20   )  )	{
   u8   sc  =  inb	(  0x60  ) ;
        if   (   ! (  sc & 0x80	)  &&	sc   <  0x3A	)	{
char	c   =  kb	[  sc	]  ;
if ( terminal_open  )  {
if	(	c ==   8	)  { if	( term_len	>	0	)   term_len  --	;	text_updated  =	1	;  }
  else  if   ( c	==	10	)  {
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
      else	term_message =	2   ;
    term_len	= 0  ;	text_updated  =   1	;
   }  else if	(   c	>=	'a'   &&  c	<=  'z'  &&  term_len  <  159  ) {
	term_buf  [	term_len ++   ]   =	c  - 32 ;   text_updated   =	1  ;
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
    vbe_init  ( ) ;
		init_font   (  )  ;
	boot_splash  (  ) ;
		win_x = (   scr_w	-   win_w )   / 2	;
        win_y   = (   scr_h	-  win_h -  50   )	/	2  ;
remap_pic   (	)  ;  init_idt	(	)   ;	init_ps2   (	)  ;
		asm  volatile   (	"sti" )	;

		int   prev_lbtn   =	0	;
render_desktop   ( )  ;

	/* the main loop. it loops, mostly. */
while   (  1  )  {
int  redraw  =   0  ;
 if (	text_updated   )  {	text_updated	= 0  ;  redraw   =	1   ; }

        if (	mouse_updated	)  {
	mouse_updated  =	0   ;
	int mx   =	mouse_x	,   my	=  mouse_y   ;
   int	lbtn   =	mouse_buttons   &  1	;

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
 file_open = 1	;   menu_open  =   0   ;	redraw	=   1  ;
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
	}  else   if	( win_open  &&  mx  >=	win_x   &&   mx	<	win_x	+   win_w	&&
   my	>= win_y  &&	my	<  win_y +  30	)  {
      if   ( mx  >=	win_x +	win_w   -  22  &&  mx   <   win_x   +  win_w  -   4  &&
  my   >=   win_y  +	4  &&   my   <   win_y   + 21 ) {
    win_open =   0 ;
      dragging   = 0 ;
		redraw  =   1  ;
		} else {
dragging  =	1   ;
		drag_off_x   =	mx	-	win_x   ;
      drag_off_y  =  my	-	win_y  ;
 drag_target_x  =  win_x   ;
drag_target_y =   win_y ;
}
	} else	if  (  menu_open   )  {
		menu_open =  0  ;   redraw   =  1  ;
}
  }

if	( win_open   &&	dragging   && lbtn   )	{
      int	nx	=	mx	- drag_off_x	;
	int   ny =	my  -   drag_off_y ;
        if  (  nx < 0	) nx   =   0 ;
if  (	ny  <	0   )  ny	= 0   ;
drag_target_x =	nx   ;
      drag_target_y =   ny  ;
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

        if	(   ! lbtn	)	dragging   = 0   ;
	prev_lbtn	=   lbtn   ;
if	( ! redraw	)  cur_redraw	(  mx	, my   )   ;
    }

if	(  redraw   )	render_desktop	(  )   ;

	/* Ease toward the latest mouse position so packet timing does not
           make the window jump from one hardware sample to the next. */
		if   ( dragging  &&	(	win_x !=   drag_target_x  ||	win_y  !=  drag_target_y   )  )	{
    int dx	=	drag_target_x	-  win_x	,   dy  =   drag_target_y -  win_y	;
  win_x	+=   ( dx >	1	|| dx	< -   1 ) ?  dx  /  2 :	dx	;
  win_y +=   (  dy  > 1  ||   dy   < - 1   )  ?  dy   / 2   :   dy   ;
		render_desktop	(  )	;
     }
     }
   }

        /* the assembly needs someone to call. don't ask why there are two mains. */
	void   _main  (   void   )   {   main	(   ) ;   }

	/* gcc insists on calling this from main(). it does nothing. like a meeting. */
	void   __main	(   void   )  {   }
