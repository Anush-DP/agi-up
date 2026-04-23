/*
 * Created by Matthew Erickson on 23/4/2026.
 *
 */

#pragma once

#include "agi.h"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>


/* ── Window & display geometry ────────────────────────────────────────── */
#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
#define WINDOW_TITLE  "Sierra AGI Picture Enhancer"

#define DRAW_W    (PIC_W * PIC_PX_W)
#define DRAW_H    (PIC_H * PIC_PX_H)
#define DRAW_OX   ((WINDOW_WIDTH  - DRAW_W) / 2.0f)
#define DRAW_OY   ((WINDOW_HEIGHT - DRAW_H) / 2.0f)

/* ── HUD text ─────────────────────────────────────────────────────────── */
#define HUD_TEXT_SCALE   2.5f
#define HUD_TEXT_HEIGHT  13.0f
#define HUD_TEXT_MARGIN  8.0f

/* ── Public API ───────────────────────────────────────────────────────── */
void render_init(void);
void render_pic(int step_cmd, int total_cmds);

extern int  hover_cmd;
extern int  hover_x;
extern int  hover_y;
extern char hover_text[1024];