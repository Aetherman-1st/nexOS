

void main();

void _main() {
    main();
}

unsigned char* VGA_SCREEN = (unsigned char*) 0xA0000;

#define COLOR_ORANGE     0x06
#define COLOR_LIGHT_GREY 0x07
#define COLOR_DARK_GREY  0x08
#define COLOR_WHITE      0x0F
#define COLOR_BLACK      0x00

typedef struct {
    unsigned short low_offset;
    unsigned short sel;
    unsigned char always0;
    unsigned char flags;
    unsigned short high_offset;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed)) idt_ptr_t;

idt_entry_t idt[256];
idt_ptr_t idt_reg;

void set_idt_gate(int n, unsigned int handler, unsigned short sel, unsigned char flags) {
    idt[n].low_offset = handler & 0xFFFF;
    idt[n].sel = sel;
    idt[n].always0 = 0;
    idt[n].flags = flags;
    idt[n].high_offset = (handler >> 16) & 0xFFFF;
}

static inline void outb(unsigned short port, unsigned char val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void mouse_wait(unsigned char type) {
    unsigned int timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}

void draw_pixel(int x, int y, unsigned char color) {
    if (x >= 0 && x < 320 && y >= 0 && y < 200) {
        VGA_SCREEN[y * 320 + x] = color;
    }
}

void draw_rect_fast(int x, int y, int width, int height, unsigned char color) {
    if (x < 0 || y < 0 || x + width > 320 || y + height > 200) return;
    for (int i = 0; i < height; i++) {
        unsigned int offset = (y + i) * 320 + x;
        for (int j = 0; j < width; j++) {
            VGA_SCREEN[offset + j] = color;
        }
    }
}

void draw_3d_rect(int x, int y, int width, int height, unsigned char base_color, int pressed) {
    draw_rect_fast(x, y, width, height, base_color);
    unsigned char top_left_color = pressed ? COLOR_DARK_GREY : COLOR_WHITE;
    unsigned char bottom_right_color = pressed ? COLOR_WHITE : COLOR_DARK_GREY;
    
    unsigned int top_offset = y * 320 + x;
    unsigned int bottom_offset = (y + height - 1) * 320 + x;
    
    for (int i = 0; i < width; i++) {
        VGA_SCREEN[top_offset + i] = top_left_color;
        VGA_SCREEN[bottom_offset + i] = bottom_right_color;
    }
    for (int i = 0; i < height; i++) {
        VGA_SCREEN[(y + i) * 320 + x] = top_left_color;
        VGA_SCREEN[(y + i) * 320 + x + width - 1] = bottom_right_color;
    }
}

unsigned char font_S[8] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00};
unsigned char font_T[8] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00};
unsigned char font_A[8] = {0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00};
unsigned char font_R[8] = {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00};
unsigned char font_n[8] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00};
unsigned char font_E[8] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00};
unsigned char font_X[8] = {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00};
unsigned char font_O[8] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00};
unsigned char font_v[8] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00};
unsigned char font_0[8] = {0x3C, 0x66, 0x6E, 0x7E, 0x76, 0x66, 0x3C, 0x00};
unsigned char font_1[8] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00};
unsigned char font_dot[8]= {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00};

void draw_char(int x, int y, unsigned char* bitmap, unsigned char color) {
    for (int row = 0; row < 8; row++) {
        unsigned char byte = bitmap[row];
        for (int col = 0; col < 8; col++) {
            if ((byte >> (7 - col)) & 1) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

unsigned char mouse_cursor[12][8] = {
    {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF},
    {0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00}
};

void draw_mouse(int mx, int my) {
    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 8; col++) {
            if (mouse_cursor[row][col] != 0xFF) {
                draw_pixel(mx + col, my + row, mouse_cursor[row][col]);
            }
        }
    }
}

void remap_pic() {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28); 
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    // Keep the timer masked, but allow keyboard IRQ1 and mouse IRQ12.
    outb(0x21, 0xF9);
    outb(0xA1, 0xEF);
}

extern void isr44();
extern void isr33();

void init_idt_mouse() {
    idt_reg.limit = (sizeof(idt_entry_t) * 256) - 1;
    idt_reg.base  = (unsigned int)&idt;
    for (int i = 0; i < 256; i++) set_idt_gate(i, 0, 0, 0);
    set_idt_gate(44, (unsigned int)isr44, 0x08, 0x8E);
    set_idt_gate(33, (unsigned int)isr33, 0x08, 0x8E);
    
    unsigned int idt_address = (unsigned int)&idt_reg;
    asm volatile("lidt (%0)" : : "r"(idt_address));
}

void init_ps2_mouse() {
    unsigned char status;
    mouse_wait(1);
    outb(0x64, 0xA8); 

    mouse_wait(1);
    outb(0x64, 0x20); 
    mouse_wait(0);
    status = (inb(0x60) | 2); 
    
    mouse_wait(1);
    outb(0x64, 0x60); 
    mouse_wait(1);
    outb(0x60, status);
    
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, 0xF6); 
    mouse_wait(0);
    inb(0x60); 

    mouse_wait(1);
    outb(0x64, 0xD4); 
    mouse_wait(1);
    outb(0x60, 0xF4); 
    mouse_wait(0);
    inb(0x60);        
}

int mouse_x = 160;
int mouse_y = 100;
volatile int mouse_updated = 0;
unsigned char mouse_cycle = 0;
unsigned char mouse_packet[3];

void mouse_handler() {
    unsigned char status = inb(0x64);
    if (status & 0x01) {
        unsigned char data = inb(0x60);
        if (status & 0x20) {
            if (mouse_cycle == 0 && !(data & 0x08)) {
                // Invalid packet header
            } else {
                mouse_packet[mouse_cycle++] = data;
                if (mouse_cycle == 3) {
                    mouse_cycle = 0;
                    if (!((mouse_packet[0] & 0x40) || (mouse_packet[0] & 0x80))) {
                        int x_delta = (int)mouse_packet[1];
                        int y_delta = (int)mouse_packet[2];
                        
                        if (mouse_packet[0] & 0x10) x_delta -= 256;
                        if (mouse_packet[0] & 0x20) y_delta -= 256;

                        mouse_x += x_delta;
                        mouse_y -= y_delta; 

                        if (mouse_x < 0) mouse_x = 0;
                        if (mouse_x > 311) mouse_x = 311;
                        if (mouse_y < 0) mouse_y = 0;
                        if (mouse_y > 187) mouse_y = 187;

                        mouse_updated = 1;
                    }
                }
            }
        }
    }
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void keyboard_handler() {
    unsigned char status = inb(0x64);

    // Drain keyboard data so the controller cannot leave IRQ1 pending.
    // Mouse bytes belong to IRQ12 and must be consumed by mouse_handler.
    if ((status & 0x01) && !(status & 0x20)) {
        (void)inb(0x60);
    }

    outb(0x20, 0x20);
}

void render_desktop() {
    draw_rect_fast(0, 0, 320, 200, COLOR_LIGHT_GREY);
    draw_rect_fast(0, 182, 320, 18, COLOR_ORANGE);
    
    draw_3d_rect(4, 184, 38, 14, COLOR_LIGHT_GREY, 0);
    draw_3d_rect(60, 30, 200, 110, COLOR_LIGHT_GREY, 0);
    draw_rect_fast(63, 33, 194, 12, COLOR_ORANGE);

    draw_char(8,  187, font_S, COLOR_BLACK);
    draw_char(14, 187, font_T, COLOR_BLACK);
    draw_char(20, 187, font_A, COLOR_BLACK);
    draw_char(26, 187, font_R, COLOR_BLACK);

    draw_char(68,  35, font_n, COLOR_WHITE);
    draw_char(74,  35, font_E, COLOR_WHITE);
    draw_char(80,  35, font_X, COLOR_WHITE);
    draw_char(86,  35, font_O, COLOR_WHITE);
    draw_char(92,  35, font_S, COLOR_WHITE);
    draw_char(101, 35, font_v, COLOR_WHITE);
    draw_char(107, 35, font_0, COLOR_WHITE);
    draw_char(111, 35, font_dot, COLOR_WHITE);
    draw_char(115, 35, font_1, COLOR_WHITE);

    draw_mouse(mouse_x, mouse_y);
}

void main() {
    remap_pic();
    init_idt_mouse();
    init_ps2_mouse();

    asm volatile("sti"); 

    render_desktop();

    while(1) {
        if (mouse_updated) {
            mouse_updated = 0;
            render_desktop(); 
        }
    }
}
