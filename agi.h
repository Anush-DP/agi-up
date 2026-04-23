/*
 * Created by Matthew Erickson on 20/4/2026.
 *
 */

#pragma once

/* ── Native pixel aspect ratio (EGA 320×200 on 4:3 CRT) ──────────────── */
#define PIC_PX_W  10   /* screen pixels per AGI pixel, horizontal */
#define PIC_PX_H   6   /* screen pixels per AGI pixel, vertical   */

/* ── AGI canvas dimensions ────────────────────────────────────────────── */
#define PIC_W         160
#define PIC_H         168
#define PIC_MAX_SCALE   6     /* maximum hi-res upscale factor (H key)   */
#define PIC_BUFFER_SIZE (PIC_W * PIC_MAX_SCALE * PIC_H * PIC_MAX_SCALE)

/* ── Native pixel aspect ratio (EGA 320×200 on 4:3 CRT) ──────────────── */
#define PIXEL_W  10   /* screen pixels per AGI pixel, horizontal */
#define PIXEL_H   6   /* screen pixels per AGI pixel, vertical   */

/* ── AGI palette / opcode constants ──────────────────────────────────── */
#define AGI_COLOR_WHITE  15   /* background / unfilled color index      */
#define AGI_NUM_COLORS   16
#define AGI_OPCODE_MIN  0xF0  /* first valid picture opcode              */
#define AGI_END         0xFF  /* end-of-picture marker                   */

/* Sierra Bresenham rounding thresholds */
#define AGI_ROUND_LO  0.499f
#define AGI_ROUND_HI  0.501f

/* F7 relative-line displacement byte */
#define DISP_X_SIGN  0x80
#define DISP_Y_SIGN  0x08

/* F9 "set pen" flags */
#define PEN_FLAG_SPLATTER  0x20
#define PEN_FLAG_RECT      0x10
#define PEN_FLAG_SIZE      0x07

/* Maximum pen-size index in hi-res mode */
#define MAX_PEN_SIZE_HIRES  6

/* ── Exported globals ─────────────────────────────────────────────────── */
extern unsigned char *raw_pic_data;
extern int            raw_pic_len;
extern int            cmd_count;

/* ── Pixel and command buffers ───────────────────────────────────────── */
extern int  pixel_buf[PIC_BUFFER_SIZE];
extern int  cmd_buf[PIC_BUFFER_SIZE];


/* ── Palette (read by renderer for RGB conversion) ────────────────────── */
extern const unsigned char color_palette[16][3];


/* ── Types ────────────────────────────────────────────────────────────── */
typedef struct {
    int     id;
    int     opcode;
    int     file_offset;
    int     arg_offset;
    int     arg_len;
} PicCmd;

/* ── Scale settings ──────────────────────────────────────────────────── */
extern int            pic_scale;

/* ── Command log ─────────────────────────────────────────────────────── */
#define MAX_CMDS      4096
extern PicCmd         cmd_log[MAX_CMDS];


/* ── Public API ───────────────────────────────────────────────────────── */
void first_parse();
void parse_to_step(int n);