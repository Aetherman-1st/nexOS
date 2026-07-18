#ifndef SCREEN_H
#define SCREEN_H

#define VIDEO_ADDRESS 0xb8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLUE 0x1F


int cursor_x = 0;
int cursor_y = 0;


void print_char(char character) {
    char* video_memory = (char*) VIDEO_ADDRESS;

    if (character == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        int offset = (cursor_y * MAX_COLS + cursor_x) * 2;
        video_memory[offset] = character;
        video_memory[offset + 1] = WHITE_ON_BLUE;
        cursor_x++;
    }

    if (cursor_x >= MAX_COLS) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= MAX_ROWS) {
        cursor_x = 0;
        cursor_y = 0;
    }
}


void print(char* string) {
    int i = 0;
    while (string[i] != '\0') {
        print_char(string[i]);
        i++;
    }
}

#endif