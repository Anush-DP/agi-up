/*
 * Created by Matthew Erickson on 20/4/2026.
 *
 * Parse and draw AGI PIC files at original and high resolution.
 *
 * Image is drawn into pic_buf. The actual rendering into OpenGL is done in renderer.c
 */

#include "agi.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Private globals ──────────────────────────────────────────────────── */
static int  current_cmd_id = -1;
static int  current_pic_col;
static bool draw_pic;
static int  pen_size, pen_solid, pen_circle;

/* ── Exported globals ─────────────────────────────────────────────────── */
unsigned char *raw_pic_data = nullptr;
int           raw_pic_len   = 0;
int           pic_scale = 1;
int           pixel_buf[PIC_BUFFER_SIZE];
int           cmd_buf[PIC_BUFFER_SIZE];
int           ref_pixel_buf[PIC_BUFFER_SIZE];
int           ref_cmd_buf[PIC_BUFFER_SIZE];
PicCmd        cmd_log[MAX_CMDS];
int           cmd_count = 0;

/* ── AGI 16-color palette (6-bit source × 4 → 8-bit) ─────────────────── */
const unsigned char color_palette[16][3] = {
    {0x00,0x00,0x00}, /* 0  black       */
    {0x00,0x00,0xA8}, /* 1  blue        */
    {0x00,0xA8,0x00}, /* 2  green       */
    {0x00,0xA8,0xA8}, /* 3  cyan        */
    {0xA8,0x00,0x00}, /* 4  red         */
    {0xA8,0x00,0xA8}, /* 5  magenta     */
    {0xA8,0x54,0x00}, /* 6  brown       */
    {0xA8,0xA8,0xA8}, /* 7  light grey  */
    {0x54,0x54,0x54}, /* 8  dark grey   */
    {0x54,0x54,0xFC}, /* 9  light blue  */
    {0x54,0xFC,0x54}, /* 10 light green */
    {0x54,0xFC,0xFC}, /* 11 light cyan  */
    {0xFC,0x54,0x54}, /* 12 light red   */
    {0xFC,0x54,0xFC}, /* 13 light mag   */
    {0xFC,0xFC,0x54}, /* 14 yellow      */
    {0xFC,0xFC,0xFC}, /* 15 white       */
};

/*
 * Circle brush bitmaps for pen sizes 0-7 (pen action 0xFA).
 * Each size N has 2N+1 rows; each row is one byte, bit 7 = leftmost pixel.
 */
static const unsigned char circle_masks[8][15] = {
    /* 0 */ {0x80},
    /* 1 */ {0xE0,0xE0,0xE0},
    /* 2 */ {0x70,0xF8,0xF8,0xF8,0x70},
    /* 3 */ {0x38,0x7C,0xFE,0xFE,0xFE,0x7C,0x38},
    /* 4 */ {0x1C,0x3E,0x7F,0xFF,0xFF,0xFF,0x7F,0x3E,0x1C},
    /* 5 */ {0x0E,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0x7F,0x3F,0x1F,0x0E},
    /* 6 */ {0x07,0x0F,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0x7F,0x3F,0x1F,0x0F,0x07},
    /* 7 */ {0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03},
};


/* ── Draw a pixel at the current scale ─────────────────────────────────────────────────────── */
static void set_pixel(const int x, const int y) {
    assert(x >= 0 && y >= 0);

    const int scale = pic_scale;
    const int canvas_width = PIC_W * scale;

    for (int dy = 0; dy < scale; dy++) {
        for (int dx = 0; dx < scale; dx++) {
            const int px = x + dx, py = y + dy;
            if (px >= PIC_W * scale || py >= PIC_H * scale) {
                continue;
            }

            if (draw_pic) {
                pixel_buf[py * canvas_width + px] = current_pic_col;
                cmd_buf[py * canvas_width + px] = current_cmd_id;
            }
        }
    }
}


/* ── Line drawing (Sierra's exact algorithm) ──────────────────────────── */
static int agi_round(const float n, const float dir) {
    const float frac = n - floorf(n);
    if (dir < 0.0f) {
        return (frac <= AGI_ROUND_HI) ? (int) floorf(n) : (int) ceilf(n);
    }

    return (frac < AGI_ROUND_LO) ? (int) floorf(n) : (int) ceilf(n);
}

static void draw_line(const int x1, const int y1, const int x2, const int y2) {
    const int height = y2 - y1;
    const int width = x2 - x1;
    const float addX = (height == 0) ? 0.0f : (float) width / (float) abs(height);
    const float addY = (width == 0) ? 0.0f : (float) height / (float) abs(width);

    set_pixel(x1, y1);

    if (abs(width) > abs(height)) {
        float y = (float) y1 + addY;
        const float stepX = (width == 0) ? 0.0f : (float) (width / abs(width));
        for (float x = (float) x1 + stepX; (int) x != x2; x += stepX) {
            set_pixel(agi_round(x, stepX), agi_round(y, addY));
            y += addY;
        }
    } else {
        float x = (float) x1 + addX;
        float stepY = (height == 0) ? 0.0f : (float) (height / abs(height));
        for (float y = (float) y1 + stepY; (int) y != y2; y += stepY) {
            set_pixel(agi_round(x, addX), agi_round(y, stepY));
            x += addX;
        }
    }

    if (x1 != x2 || y1 != y2) {
        set_pixel(x2, y2);
    }
}


/* ── Flood fill (queue-based) ─────────────────────────────────────────── */
typedef struct { int x, y; } Pt;

static void enqueue(const int nx, const int ny, const int canvas_width, Pt *queue, int *tail) {
    const int idx = ny * canvas_width + nx;
    if (pixel_buf[idx] == AGI_COLOR_WHITE) {
        pixel_buf[idx]   = current_pic_col;
        cmd_buf[idx]     = current_cmd_id;
        queue[(*tail)++] = (Pt){nx, ny};
    }
}

static void flood_fill(const int sx, const int sy) {
    static Pt queue[PIC_BUFFER_SIZE];

    const int cw = PIC_W * pic_scale, ch = PIC_H * pic_scale;
    if ((unsigned)sx >= (unsigned)cw || (unsigned)sy >= (unsigned)ch) return;
    if (!draw_pic) return;
    if (pixel_buf[sy * cw + sx] != AGI_COLOR_WHITE) return;

    int head = 0, tail = 0;
    const int sidx = sy * cw + sx;
    pixel_buf[sidx] = current_pic_col;
    cmd_buf[sidx]   = current_cmd_id;
    queue[tail++]   = (Pt){sx, sy};

    while (head < tail) {
        const Pt p = queue[head++];

        if (p.x > 0)      { enqueue(p.x - 1, p.y,     cw, queue, &tail); }
        if (p.x < cw - 1) { enqueue(p.x + 1, p.y,     cw, queue, &tail); }
        if (p.y > 0)      { enqueue(p.x,     p.y - 1, cw, queue, &tail); }
        if (p.y < ch - 1) { enqueue(p.x,     p.y + 1, cw, queue, &tail); }
    }
}

/* ── Pen plot ─────────────────────────────────────────────────────────── */
static void plot_pen(const int cx, const int cy){
    const int diam = pen_size * 2 + 1;

    if (!pen_circle) {
        for (int row = 0; row < diam; row++) {
            for (int col = 0; col < diam; col++) {
                set_pixel(cx - pen_size + col, cy - pen_size + row);
            }
        }
        return;
    }

    for (int row = 0; row < diam; row++) {
        const unsigned char mask = circle_masks[pen_size][row];

        for (int bit = 7; bit >= 0; bit--) {
            if (mask & (1 << bit)) {
                set_pixel(cx - pen_size + (7 - bit), cy - pen_size + row);
            }
        }
    }
}

/* ── Parse the AGI PIC file ─────────────────────────────────────────── */
void pic_init(void) {
    for (int i = 0; i < PIC_BUFFER_SIZE; i++) {
        pixel_buf[i] = AGI_COLOR_WHITE;
        cmd_buf[i] = -1;
    }

    cmd_count = 0;
    current_cmd_id = -1;
    draw_pic = false;
    current_pic_col = 0;
    pen_size = 0;
    pen_solid = 1;
    pen_circle = 1;
}

static unsigned char peek(const unsigned char *d, const int i, const int len, const int offset) {
    return (i + offset < len) ? d[i + offset] : AGI_END;
}

static unsigned char read_byte(const unsigned char *d, int *i) {
    return d[(*i)++];
}

static bool has_data(const unsigned char *d, const int i, const int len) {
    return i < len && d[i] < AGI_OPCODE_MIN;
}

void pic_parse(const unsigned char *d, int len, int max_cmds) {
    int i = 0;
    const int S = pic_scale;

    while (i < len) {
        const unsigned char cmd = read_byte(d, &i);
        if (cmd == AGI_END) { break; }
        if (cmd < AGI_OPCODE_MIN) { continue; }

        if (max_cmds >= 0 && cmd_count >= max_cmds) { break; }

        const int cmd_file_off = i - 1;
        const int arg_start = i;
        if (cmd_count < MAX_CMDS) {
            current_cmd_id = cmd_count;
            cmd_log[cmd_count++] = (PicCmd){current_cmd_id, cmd, cmd_file_off, arg_start, 0};
        }

        switch (cmd) {
            case 0xF0: /* set picture colour and enable drawing */
                current_pic_col = read_byte(d, &i);
                draw_pic = true;
                break;

            case 0xF1: /* disable picture drawing */
                draw_pic = false;
                break;

            case 0xF2: i++; break; /* set priority colour and enable priority drawing */
            case 0xF3: break;      /* disable priority drawing */

            case 0xF4: {
                /* Y-corner lines — alternating vertical/horizontal segments, starting vertical */
                if (!has_data(d, i, len)) { break; }
                int x = read_byte(d, &i);
                if (!has_data(d, i, len)) { break; }
                int y = read_byte(d, &i);
                int toggle = 0;
                while (has_data(d, i, len)) {
                    if (toggle == 0) {
                        const int ny = read_byte(d, &i);
                        draw_line(x * S, y * S, x * S, ny * S);
                        y = ny;
                    } else {
                        const int nx = read_byte(d, &i);
                        draw_line(x * S, y * S, nx * S, y * S);
                        x = nx;
                    }
                    toggle ^= 1;
                }
                break;
            }

            case 0xF5: {
                /* X-corner lines — alternating horizontal/vertical segments, starting horizontal */
                if (!has_data(d, i, len)) { break; }
                int x = read_byte(d, &i);
                if (!has_data(d, i, len)) { break; }
                int y = read_byte(d, &i);
                int toggle = 0;
                while (has_data(d, i, len)) {
                    if (toggle == 0) {
                        const int nx = read_byte(d, &i);
                        draw_line(x * S, y * S, nx * S, y * S);
                        x = nx;
                    } else {
                        const int ny = read_byte(d, &i);
                        draw_line(x * S, y * S, x * S, ny * S);
                        y = ny;
                    }
                    toggle ^= 1;
                }
                break;
            }

            case 0xF6: {
                /* absolute line — sequence of (x,y) endpoints drawn as connected segments */
                if (!has_data(d, i, len)) { break; }
                int x = read_byte(d, &i);
                if (!has_data(d, i, len)) { break; }
                int y = read_byte(d, &i);
                while (has_data(d, i, len) && peek(d, i, len, 1) < AGI_OPCODE_MIN) {
                    const int nx = read_byte(d, &i);
                    const int ny = read_byte(d, &i);
                    draw_line(x * S, y * S, nx * S, ny * S);
                    x = nx;
                    y = ny;
                }
                break;
            }

            case 0xF7: {
                /* relative line — start (x,y) then packed displacement bytes (dx in high nibble, dy in low) */
                if (!has_data(d, i, len)) { break; }
                int x = read_byte(d, &i);
                if (!has_data(d, i, len)) { break; }
                int y = read_byte(d, &i);
                while (has_data(d, i, len)) {
                    const unsigned char disp = read_byte(d, &i);
                    int dx = (disp >> 4) & 7;
                    if (disp & DISP_X_SIGN) { dx = -dx; }
                    int dy = disp & 7;
                    if (disp & DISP_Y_SIGN) { dy = -dy; }
                    if (dx == 0 && dy == 0) {
                        set_pixel(x * S, y * S);
                    } else {
                        draw_line(x * S, y * S, (x + dx) * S, (y + dy) * S);
                        x += dx;
                        y += dy;
                    }
                }
                break;
            }

            case 0xF8: /* flood fill — one or more (x,y) seed coordinates */
                while (has_data(d, i, len) && peek(d, i, len, 1) < AGI_OPCODE_MIN) {
                    const int fx = read_byte(d, &i);
                    const int fy = read_byte(d, &i);
                    flood_fill(fx * S + S / 2, fy * S + S / 2);
                }
                break;

            case 0xF9: {
                /* set pen — flags byte encodes size, circle/rectangle shape, solid/splatter fill */
                const unsigned char flags = read_byte(d, &i);
                pen_solid  = !(flags & PEN_FLAG_SPLATTER);
                pen_circle = !(flags & PEN_FLAG_RECT);
                pen_size   = flags & PEN_FLAG_SIZE;
                break;
            }

            case 0xFA: /* plot pen — one or more (x,y) positions; splatter pen prepends a texture byte */
                while (has_data(d, i, len)) {
                    if (!pen_solid) {
                        if (peek(d, i, len, 1) >= AGI_OPCODE_MIN || peek(d, i, len, 2) >= AGI_OPCODE_MIN) { break; }
                        i++;
                    }
                    if (!has_data(d, i, len) || peek(d, i, len, 1) >= AGI_OPCODE_MIN) { break; }
                    const int px = read_byte(d, &i);
                    const int py = read_byte(d, &i);
                    {
                        const int saved = pen_size;
                        pen_size = (pen_size * S > MAX_PEN_SIZE_HIRES) ? MAX_PEN_SIZE_HIRES : pen_size * S;
                        plot_pen(px * S, py * S);
                        pen_size = saved;
                    }
                }
                break;

            default:
                break;
        }

        if (current_cmd_id >= 0) {
            cmd_log[current_cmd_id].arg_len = i - arg_start;
        }
    }
}


/* ── Public API ───────────────────────────────────────────────────────── */
void first_parse() {
    pic_init();
    if (!raw_pic_data) {
        exit(EXIT_FAILURE);
    }

    pic_parse(raw_pic_data, raw_pic_len, -1);

    // TODO - bug where enhance doesn't work properly when image is half rendered
    // probably not with these buffers pre-se
    memcpy(ref_pixel_buf, pixel_buf, sizeof(pixel_buf));
    memcpy(ref_cmd_buf,   cmd_buf,   sizeof(cmd_buf));

}

void parse_to_step(int n) {
    pic_init();
    if (n <= 0 || !raw_pic_data) return;

    pic_parse(raw_pic_data, raw_pic_len, n);
}
