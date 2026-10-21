#ifndef OLED_H
#define OLED_H

#include <stdbool.h>

#include "hardware/i2c.h"

#define OLED_WIDTH      128
#define OLED_HEIGHT     32
#define OLED_CHAR_WIDTH 6

bool oled_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin);
void oled_clear(void);
void oled_char(int x, int y, char c, int scale);
void oled_text(int x, int y, const char *s, int scale);
void oled_invert(int x, int y, int w, int h);
void oled_show(void);

#endif
