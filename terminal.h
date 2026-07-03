#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

struct window;

// Initialize terminal to draw to a specific buffer
void terminal_init(uint32_t* fb, int width, int height, int pitch);

// Redirect terminal drawing to a different buffer
void terminal_set_buffer(uint32_t* fb, int width, int height, int pitch);

// Set the boundary rect for the terminal
void terminal_set_bounds(int x, int y, int w, int h);
void terminal_putchar(char c);
void terminal_print(const char* str);
void terminal_clear(void);
void terminal_draw_in_window(struct window* win);

#endif
