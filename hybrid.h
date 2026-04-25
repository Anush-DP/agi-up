/*
 * Created by Matthew Erickson on 23/4/2026.
 *
 */

#pragma once

#include "agi.h"

#define HYBRID_W           (PIC_W * PIC_PX_W)
#define HYBRID_H           (PIC_H * PIC_PX_H)
#define HYBRID_BUFFER_SIZE (HYBRID_W * HYBRID_H)

/*
 * Represents one anchor in the vectorized image.
 *
 * grid_x / grid_y     — cell position in the AGI pixel grid
 * screen_x / screen_y — projected position in high resolution pixel space
 * connects_*          — true if this anchor has a drawn connection in that compass direction
 */
typedef struct {
    int  grid_x,   grid_y;
    int  screen_x, screen_y;
    bool connects_n;
    bool connects_ne;
    bool connects_e;
    bool connects_se;
    bool connects_s;
    bool connects_sw;
    bool connects_w;
    bool connects_nw;
    bool is_endpoint;
} Anchor;

unsigned char hybrid_pixel_buf[HYBRID_BUFFER_SIZE];
unsigned char hybrid_anchor_buf[HYBRID_BUFFER_SIZE];
unsigned char hybrid_cmd_type_buf[HYBRID_BUFFER_SIZE];

// Public API
extern void parse_enhanced(void);
extern void enhance(int mode);