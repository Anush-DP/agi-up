/*
 * Created by Matthew Erickson on 23/4/2026.
 */

#include "hybrid.h"

#include <stdlib.h>
#include <string.h>

#include "agi.h"

#define CELL_W PIC_PX_W
#define CELL_H PIC_PX_H

Anchor anchors[PIC_W * PIC_H];

/* Direction tables shared by all connection passes.
 * Index order: N=0 NE=1 E=2 SE=3 S=4 SW=5 W=6 NW=7 */
static constexpr int dir_dx[8] = {  0,  1, 1,  1, 0, -1, -1, -1 };
static constexpr int dir_dy[8] = { -1, -1, 0,  1, 1,  1,  0, -1 };

static void set_connect(Anchor *anchor, int dir)
{
    switch (dir) {
        case 0: anchor->connects_n  = true; break;
        case 1: anchor->connects_ne = true; break;
        case 2: anchor->connects_e  = true; break;
        case 3: anchor->connects_se = true; break;
        case 4: anchor->connects_s  = true; break;
        case 5: anchor->connects_sw = true; break;
        case 6: anchor->connects_w  = true; break;
        case 7: anchor->connects_nw = true; break;
    }
}

static bool get_connect(const Anchor *anchor, int dir)
{
    switch (dir) {
        case 0: return anchor->connects_n;
        case 1: return anchor->connects_ne;
        case 2: return anchor->connects_e;
        case 3: return anchor->connects_se;
        case 4: return anchor->connects_s;
        case 5: return anchor->connects_sw;
        case 6: return anchor->connects_w;
        case 7: return anchor->connects_nw;
        default: return false;
    }
}


/* TODO - rewrite this comment
 * Project cell (cx,cy) onto segment (sx,sy)→(ex,ey), all in hybrid space.
 * Returns squared distance from cell to projection; writes projection to *px,*py.
 */
static float project_onto_segment(
    float cx, float cy,
    float sx, float sy, float ex, float ey,
    int *px, int *py) {
    const float dx = ex - sx, dy = ey - sy;
    const float len2 = dx*dx + dy*dy;
    float t = 0.0f;

    if (len2 > 0.0f) {
        t = ((cx - sx)*dx + (cy - sy)*dy) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    const float qx = sx + t*dx;
    const float qy = sy + t*dy;
    *px = (int)(qx + 0.5f);
    *py = (int)(qy + 0.5f);
    const float ddx = cx - qx, ddy = cy - qy;

    return ddx*ddx + ddy*ddy;
}

static void process_segment(
    const int x0, const int y0, const int x1, const int y1,
    const int cmd_id,
    int *anchor_x, int *anchor_y, float *best_dist2) {
    const float svx = x0 * CELL_W + CELL_W * 0.5f;
    const float svy = y0 * CELL_H + CELL_H * 0.5f;
    const float evx = x1 * CELL_W + CELL_W * 0.5f;
    const float evy = y1 * CELL_H + CELL_H * 0.5f;
    const int min_cx = x0 < x1 ? x0 : x1;
    const int max_cx = x0 > x1 ? x0 : x1;
    const int min_cy = y0 < y1 ? y0 : y1;
    const int max_cy = y0 > y1 ? y0 : y1;

    for (int cy = min_cy; cy <= max_cy; cy++) {
        for (int cx = min_cx; cx <= max_cx; cx++) {
            if ((unsigned)cx >= (unsigned)PIC_W || (unsigned)cy >= (unsigned)PIC_H) { continue; }
            if (ref_cmd_buf[cy * PIC_W + cx] != cmd_id) { continue; }
            const float ccx = cx * CELL_W + CELL_W * 0.5f;
            const float ccy = cy * CELL_H + CELL_H * 0.5f;
            int qx, qy;
            const float dist2 = project_onto_segment(ccx, ccy, svx, svy, evx, evy, &qx, &qy);
            const int idx = cy * PIC_W + cx;

            if (dist2 < best_dist2[idx]) {
                best_dist2[idx] = dist2;
                anchor_x[idx]   = qx;
                anchor_y[idx]   = qy;
            }
        }
    }
}

/*
 * Reposition anchors for every cell owned by the given line command.
 * Iterates all segments described by the command's args, projecting each
 * owned cell onto the nearest segment.
 * best_dist2[] must be pre-initialised to a large value (e.g. FLT_MAX).
 */
static void reposition_for_cmd(
    const PicCmd *cmd,
    int *anchor_x, int *anchor_y,
    float *best_dist2) {
    const unsigned char *args = raw_pic_data + cmd->arg_offset;
    const int            len  = cmd->arg_len;
    if (len < 2 || !raw_pic_data) return;

    if (cmd->opcode == 0xF6) {
        /* absolute line: x0,y0 then pairs x1,y1 … */
        if (len < 4) return;
        int x = args[0], y = args[1];
        for (int k = 2; k + 1 < len; k += 2) {
            const int nx = args[k], ny = args[k+1];
            process_segment(x, y, nx, ny, cmd->id, anchor_x, anchor_y, best_dist2);
            x = nx; y = ny;
        }

    } else if (cmd->opcode == 0xF4) {
        /* Y-corner: x,y then alternating ny,nx … */
        if (len < 3) return;
        int x = args[0], y = args[1];
        int toggle = 0;
        for (int k = 2; k < len; k++, toggle ^= 1) {
            if (toggle == 0) { const int ny = args[k]; process_segment(x,y,x,ny, cmd->id, anchor_x, anchor_y, best_dist2); y=ny; }
            else             { const int nx = args[k]; process_segment(x,y,nx,y, cmd->id, anchor_x, anchor_y, best_dist2); x=nx; }
        }

    } else if (cmd->opcode == 0xF5) {
        /* X-corner: x,y then alternating nx,ny … */
        if (len < 3) return;
        int x = args[0], y = args[1];
        int toggle = 0;
        for (int k = 2; k < len; k++, toggle ^= 1) {
            if (toggle == 0) { const int nx = args[k]; process_segment(x,y,nx,y, cmd->id, anchor_x, anchor_y, best_dist2); x=nx; }
            else             { const int ny = args[k]; process_segment(x,y,x,ny, cmd->id, anchor_x, anchor_y, best_dist2); y=ny; }
        }

    } else if (cmd->opcode == 0xF7) {
        /* relative line: each displacement byte is its own segment */
        if (len < 2) return;
        int x = args[0], y = args[1];
        for (int k = 2; k < len; k++) {
            const unsigned char disp = args[k];
            int dx = (disp >> 4) & 7; if (disp & DISP_X_SIGN) { dx = -dx; }
            int dy =  disp       & 7; if (disp & DISP_Y_SIGN)  { dy = -dy; }
            if (dx != 0 || dy != 0) {
                process_segment(x, y, x+dx, y+dy, cmd->id, anchor_x, anchor_y, best_dist2);
                x += dx; y += dy;
            }
        }
    }
}

/*
 * Initialise anchors: project each cell's anchor onto the nearest segment
 * of its line command (or leave it at cell centre for fill/background cells).
 */
static void build_anchors(void) {
    static int   anchor_x[PIC_W * PIC_H];
    static int   anchor_y[PIC_W * PIC_H];
    static float best_dist2[PIC_W * PIC_H];

    for (int y = 0; y < PIC_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            const int idx = y * PIC_W + x;
            anchor_x[idx]   = x * CELL_W + CELL_W / 2;
            anchor_y[idx]   = y * CELL_H + CELL_H / 2;
            best_dist2[idx] = 1e30f;
        }
    }

    for (int c = 0; c < cmd_count; c++) {
        const unsigned char op = cmd_log[c].opcode;
        if (op == 0xF4 || op == 0xF5 || op == 0xF6 || op == 0xF7) {
            reposition_for_cmd(&cmd_log[c], anchor_x, anchor_y, best_dist2);
        }
    }

    for (int y = 0; y < PIC_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            const int idx = y * PIC_W + x;
            anchors[idx] = (Anchor){
                .grid_x   = x,
                .grid_y   = y,
                .screen_x = anchor_x[idx],
                .screen_y = anchor_y[idx],
            };
        }
    }
}

/* Returns true if cmd_id refers to a non-fill line command. */
static bool is_line_cmd(int cmd_id) {
    return cmd_id >= 0 && cmd_log[cmd_id].opcode != 0xF8;
}

/*
 * Pass A: calculate connections for line-command anchors.
 *
 */
static void connect_line_anchors(void) {
    for (int y = 0; y < PIC_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            const int     idx    = y * PIC_W + x;
            const int cmd_id = ref_cmd_buf[idx];
            if (!is_line_cmd(cmd_id)) continue;

            const unsigned char color  = ref_pixel_buf[idx];
            Anchor             *anchor = &anchors[idx];

            int same_cmd_count = 0;

            // Connect to all same-colour neighbours
            for (int dir = 0; dir < 8; dir++) {
                const int nx = x + dir_dx[dir];
                const int ny = y + dir_dy[dir];
                if ((unsigned)nx >= (unsigned)PIC_W ||
                    (unsigned)ny >= (unsigned)PIC_H) continue;
                const int nidx = ny * PIC_W + nx;
                // if (ref_pixel_buf[nidx] == color && is_line_cmd(ref_cmd_buf[nidx])) { // TODO - should we connect only to other lines?
                //     set_connect(anchor, dir);
                //     if (ref_cmd_buf[nidx] == cmd_id) { same_cmd_count++; }
                // }
                if (ref_pixel_buf[nidx] == color) { // TODO - should we connect only to other lines?
                    set_connect(anchor, dir);
                    if (ref_cmd_buf[nidx] == cmd_id) { same_cmd_count++; }
                }
                // if (ref_cmd_buf[nidx] == cmd_id) {
                //     set_connect(anchor, dir);
                //     same_cmd_count++;
                // }
            }

            if (same_cmd_count >= 2) {
                /* Interior: no additional connections needed. */
            } else {
                /* End-point. One connection per foreign same-colour command */
                int     picked_cmd[8];
                int     picked_dir[8];
                bool    picked_has_opp[8];
                int     npicked              = 0;
                bool    has_forward_connect  = false;

                for (int dir = 0; dir < 8; dir++) {
                    const int nx = x + dir_dx[dir];
                    const int ny = y + dir_dy[dir];
                    if ((unsigned)nx >= (unsigned)PIC_W ||
                        (unsigned)ny >= (unsigned)PIC_H) continue;
                    const int nidx = ny * PIC_W + nx;
                    if (ref_pixel_buf[nidx] != color) continue;
                    const int cmd_n = ref_cmd_buf[nidx];

                    if (cmd_n == cmd_id) continue;

                    const int  opp     = (dir + 4) % 8;
                    const int  ox      = x + dir_dx[opp];
                    const int  oy      = y + dir_dy[opp];
                    const bool has_opp =
                        (unsigned)ox < (unsigned)PIC_W &&
                        (unsigned)oy < (unsigned)PIC_H &&
                        ref_cmd_buf[oy * PIC_W + ox] == cmd_id;

                    int slot = -1;

                    for (int k = 0; k < npicked; k++) {
                        if (picked_cmd[k] == cmd_n) { slot = k; break; }
                    }

                    if (slot < 0) {
                        picked_cmd[npicked]     = cmd_n;
                        picked_dir[npicked]     = dir;
                        picked_has_opp[npicked] = has_opp;
                        npicked++;
                    } else if (has_opp && !picked_has_opp[slot]) {
                        picked_dir[slot]     = dir;
                        picked_has_opp[slot] = true;
                    }
                }

                for (int k = 0; k < npicked; k++) {
                    set_connect(anchor, picked_dir[k]);
                    has_forward_connect = true;
                }

                /*
                 * Fallback: this endpoint has no forward same-colour neighbour
                 * to connect to.  Connect in the travel direction (opposite to
                 * the single same-command neighbour behind us) regardless of
                 * the destination anchor's colour.
                 */
                if (!has_forward_connect && same_cmd_count == 1) {
                    for (int dir = 0; dir < 8; dir++) {
                        const int nx = x + dir_dx[dir];
                        const int ny = y + dir_dy[dir];
                        if ((unsigned)nx >= (unsigned)PIC_W ||
                            (unsigned)ny >= (unsigned)PIC_H) continue;
                        if (ref_cmd_buf[ny * PIC_W + nx] != cmd_id) continue;
                        /* dir points back along the line; forward is opposite */
                        const int fwd = (dir + 4) % 8;
                        const int fx  = x + dir_dx[fwd];
                        const int fy  = y + dir_dy[fwd];
                        if ((unsigned)fx < (unsigned)PIC_W &&
                            (unsigned)fy < (unsigned)PIC_H &&
                            is_line_cmd(ref_cmd_buf[fy * PIC_W + fx])) {
                            set_connect(anchor, fwd);
                        }
                        break;
                    }
                }
            }
        }
    }
}

/*
 * Bresenham line into vec_buf and vec_cmd_type_buf.
 * The first half of the pixels use color_a; the second half use color_b.
 * Pass the same value for both to get a solid-color line.
 */
static void draw_line(int x1, int y1, int x2, int y2,
                          const unsigned char color_a, const unsigned char color_b,
                          const unsigned char pix_type)
{
    const int dx    = abs(x2 - x1), dy = abs(y2 - y1);
    const int sx    = (x1 < x2) ? 1 : -1;
    const int sy    = (y1 < y2) ? 1 : -1;
    const int total = (dx > dy ? dx : dy) + 1;
    const int half  = total / 2;
    int err  = dx - dy;
    int step = 0;
    while (true) {
        if ((unsigned)x1 < (unsigned)HYBRID_W && (unsigned)y1 < (unsigned)HYBRID_H) {
            const int idx = y1 * HYBRID_W + x1;
            hybrid_pixel_buf[idx]          = (step < half) ? color_a : color_b;
            hybrid_cmd_type_buf[idx] = pix_type;
        }
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
        step++;
    }
}

/*
 * Render all connections and anchor dots into vec_buf / vec_anchor_buf.
 * Connections are drawn first so anchor dots paint on top.
 * Each pair is checked from the west/north cell only (OR rule covers both
 * sides), avoiding double-drawing.
 */
static void hybrid_render(void)
{
    memset(hybrid_pixel_buf,          0xFF, sizeof(hybrid_pixel_buf));
    memset(hybrid_anchor_buf,   0,   sizeof(hybrid_anchor_buf));
    memset(hybrid_cmd_type_buf, 0,   sizeof(hybrid_cmd_type_buf));

    for (int y = 0; y < PIC_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            const int           idx    = y * PIC_W + x;
            const Anchor       *anchor = &anchors[idx];
            const unsigned char color  = ref_pixel_buf[idx];
            const int           cmd_id = ref_cmd_buf[idx];
            const unsigned char pix_type = (cmd_id < 0)                     ? 0
                                         : (cmd_log[cmd_id].opcode == 0xF8) ? 2
                                                                             : 1;

            if (x + 1 < PIC_W) {
                const int    nidx = idx + 1;
                const Anchor *nb  = &anchors[nidx];
                if (anchor->connects_e || nb->connects_w) {
                    draw_line(anchor->screen_x, anchor->screen_y,
                                  nb->screen_x,     nb->screen_y,
                                  color, ref_pixel_buf[nidx], pix_type);
                }
            }
            if (x + 1 < PIC_W && y + 1 < PIC_H) {
                const int    nidx = idx + PIC_W + 1;
                const Anchor *nb  = &anchors[nidx];
                if (anchor->connects_se || nb->connects_nw) {
                    draw_line(anchor->screen_x, anchor->screen_y,
                                  nb->screen_x,     nb->screen_y,
                                  color, ref_pixel_buf[nidx], pix_type);
                }
            }
            if (y + 1 < PIC_H) {
                const int    nidx = idx + PIC_W;
                const Anchor *nb  = &anchors[nidx];
                if (anchor->connects_s || nb->connects_n) {
                    draw_line(anchor->screen_x, anchor->screen_y,
                                  nb->screen_x,     nb->screen_y,
                                  color, ref_pixel_buf[nidx], pix_type);
                }
            }
            if (x > 0 && y + 1 < PIC_H) {
                const int    nidx = idx + PIC_W - 1;
                const Anchor *nb  = &anchors[nidx];
                if (anchor->connects_sw || nb->connects_ne) {
                    draw_line(anchor->screen_x, anchor->screen_y,
                                  nb->screen_x,     nb->screen_y,
                                  color, ref_pixel_buf[nidx], pix_type);
                }
            }
        }
    }

    /* Canvas-edge connections: draw from border anchors to the nearest boundary pixel. */
    for (int y = 0; y < PIC_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            const int           idx      = y * PIC_W + x;
            const Anchor       *anchor   = &anchors[idx];
            const unsigned char color    = ref_pixel_buf[idx];
            const int           cmd_id   = ref_cmd_buf[idx];
            const unsigned char pix_type = (cmd_id < 0)                     ? 0
                                         : (cmd_log[cmd_id].opcode == 0xF8) ? 2
                                                                             : 1;
            for (int dir = 0; dir < 8; dir++) {
                const int nx = x + dir_dx[dir];
                const int ny = y + dir_dy[dir];
                if ((unsigned)nx < (unsigned)PIC_W && (unsigned)ny < (unsigned)PIC_H) continue;
                if (!get_connect(anchor, dir)) continue;
                const int bx = dir_dx[dir] > 0 ? HYBRID_W - 1
                             : dir_dx[dir] < 0 ? 0
                             : anchor->screen_x;
                const int by = dir_dy[dir] > 0 ? HYBRID_H - 1
                             : dir_dy[dir] < 0 ? 0
                             : anchor->screen_y;
                draw_line(anchor->screen_x, anchor->screen_y, bx, by, color, color, pix_type);
            }
        }
    }

    for (int idx = 0; idx < PIC_W * PIC_H; idx++) {
        const int sx = anchors[idx].screen_x;
        const int sy = anchors[idx].screen_y;

        if ((unsigned)sx < (unsigned)HYBRID_W && (unsigned)sy < (unsigned)HYBRID_H) {
            const int           cmd_id   = ref_cmd_buf[idx];
            const unsigned char pix_type = (cmd_id < 0)                        ? 0
                                         : (cmd_log[cmd_id].opcode == 0xF8)    ? 2
                                                                                : 1;
            const int pixel = sy * HYBRID_W + sx;
            hybrid_pixel_buf[pixel]          = ref_pixel_buf[idx];
            hybrid_cmd_type_buf[pixel] = pix_type;
            hybrid_anchor_buf[pixel]   = 1;
        }
    }
}

/*
 * Pass B: calculate connections for fill/background anchors.
 *
 * Cardinal connections: allowed unless the neighbour is a line cell.
 * Diagonal connections: additionally suppressed when either flanking cardinal
 * neighbour is a line cell (the diagonal would visually cross it).
 */
static void connect_fill_anchors(void)
{
    /* For each diagonal: the two flanking cardinal directions.
     *   NE (1): N (0) and E (2)
     *   SE (3): S (4) and E (2)
     *   SW (5): S (4) and W (6)
     *   NW (7): N (0) and W (6)  */
    static const int diag_dirs[4]   = { 1, 3, 5, 7 };
    static const int diag_card_a[4] = { 0, 4, 4, 0 };
    static const int diag_card_b[4] = { 2, 2, 6, 6 };

    for (int y = 0; y < PIC_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            const int     idx    = y * PIC_W + x;
            const int cmd_id = ref_cmd_buf[idx];
            if (is_line_cmd(cmd_id)) continue;

            const unsigned char color  = ref_pixel_buf[idx];
            Anchor             *anchor = &anchors[idx];

            for (int dir = 0; dir < 8; dir += 2) {
                const int nx = x + dir_dx[dir];
                const int ny = y + dir_dy[dir];
                const bool oob = (unsigned)nx >= (unsigned)PIC_W ||
                                 (unsigned)ny >= (unsigned)PIC_H;
                if (!oob) {
                    const int nidx = ny * PIC_W + nx;
                    if (ref_pixel_buf[nidx] != color) continue;
                    if (is_line_cmd(ref_cmd_buf[nidx])) continue;
                }
                set_connect(anchor, dir);
            }

            for (int d = 0; d < 4; d++) {
                const int diag = diag_dirs[d];
                const int dnx  = x + dir_dx[diag];
                const int dny  = y + dir_dy[diag];
                if ((unsigned)dnx >= (unsigned)PIC_W ||
                    (unsigned)dny >= (unsigned)PIC_H) continue;
                if (ref_pixel_buf[dny * PIC_W + dnx] != color) continue;
                if (is_line_cmd(ref_cmd_buf[dny * PIC_W + dnx])) continue;

                const int ax = x + dir_dx[diag_card_a[d]];
                const int ay = y + dir_dy[diag_card_a[d]];
                const int bx = x + dir_dx[diag_card_b[d]];
                const int by = y + dir_dy[diag_card_b[d]];
                if ((unsigned)ax < (unsigned)PIC_W && (unsigned)ay < (unsigned)PIC_H &&
                    (unsigned)bx < (unsigned)PIC_W && (unsigned)by < (unsigned)PIC_H) {
                    if (is_line_cmd(ref_cmd_buf[ay * PIC_W + ax]) &&
                        is_line_cmd(ref_cmd_buf[by * PIC_W + bx])) continue;
                }

                set_connect(anchor, diag);
            }
        }
    }
}

/* ── Image enhancement passes ────────────────────────────────────────────── */

/* Returns true if a pixel of the given type qualifies as a source for mode. */
static bool enhance_type_eligible(const unsigned char pix_type, const int mode) {
    if (mode == 1) { return pix_type == 1; }           /* lines only */
    if (mode == 2) { return pix_type == 0 || pix_type == 2; } /* fill / no-cmd */
    return true;                                        /* mode 0: all */
}

/* Determine the propagated type when a pixel is newly coloured.
 * For a fixed mode the type is unambiguous; for mode 0 (all) we use
 * whichever source type (line vs fill/none) contributed more neighbours. */
static unsigned char enhance_result_type(const int mode,
                                         const int line_count,
                                         const int fill_count) {
    if (mode == 1) { return 1; }
    if (mode == 2) { return 2; }
    return (line_count >= fill_count) ? 1 : 2;
}

/*
 * Run one iteration of the enhance kernel over vec_buf.
 *
 * mode 0 (','): all coloured pixels are eligible sources.
 * mode 1 ('.'): only pixels written by line commands are eligible.
 * mode 2 ('/'): only pixels written by fill or no-command are eligible.
 *
 * A snapshot of both hybrid_pixel_buf and hybrid_cmd_type_buf is taken first so all
 * reads see the pre-iteration state (simultaneous update).
 */
void enhance(const int mode) {
    static unsigned char src[HYBRID_BUFFER_SIZE];
    static unsigned char src_type[HYBRID_BUFFER_SIZE];
    memcpy(src,      hybrid_pixel_buf,    sizeof(hybrid_pixel_buf));
    memcpy(src_type, hybrid_cmd_type_buf, sizeof(hybrid_cmd_type_buf));

    for (int y = 0; y < HYBRID_H; y++) {
        for (int x = 0; x < HYBRID_W; x++) {
            const bool is_background   = (src[y * HYBRID_W + x] == 0xFF);
            const bool is_fill_pixel   = (src_type[y * HYBRID_W + x] == 2);
            const bool overrideable    = (mode == 1 && is_fill_pixel);
            if (!is_background && !overrideable) continue;

            /* Count line-command neighbours unconditionally to decide
             * whether fill votes should be suppressed. */
            int line_neighbours = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if ((unsigned)nx >= (unsigned)HYBRID_W ||
                        (unsigned)ny >= (unsigned)HYBRID_H) continue;
                    const unsigned char nc = src[ny * HYBRID_W + nx];
                    if (nc >= 16) continue;
                    if (src_type[ny * HYBRID_W + nx] == 1) line_neighbours++;
                }
            }
            const bool suppress_fill = (line_neighbours >= 3);

            int count[16] = {0};
            int line_count = 0, fill_count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if ((unsigned)nx >= (unsigned)HYBRID_W ||
                        (unsigned)ny >= (unsigned)HYBRID_H) continue;
                    const unsigned char nc = src[ny * HYBRID_W + nx];
                    if (nc >= 16) continue;
                    const unsigned char nt = src_type[ny * HYBRID_W + nx];
                    if (suppress_fill && nt != 1) continue;
                    if (!enhance_type_eligible(nt, mode)) continue;
                    count[nc]++;
                    if (nt == 1) { line_count++; } else { fill_count++; }
                }
            }

            /* Collect all colours that share the highest count.
             * Mode 1 (line-only) uses ≥2 so single-pixel 45° diagonals are
             * enhanced; all other modes require the stricter ≥3 threshold. */
            const int min_votes = (mode == 1) ? 1 : 2;
            int best_count = min_votes;
            int tied[16];
            int ntied = 0;
            for (int c = 0; c < 16; c++) {
                if (count[c] > best_count) {
                    best_count = count[c];
                    ntied = 0;
                    tied[ntied++] = c;
                } else if (count[c] == best_count && best_count > min_votes) {
                    tied[ntied++] = c;
                }
            }

            unsigned char result_color = 0xFF;
            if (ntied == 1) {
                result_color = (unsigned char)tied[0];
            } else if (ntied > 1) {
                /* Tie: average the RGB values of the tied colours and pick
                 * the palette entry nearest to that average. */
                int sum_r = 0, sum_g = 0, sum_b = 0;
                for (int k = 0; k < ntied; k++) {
                    sum_r += color_palette[tied[k]][0];
                    sum_g += color_palette[tied[k]][1];
                    sum_b += color_palette[tied[k]][2];
                }
                const int avg_r = sum_r / ntied;
                const int avg_g = sum_g / ntied;
                const int avg_b = sum_b / ntied;
                int nearest = 0, nearest_dist2 = 0x7fffffff;
                for (int c = 0; c < 16; c++) {
                    const int dr = avg_r - (int)color_palette[c][0];
                    const int dg = avg_g - (int)color_palette[c][1];
                    const int db = avg_b - (int)color_palette[c][2];
                    const int dist2 = dr*dr + dg*dg + db*db;
                    if (dist2 < nearest_dist2) {
                        nearest = c;
                        nearest_dist2 = dist2;
                    }
                }
                result_color = (unsigned char)nearest;
            }

            if (result_color != 0xFF) {
                const int pixel = y * HYBRID_W + x;
                hybrid_pixel_buf[pixel]          = result_color;
                hybrid_cmd_type_buf[pixel] = enhance_result_type(mode, line_count, fill_count);
            }
        }
    }

    /* Isolated-pixel pass: any eligible coloured pixel with no same-colour
     * eligible neighbour floods its colour into all surrounding background pixels. */
    for (int y = 0; y < HYBRID_H; y++) {
        for (int x = 0; x < HYBRID_W; x++) {
            const unsigned char colour   = src[y * HYBRID_W + x];
            const unsigned char pix_type = src_type[y * HYBRID_W + x];
            if (colour >= 16) continue;
            if (!enhance_type_eligible(pix_type, mode)) continue;

            bool has_same_neighbour = false;
            for (int dy = -1; dy <= 1 && !has_same_neighbour; dy++) {
                for (int dx = -1; dx <= 1 && !has_same_neighbour; dx++) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if ((unsigned)nx >= (unsigned)HYBRID_W ||
                        (unsigned)ny >= (unsigned)HYBRID_H) continue;
                    if (src[ny * HYBRID_W + nx] == colour &&
                        enhance_type_eligible(src_type[ny * HYBRID_W + nx], mode)) {
                        has_same_neighbour = true;
                    }
                }
            }

            if (has_same_neighbour) continue;

            /* Isolated pixel — paint all background neighbours. */
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if ((unsigned)nx >= (unsigned)HYBRID_W ||
                        (unsigned)ny >= (unsigned)HYBRID_H) continue;
                    if (src[ny * HYBRID_W + nx] == 0xFF) {
                        const int pixel = ny * HYBRID_W + nx;
                        hybrid_pixel_buf[pixel]          = colour;
                        hybrid_cmd_type_buf[pixel] = pix_type;
                    }
                }
            }
        }
    }
}

void parse_enhanced(void) {
    build_anchors();
    connect_line_anchors();
    connect_fill_anchors();
    hybrid_render();
}