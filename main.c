/*
 * AGI image enhancer
 *
 * Created by Matthew Erickson on 20/4/2026.
 *
 *
 * TODO: usage instructions
 */
#include "agi.h"
#include "render.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include "hybrid.h"

/* ── Window & display geometry ────────────────────────────────────────── */
#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
#define WINDOW_TITLE  "Sierra AGI Picture Enhancer"

/* ── Step-through state ───────────────────────────────────────────────── */
static int total_cmds = 0;
static int step_cmd = 0;
static int step_dir = 0;
static double key_press_time = 0.0;
static double last_step_time = 0.0;

#define STEP_INITIAL_DELAY  0.4
#define STEP_REPEAT_SEC     (1.0 / 60.0)


void parse_args(const int argc, char **argv, int *monitor_index, const char **pic_path, const int monitor_count) {
    *monitor_index = 0;
    *pic_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'm') {
            *monitor_index = atoi(argv[i] + 2) - 1;
        } else {
            *pic_path = argv[i];
        }
    }

    if (!*pic_path) {
        fprintf(stderr, "Usage: %s [-mN] <pic-file>\n", argv[0]);
        fprintf(stderr, "  -mN   open on monitor N (default: 1)\n");
        exit(EXIT_FAILURE);
    }

    if (*monitor_index < 0 || *monitor_index >= monitor_count) {
        fprintf(stderr, "Monitor %d not found (%d available), using primary\n",
                *monitor_index + 1, monitor_count);
        *monitor_index = 0;
    }
}

void parse_next(void) {
    const int next = step_cmd + step_dir;

    if (next >= 0 && next <= total_cmds) {
        step_cmd = next;
        parse_to_step(step_cmd);
    }
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
    (void) window;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
}

static void key_callback(GLFWwindow *window, const int key, const int scan_code, const int action, const int mods) {
    (void) scan_code;
    (void) mods;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (key == GLFW_KEY_H && action == GLFW_PRESS) {
        pic_scale = pic_scale == 1 ? PIC_MAX_SCALE : 1;
        parse_to_step(step_cmd);
        clear_hover();
    }

    if (key == GLFW_KEY_E && action == GLFW_PRESS) {
        if (!enhance_mode) {
            enhance_mode = true;
            pic_enhance();
        } else {
            enhance_mode = false;
        }

        clear_hover();
    }

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT) {
            step_dir = (key == GLFW_KEY_RIGHT) ? 1 : -1;
            key_press_time = glfwGetTime();
            last_step_time = key_press_time;

            parse_next(); // Update once on key down. Repeat handled in main()
        }
    } else if (action == GLFW_RELEASE) {
        if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT)
            step_dir = 0;
    }
}

/* ── Cursor callback ──────────────────────────────────────────────────── */
static const char *color_name(int c) {
    static const char *names[16] = {
        "black", "blue", "green", "cyan", "red", "magenta", "brown", "lt grey",
        "dk grey", "lt blue", "lt green", "lt cyan", "lt red", "lt magenta", "yellow", "white"
    };
    return (c >= 0 && c < 16) ? names[c] : "?";
}

static const char *opcode_name(unsigned char op) {
    switch (op) {
        case 0xF0: return "set color";
        case 0xF1: return "disable draw";
        case 0xF2: return "set pri color";
        case 0xF3: return "disable pri";
        case 0xF4: return "Y corner";
        case 0xF5: return "X corner";
        case 0xF6: return "abs line";
        case 0xF7: return "rel line";
        case 0xF8: return "fill";
        case 0xF9: return "set pen";
        case 0xFA: return "pen plot";
        default: return "?";
    }
}

static void emit(char *out, const int outsz, int *pos, const char *fmt, ...) {
    if (*pos < outsz - 1) {
        va_list args;
        va_start(args, fmt);
        *pos += vsnprintf(out + *pos, (size_t)(outsz - *pos), fmt, args);
        va_end(args);
    }
}

static void format_cmd_args(char *out, const int outsz, const int cmd_id) {
    PicCmd *cmd = &cmd_log[cmd_id];
    const unsigned char *a = raw_pic_data + cmd->arg_offset;
    const int len = cmd->arg_len;
    int pos = 0;

    switch (cmd->opcode) {
        case 0xF0:
        case 0xF2:
            if (len >= 1) { emit(out, outsz, &pos, "color=%d (%s)", a[0], color_name(a[0])); }
            break;

        case 0xF1:
        case 0xF3:
            break;

        case 0xF4:
        case 0xF5:
            if (len >= 2) {
                emit(out, outsz, &pos, "start=(%d,%d)", a[0], a[1]);
                int toggle = 0;
                for (int k = 2; k < len; k++, toggle ^= 1) {
                    const int is_x = (cmd->opcode == 0xF4) ? toggle : !toggle;
                    emit(out, outsz, &pos, "  %s=%d", is_x ? "x" : "y", a[k]);
                }
            }
            break;

        case 0xF6:
            for (int k = 0; k + 1 < len; k += 2) {
                if (k > 0) { emit(out, outsz, &pos, " ->"); }
                emit(out, outsz, &pos, " (%d,%d)", a[k], a[k + 1]);
            }
            break;

        case 0xF7:
            if (len >= 2) {
                emit(out, outsz, &pos, "start=(%d,%d)", a[0], a[1]);
                for (int k = 2; k < len; k++) {
                    int dx = (a[k] >> 4) & 7;
                    if (a[k] & DISP_X_SIGN) { dx = -dx; }
                    int dy = a[k] & 7;
                    if (a[k] & DISP_Y_SIGN) { dy = -dy; }
                    emit(out, outsz, &pos, "  (%+d,%+d)", dx, dy);
                }
            }
            break;

        case 0xF8:
            for (int k = 0; k + 1 < len; k += 2) {
                emit(out, outsz, &pos, "%s(%d,%d)", k ? "  " : "", a[k], a[k + 1]);
            }
            break;

        case 0xF9:
            if (len >= 1) {
                emit(out, outsz, &pos, "size=%d  %s  %s",
                     a[0] & PEN_FLAG_SIZE,
                     (a[0] & PEN_FLAG_RECT) ? "rect" : "circle",
                     (a[0] & PEN_FLAG_SPLATTER) ? "splatter" : "solid");
            }
            break;

        case 0xFA: {
            int splatter = 0;
            for (int k = cmd_id - 1; k >= 0; k--) {
                if (cmd_log[k].opcode == 0xF9 && cmd_log[k].arg_len >= 1) {
                    splatter = (raw_pic_data[cmd_log[k].arg_offset] & PEN_FLAG_SPLATTER) != 0;
                    break;
                }
            }
            const int step = splatter ? 3 : 2;
            for (int k = 0; k + 1 < len; k += step) {
                if (splatter) {
                    emit(out, outsz, &pos, "%stex=%d (%d,%d)", k ? "  " : "", a[k], a[k + 1], a[k + 2]);
                } else { emit(out, outsz, &pos, "%s(%d,%d)", k ? "  " : "", a[k], a[k + 1]); }
            }
            break;
        }

        default: {
            for (int k = 0; k < len; k++) { emit(out, outsz, &pos, " %02X", a[k]); }
            break;
        }
    }
}

void cursor_pos_callback(GLFWwindow *window, double x_pos, double y_pos) {
    (void) window;

    const int prev_hover_cmd = hover_cmd;
    const int cw = PIC_W * pic_scale, ch = PIC_H * pic_scale;
    const int ax = (int) ((x_pos - DRAW_OX) / (PIC_PX_W / (double) pic_scale));
    const int ay = (int) ((y_pos - DRAW_OY) / (PIC_PX_H / (double) pic_scale));

    if (ax >= 0 && ax < cw && ay >= 0 && ay < ch) {
        hover_cmd = cmd_buf[ay * cw + ax];
        hover_x = ax / pic_scale;
        hover_y = ay / pic_scale;
    } else {
        hover_cmd = -1;
        hover_x = -1;
        hover_y = -1;
    }

    if (hover_cmd == prev_hover_cmd) return;

    if (hover_cmd < 0 || !raw_pic_data) {
        hover_text[0] = '\0';
        return;
    }

    const PicCmd *cmd = &cmd_log[hover_cmd];
    char args[512];
    args[0] = '\0';
    format_cmd_args(args, sizeof(args), hover_cmd);

    char nbr_str[128];


    snprintf(hover_text, sizeof(hover_text),
             "cmd %d  F%X (%s)  @%d    %s    %s",
             cmd->id, cmd->opcode & 0x0F, opcode_name(cmd->opcode),
             cmd->file_offset, args, nbr_str);
}

void initialize_glfw_context(GLFWwindow *window) {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
}

void initialize_gl_context(void) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, WINDOW_WIDTH, WINDOW_HEIGHT, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

long load_image_data_from_file(const char *pic_path) {
    FILE *file = fopen(pic_path, "rb");

    if (!file) {
        fprintf(stderr, "Cannot open %s\n", pic_path);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    raw_pic_data = malloc((size_t) size);

    if (raw_pic_data == NULL) {
        fclose(file);
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    fread(raw_pic_data, 1, (size_t) size, file);
    raw_pic_len = (int) size;
    fclose(file);
    return size;
}

int main(const int argc, char **argv) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    int monitor_count;
    GLFWmonitor **monitors = glfwGetMonitors(&monitor_count);

    int monitor_index;
    const char *pic_path;

    parse_args(argc, argv, &monitor_index, &pic_path, monitor_count);

    GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                          WINDOW_TITLE,
                                          monitors[monitor_index], nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    initialize_glfw_context(window);
    initialize_gl_context();
    render_init();

    load_image_data_from_file(pic_path);

    first_parse();
    total_cmds = cmd_count;
    step_cmd = 0;

    while (!glfwWindowShouldClose(window)) {
        if (step_dir != 0) {
            const double now = glfwGetTime();
            if (now - key_press_time >= STEP_INITIAL_DELAY &&
                now - last_step_time >= STEP_REPEAT_SEC) {
                parse_next();
                last_step_time = now;
            }
        }

        render_pic(step_cmd, total_cmds);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}
