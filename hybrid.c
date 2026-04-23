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

            // Connect to all same command neighbours
            for (int dir = 0; dir < 8; dir++) {
                const int nx = x + dir_dx[dir];
                const int ny = y + dir_dy[dir];
                if ((unsigned)nx >= (unsigned)PIC_W ||
                    (unsigned)ny >= (unsigned)PIC_H) continue;
                if (ref_cmd_buf[ny * PIC_W + nx] == cmd_id) {
                    set_connect(anchor, dir);
                    same_cmd_count++;
                }
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

                    //if (cmd_n == cmd_id) continue; // TODO is this what we want?

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
                 * Fallback: connect in travel direction (opposite the single
                 * same-command neighbour behind us).
                 * Off-canvas: always extend to the canvas boundary.
                 * In-bounds:  only connect when no forward same-colour neighbour
                 *             was found AND the destination is a line cell.
                 */
                if (same_cmd_count == 1) {
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
                        const bool fwd_oob = (unsigned)fx >= (unsigned)PIC_W ||
                                             (unsigned)fy >= (unsigned)PIC_H;
                        if (fwd_oob) {
                            set_connect(anchor, fwd);
                        } else if (!has_forward_connect && is_line_cmd(ref_cmd_buf[fy * PIC_W + fx])) {
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
            const int       cmd_id = ref_cmd_buf[idx];
            const unsigned char pix_type = (cmd_id < 0)                        ? 0
                                         : (cmd_log[cmd_id].opcode == 0xF8)    ? 2
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
                const bool a_is_line = (unsigned)ax < (unsigned)PIC_W &&
                                       (unsigned)ay < (unsigned)PIC_H &&
                                       is_line_cmd(ref_cmd_buf[ay * PIC_W + ax]);
                const bool b_is_line = (unsigned)bx < (unsigned)PIC_W &&
                                       (unsigned)by < (unsigned)PIC_H &&
                                       is_line_cmd(ref_cmd_buf[by * PIC_W + bx]);
                if (a_is_line || b_is_line) continue;

                set_connect(anchor, diag);
            }
        }
    }
}

void pic_enhance(void) {
    build_anchors();
    connect_line_anchors();
    connect_fill_anchors();
    hybrid_render();
}