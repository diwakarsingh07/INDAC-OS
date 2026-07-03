#include "terminal.h"
#include "font.h"
#include "string.h"
#include "gui.h" // For gui_draw_scaled_text
#include "wm.h"
#include <stddef.h>

#define TERM_ROWS 25
#define TERM_COLS 80

static char term_buffer[TERM_ROWS][TERM_COLS];
static int cursor_x = 0;
static int cursor_y = 0;

void terminal_init(uint32_t* fb, int width, int height, int pitch) {
    (void)fb; (void)width; (void)height; (void)pitch;
    terminal_clear();
}

void terminal_clear(void) {
    for (int y = 0; y < TERM_ROWS; y++) {
        for (int x = 0; x < TERM_COLS; x++) {
            term_buffer[y][x] = ' ';
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') { // Backspace
        if (cursor_x > 0) {
            cursor_x--;
            term_buffer[cursor_y][cursor_x] = ' ';
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = TERM_COLS - 1;
            term_buffer[cursor_y][cursor_x] = ' ';
        }
    } else {
        term_buffer[cursor_y][cursor_x] = c;
        cursor_x++;
        if (cursor_x >= TERM_COLS) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    
    // Scrolling
    if (cursor_y >= TERM_ROWS) {
        for (int y = 1; y < TERM_ROWS; y++) {
            for (int x = 0; x < TERM_COLS; x++) {
                term_buffer[y-1][x] = term_buffer[y][x];
            }
        }
        for (int x = 0; x < TERM_COLS; x++) {
            term_buffer[TERM_ROWS-1][x] = ' ';
        }
        cursor_y = TERM_ROWS - 1;
    }
}

void terminal_print(const char* str) {
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        terminal_putchar(str[i]);
    }
}

// Called by apps.c (Window Manager) every frame
void terminal_draw_in_window(struct window* win) {
    int start_x = win->x + 10;
    int start_y = win->y + 10;
    
    // We can draw the text using gui_draw_scaled_text line by line
    for (int y = 0; y < TERM_ROWS; y++) {
        char line[TERM_COLS + 1];
        for (int x = 0; x < TERM_COLS; x++) {
            line[x] = term_buffer[y][x];
        }
        line[TERM_COLS] = '\0';
        
        gui_draw_scaled_text(line, start_x, start_y + (y * 16), 0x0000FF00, 1);
    }
    
    // Draw cursor block
    gui_draw_rect(start_x + (cursor_x * 8), start_y + (cursor_y * 16), 8, 16, 0x0000FF00);
}
