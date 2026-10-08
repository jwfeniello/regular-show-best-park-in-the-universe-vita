#pragma once

/* Geometric face-button symbols; private-use characters for shoulder badges. */
#define PARK_ICON_SQUARE   "\xe2\x96\xa1"
#define PARK_ICON_TRIANGLE "\xe2\x96\xb3"
#define PARK_ICON_CIRCLE   "\xe2\x97\x8b"
#define PARK_ICON_CROSS    "\xc3\x97"
#define PARK_ICON_L        "\xee\x80\x80"
#define PARK_ICON_R        "\xee\x80\x81"

int park_button_glyph(int codepoint);
float park_button_advance(int codepoint, int size);
void park_button_draw(int codepoint, int size, unsigned char *rgba,
                      int width, int height, float x, int baseline);
