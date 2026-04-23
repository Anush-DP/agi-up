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

#include <stdio.h>
#include <stdlib.h>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

/* ── Window & display geometry ────────────────────────────────────────── */
#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
#define WINDOW_TITLE  "Sierra AGI Picture Enhancer"

/* ── Step-through state ───────────────────────────────────────────────── */
static int    total_cmds     = 0;
static int    step_cmd       = 0;
static int    step_dir       = 0;
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

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void)window;
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

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT) {
            step_dir       = (key == GLFW_KEY_RIGHT) ? 1 : -1;
            key_press_time = glfwGetTime();
            last_step_time = key_press_time;

            parse_next(); // Update once on key down. Repeat handled in main()
        }
    } else if (action == GLFW_RELEASE) {
        if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT)
            step_dir = 0;
    }
}

void initialize_glfw_context(GLFWwindow *window) {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    //glfwSetCursorPosCallback(window, cursor_pos_callback); // TODO
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
    raw_pic_data = malloc((size_t)size);

    if (raw_pic_data == NULL) {
        fclose(file);
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    fread(raw_pic_data, 1, (size_t)size, file);
    raw_pic_len    = (int)size;
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
                                          monitors[monitor_index], NULL);
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
    step_cmd   = 0;

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
