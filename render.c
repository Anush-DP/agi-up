/*
 * Created by Matthew Erickson on 22/4/2026.
 *
 */

#include "render.h"

#include "agi.h"

#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <GLFW/glfw3.h>

#define STB_EASY_FONT_IMPLEMENTATION
#include "libs/stb_easy_font.h"


/* ── Private state ────────────────────────────────────────────────────── */
static GLuint        pic_tex;
static unsigned char tex_rgb[PIC_W * PIC_H * 3];

/* ── Hover state (written by main.c, read by renderer) ───────────────── */
int  hover_cmd  = -1;
int  hover_x    = -1;
int  hover_y    = -1;
char hover_text[1024] = "";

/* ── Text rendering ───────────────────────────────────────────────────── */
static void draw_text(float x, float y, float scale, const char *text,
                      float r, float g, float b) {
    static char buf[99999];
    int quads = stb_easy_font_print(0, 0, (char *)text, NULL, buf, sizeof(buf));
    glColor3f(r, g, b);
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buf);
    glDrawArrays(GL_QUADS, 0, quads * 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
}

/* ── Texture upload ───────────────────────────────────────────────────── */
static void upload_texture(void)
{
    glBindTexture(GL_TEXTURE_2D, pic_tex);
    static bool last_vec_mode  = false;
    static int  last_tex_scale = 0;
    // TODO
    // const bool  need_realloc   = (vec_mode != last_vec_mode) ||
    //                              (!vec_mode && last_tex_scale != pic_scale);

    const int cw  = PIC_W;
    const int ch  = PIC_H;
    const int npx = cw * ch;

    for (int i = 0; i < npx; i++) {
        unsigned char c = pixel_buf[i];
        if (c >= AGI_NUM_COLORS) c = AGI_NUM_COLORS - 1;
        unsigned char r = color_palette[c][0];
        unsigned char g = color_palette[c][1];
        unsigned char b = color_palette[c][2];

        if (hover_cmd >= 0 && cmd_buf[i] == hover_cmd) {
            r = 0xFF; g = 0x80; b = 0x00;
        }

        // TODO
        // if (hover_cmd >= 0 && cmd_buf[i] == (int16_t)hover_cmd) {
        //     r = 0xFF; g = 0x80; b = 0x00;
        // }
        // if (debug_angled && angled_buf[i]) {
        //     r = 0xFF; g = 0x00; b = 0x00;
        // }
        // if (debug_angled && startend_buf[i]) {
        //     r = 0xFF; g = 0x80; b = 0xC0;
        // }
        tex_rgb[i*3+0] = r;
        tex_rgb[i*3+1] = g;
        tex_rgb[i*3+2] = b;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, PIC_W, PIC_H, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex_rgb);

    // glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch,
    //                 GL_RGB, GL_UNSIGNED_BYTE, tex_rgb);

}

/* ── Public API ───────────────────────────────────────────────────────── */
void render_init(void) {
    glGenTextures(1, &pic_tex);
    glBindTexture(GL_TEXTURE_2D, pic_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void render_hud(const int step_cmd, const int total_cmds) {
    const float TEXT_SCALE = HUD_TEXT_SCALE;
    const float TEXT_Y_TOP = (DRAW_OY - HUD_TEXT_HEIGHT * TEXT_SCALE) * 0.5f;
    const float TEXT_Y_BOT = DRAW_OY + DRAW_H + (DRAW_OY - HUD_TEXT_HEIGHT * TEXT_SCALE) * 0.5f;

    /* Step counter — top-right */
    char step[32];
    snprintf(step, sizeof(step), "cmd %d / %d", step_cmd, total_cmds);
    float text_w = stb_easy_font_width(step) * TEXT_SCALE;
    draw_text(WINDOW_WIDTH - text_w - HUD_TEXT_MARGIN, TEXT_Y_TOP, TEXT_SCALE, step, 1.0f, 1.0f, 1.0f);

    /* Pixel coordinates — top-left */
    if (hover_x >= 0) {
        char coords[32];
        snprintf(coords, sizeof(coords), "%d, %d", hover_x, hover_y);
        draw_text(HUD_TEXT_MARGIN, TEXT_Y_TOP, TEXT_SCALE, coords, 1.0f, 1.0f, 1.0f);
    }

    /* Command info — bottom strip */
    if (hover_text[0] != '\0') {
        draw_text(HUD_TEXT_MARGIN, TEXT_Y_BOT, TEXT_SCALE, hover_text, 1.0f, 1.0f, 1.0f);
    }
}

void render_pic(int step_cmd, int total_cmds) {
    upload_texture();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, pic_tex);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(DRAW_OX,          DRAW_OY         );
        glTexCoord2f(1.0f, 0.0f); glVertex2f(DRAW_OX + DRAW_W, DRAW_OY         );
        glTexCoord2f(1.0f, 1.0f); glVertex2f(DRAW_OX + DRAW_W, DRAW_OY + DRAW_H);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(DRAW_OX,          DRAW_OY + DRAW_H);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    render_hud(step_cmd, total_cmds);
}
