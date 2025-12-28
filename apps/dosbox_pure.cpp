#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#else
#define BUDOSTACK_HAVE_SDL2 0
#endif
#else
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#endif

#include "../lib/libretro.h"
#include "../lib/retro_shader_bridge.h"

struct retro_core_api {
    void *handle;
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    unsigned (*retro_api_version)(void);
    void (*retro_get_system_info)(struct retro_system_info *);
    void (*retro_get_system_av_info)(struct retro_system_av_info *);
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_run)(void);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_unload_game)(void);
    void (*retro_reset)(void);
};

struct dosbox_pure_context {
    struct retro_core_api core;
    struct retro_system_info system_info;
    struct retro_system_av_info av_info;
    enum retro_pixel_format pixel_format;
    SDL_Window *window;
    SDL_GLContext gl_context;
    SDL_AudioDeviceID audio_device;
    struct retro_shader_bridge *shader_bridge;
    bool running;
    bool joypad_state[RETRO_DEVICE_ID_JOYPAD_R3 + 1];
    char content_path[PATH_MAX];
    char content_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char save_dir[PATH_MAX];
    char base_dir[PATH_MAX];
    unsigned frame_counter;
    void *game_data;
    size_t game_data_size;
};

static struct dosbox_pure_context *dosbox_ctx = NULL;

static void dosbox_log(enum retro_log_level level, const char *fmt, ...) {
    const char *prefix = "INFO";
    switch (level) {
        case RETRO_LOG_DEBUG:
            prefix = "DEBUG";
            break;
        case RETRO_LOG_INFO:
            prefix = "INFO";
            break;
        case RETRO_LOG_WARN:
            prefix = "WARN";
            break;
        case RETRO_LOG_ERROR:
            prefix = "ERROR";
            break;
        default:
            break;
    }
    fprintf(stderr, "[dosbox_pure] %s: ", prefix);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static bool dosbox_environment_cb(unsigned cmd, void *data) {
    if (!dosbox_ctx) {
        return false;
    }
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            enum retro_pixel_format *format = (enum retro_pixel_format *)data;
            if (!format) {
                return false;
            }
            if (*format == RETRO_PIXEL_FORMAT_XRGB8888 || *format == RETRO_PIXEL_FORMAT_RGB565) {
                dosbox_ctx->pixel_format = *format;
                return true;
            }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            const char **dir = (const char **)data;
            if (!dir) {
                return false;
            }
            *dir = dosbox_ctx->system_dir;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            const char **dir = (const char **)data;
            if (!dir) {
                return false;
            }
            *dir = dosbox_ctx->save_dir;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY: {
            const char **dir = (const char **)data;
            if (!dir) {
                return false;
            }
            *dir = dosbox_ctx->content_dir;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
            bool *can_dupe = (bool *)data;
            if (!can_dupe) {
                return false;
            }
            *can_dupe = false;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback *callback = (struct retro_log_callback *)data;
            if (!callback) {
                return false;
            }
            callback->log = dosbox_log;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;
        default:
            return false;
    }
}

static void dosbox_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!dosbox_ctx || !dosbox_ctx->shader_bridge || !data) {
        return;
    }
    retro_shader_bridge_set_frame(dosbox_ctx->shader_bridge, data, width, height, pitch, dosbox_ctx->pixel_format);
}

static void dosbox_audio_sample(int16_t left, int16_t right) {
    if (!dosbox_ctx || dosbox_ctx->audio_device == 0) {
        return;
    }
    int16_t samples[2] = { left, right };
    SDL_QueueAudio(dosbox_ctx->audio_device, samples, sizeof(samples));
}

static size_t dosbox_audio_sample_batch(const int16_t *data, size_t frames) {
    if (!dosbox_ctx || dosbox_ctx->audio_device == 0 || !data || frames == 0u) {
        return 0u;
    }
    size_t bytes = frames * 2u * sizeof(int16_t);
    SDL_QueueAudio(dosbox_ctx->audio_device, data, bytes);
    return frames;
}

static void dosbox_input_poll(void) {
}

static int16_t dosbox_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (!dosbox_ctx || port != 0u || index != 0u) {
        return 0;
    }
    if (device == RETRO_DEVICE_JOYPAD && id <= RETRO_DEVICE_ID_JOYPAD_R3) {
        return dosbox_ctx->joypad_state[id] ? 1 : 0;
    }
    return 0;
}

static int ensure_directory(const char *path) {
    if (!path || *path == '\0') {
        return -1;
    }
    char temp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) {
        return -1;
    }
    memcpy(temp, path, len + 1u);

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int build_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
    if (!dest || dest_size == 0u || !base || !suffix) {
        return -1;
    }
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len + suffix_len + 2u > dest_size) {
        return -1;
    }
    memcpy(dest, base, base_len);
    if (base_len > 0u && base[base_len - 1u] != '/' && suffix[0] != '/') {
        dest[base_len] = '/';
        memcpy(dest + base_len + 1u, suffix, suffix_len + 1u);
    } else if (base_len > 0u && base[base_len - 1u] == '/' && suffix[0] == '/') {
        memcpy(dest + base_len, suffix + 1u, suffix_len);
        dest[base_len + suffix_len - 1u] = '\0';
    } else {
        memcpy(dest + base_len, suffix, suffix_len + 1u);
    }
    return 0;
}

static int resolve_path(const char *path, char *out_path, size_t out_size) {
    if (!path || !out_path || out_size == 0u) {
        return -1;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        return -1;
    }
    size_t len = strlen(resolved);
    if (len + 1u > out_size) {
        return -1;
    }
    memcpy(out_path, resolved, len + 1u);
    return 0;
}

static int resolve_content_dir(const char *content_path, char *out_dir, size_t out_size) {
    if (!content_path || !out_dir || out_size == 0u) {
        return -1;
    }
    struct stat st;
    if (stat(content_path, &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        size_t len = strlen(content_path);
        if (len + 1u > out_size) {
            return -1;
        }
        memcpy(out_dir, content_path, len + 1u);
        return 0;
    }
    char temp[PATH_MAX];
    size_t len = strlen(content_path);
    if (len + 1u > sizeof(temp)) {
        return -1;
    }
    memcpy(temp, content_path, len + 1u);
    char *slash = strrchr(temp, '/');
    if (!slash) {
        return -1;
    }
    if (slash == temp) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    len = strlen(temp);
    if (len + 1u > out_size) {
        return -1;
    }
    memcpy(out_dir, temp, len + 1u);
    return 0;
}

static int load_game_data(const char *path, void **out_data, size_t *out_size) {
    if (!path || !out_data || !out_size) {
        return -1;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long file_len = ftell(fp);
    if (file_len < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    size_t size = (size_t)file_len;
    void *data = malloc(size);
    if (!data) {
        fclose(fp);
        return -1;
    }
    size_t read_len = fread(data, 1, size, fp);
    fclose(fp);
    if (read_len != size) {
        free(data);
        return -1;
    }
    *out_data = data;
    *out_size = size;
    return 0;
}

static int load_core_symbol(void *handle, const char *name, void **symbol) {
    dlerror();
    *symbol = dlsym(handle, name);
    const char *error = dlerror();
    if (error) {
        fprintf(stderr, "Failed to resolve %s: %s\n", name, error);
        return -1;
    }
    return 0;
}

static int load_core(const char *path, struct retro_core_api *core) {
    if (!path || !core) {
        return -1;
    }
    memset(core, 0, sizeof(*core));
    core->handle = dlopen(path, RTLD_NOW);
    if (!core->handle) {
        fprintf(stderr, "Failed to load core '%s': %s\n", path, dlerror());
        return -1;
    }
    if (load_core_symbol(core->handle, "retro_init", (void **)&core->retro_init) != 0 ||
        load_core_symbol(core->handle, "retro_deinit", (void **)&core->retro_deinit) != 0 ||
        load_core_symbol(core->handle, "retro_api_version", (void **)&core->retro_api_version) != 0 ||
        load_core_symbol(core->handle, "retro_get_system_info", (void **)&core->retro_get_system_info) != 0 ||
        load_core_symbol(core->handle, "retro_get_system_av_info", (void **)&core->retro_get_system_av_info) != 0 ||
        load_core_symbol(core->handle, "retro_set_environment", (void **)&core->retro_set_environment) != 0 ||
        load_core_symbol(core->handle, "retro_set_video_refresh", (void **)&core->retro_set_video_refresh) != 0 ||
        load_core_symbol(core->handle, "retro_set_audio_sample", (void **)&core->retro_set_audio_sample) != 0 ||
        load_core_symbol(core->handle, "retro_set_audio_sample_batch", (void **)&core->retro_set_audio_sample_batch) != 0 ||
        load_core_symbol(core->handle, "retro_set_input_poll", (void **)&core->retro_set_input_poll) != 0 ||
        load_core_symbol(core->handle, "retro_set_input_state", (void **)&core->retro_set_input_state) != 0 ||
        load_core_symbol(core->handle, "retro_run", (void **)&core->retro_run) != 0 ||
        load_core_symbol(core->handle, "retro_load_game", (void **)&core->retro_load_game) != 0 ||
        load_core_symbol(core->handle, "retro_unload_game", (void **)&core->retro_unload_game) != 0 ||
        load_core_symbol(core->handle, "retro_reset", (void **)&core->retro_reset) != 0) {
        dlclose(core->handle);
        memset(core, 0, sizeof(*core));
        return -1;
    }
    return 0;
}

static void unload_core(struct retro_core_api *core) {
    if (!core) {
        return;
    }
    if (core->handle) {
        dlclose(core->handle);
    }
    memset(core, 0, sizeof(*core));
}

static void update_key(SDL_Keycode key, bool pressed) {
    if (!dosbox_ctx) {
        return;
    }
    switch (key) {
        case SDLK_UP:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_UP] = pressed;
            break;
        case SDLK_DOWN:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_DOWN] = pressed;
            break;
        case SDLK_LEFT:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_LEFT] = pressed;
            break;
        case SDLK_RIGHT:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_RIGHT] = pressed;
            break;
        case SDLK_z:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_B] = pressed;
            break;
        case SDLK_x:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_A] = pressed;
            break;
        case SDLK_a:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_Y] = pressed;
            break;
        case SDLK_s:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_X] = pressed;
            break;
        case SDLK_q:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_L] = pressed;
            break;
        case SDLK_w:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_R] = pressed;
            break;
        case SDLK_RETURN:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_START] = pressed;
            break;
        case SDLK_RSHIFT:
            dosbox_ctx->joypad_state[RETRO_DEVICE_ID_JOYPAD_SELECT] = pressed;
            break;
        default:
            break;
    }
}

static void usage(const char *name) {
    fprintf(stderr, "Usage: %s --core <core_path> --content <dos_game_dir> [options]\n", name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s, --shader <path>   Add a GLSL shader from ./shaders (repeatable).\n");
    fprintf(stderr, "  --no-shader           Disable CRT shader stack.\n");
}

int main(int argc, char **argv) {
#if !BUDOSTACK_HAVE_SDL2
    (void)argc;
    (void)argv;
    fprintf(stderr, "SDL2 is required to run dosbox_pure.\n");
    return 1;
#else
    const char *core_path = NULL;
    const char *content_arg = NULL;
    const char *shader_args[8];
    size_t shader_count = 0u;
    bool shaders_enabled = true;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--core") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing core path after --core.\n");
                return 1;
            }
            core_path = argv[++i];
        } else if (strcmp(arg, "--content") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing content path after --content.\n");
                return 1;
            }
            content_arg = argv[++i];
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--shader") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing shader path after %s.\n", arg);
                return 1;
            }
            if (shader_count < sizeof(shader_args) / sizeof(shader_args[0])) {
                shader_args[shader_count++] = argv[++i];
            } else {
                fprintf(stderr, "Too many shaders specified (max %zu).\n",
                        sizeof(shader_args) / sizeof(shader_args[0]));
                return 1;
            }
        } else if (strcmp(arg, "--no-shader") == 0) {
            shaders_enabled = false;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg);
            usage(argv[0]);
            return 1;
        }
    }

    if (!core_path || !content_arg) {
        usage(argv[0]);
        return 1;
    }

    struct dosbox_pure_context context;
    memset(&context, 0, sizeof(context));
    context.pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
    context.running = true;
    dosbox_ctx = &context;

    if (resolve_path(content_arg, context.content_path, sizeof(context.content_path)) != 0) {
        fprintf(stderr, "Failed to resolve content path: %s\n", content_arg);
        return 1;
    }
    if (resolve_content_dir(context.content_path, context.content_dir, sizeof(context.content_dir)) != 0) {
        fprintf(stderr, "Failed to resolve content directory for %s\n", context.content_path);
        return 1;
    }

    char root_dir[PATH_MAX];
    if (!getcwd(root_dir, sizeof(root_dir))) {
        fprintf(stderr, "Failed to resolve working directory.\n");
        return 1;
    }

    const char *user = getenv("USER");
    if (!user || *user == '\0') {
        user = "default";
    }
    if (build_path(context.base_dir, sizeof(context.base_dir), root_dir, "users") != 0 ||
        build_path(context.base_dir, sizeof(context.base_dir), context.base_dir, user) != 0 ||
        build_path(context.base_dir, sizeof(context.base_dir), context.base_dir, "dosbox_pure") != 0) {
        fprintf(stderr, "Failed to build user base path.\n");
        return 1;
    }
    if (build_path(context.system_dir, sizeof(context.system_dir), context.base_dir, "system") != 0 ||
        build_path(context.save_dir, sizeof(context.save_dir), context.base_dir, "save") != 0) {
        fprintf(stderr, "Failed to build system/save paths.\n");
        return 1;
    }
    if (ensure_directory(context.system_dir) != 0 || ensure_directory(context.save_dir) != 0) {
        fprintf(stderr, "Failed to create system/save directories under %s\n", context.base_dir);
        return 1;
    }

    if (load_core(core_path, &context.core) != 0) {
        return 1;
    }

    context.core.retro_set_environment(dosbox_environment_cb);
    context.core.retro_set_video_refresh(dosbox_video_refresh);
    context.core.retro_set_audio_sample(dosbox_audio_sample);
    context.core.retro_set_audio_sample_batch(dosbox_audio_sample_batch);
    context.core.retro_set_input_poll(dosbox_input_poll);
    context.core.retro_set_input_state(dosbox_input_state);

    context.core.retro_init();

    if (context.core.retro_api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "Core API version mismatch (expected %u).\n", RETRO_API_VERSION);
    }

    context.core.retro_get_system_info(&context.system_info);
    context.core.retro_get_system_av_info(&context.av_info);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        context.core.retro_deinit();
        unload_core(&context.core);
        return 1;
    }

    int base_width = (int)context.av_info.geometry.base_width;
    int base_height = (int)context.av_info.geometry.base_height;
    if (base_width <= 0) {
        base_width = 640;
    }
    if (base_height <= 0) {
        base_height = 480;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#ifdef SDL_GL_CONTEXT_PROFILE_MASK
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    context.window = SDL_CreateWindow("BUDOSTACK dosbox-pure",
                                      SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED,
                                      base_width * 2,
                                      base_height * 2,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!context.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        context.core.retro_deinit();
        unload_core(&context.core);
        return 1;
    }

    context.gl_context = SDL_GL_CreateContext(context.window);
    if (!context.gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(context.window);
        SDL_Quit();
        context.core.retro_deinit();
        unload_core(&context.core);
        return 1;
    }

    if (SDL_GL_MakeCurrent(context.window, context.gl_context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(context.gl_context);
        SDL_DestroyWindow(context.window);
        SDL_Quit();
        context.core.retro_deinit();
        unload_core(&context.core);
        return 1;
    }

    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError());
    }

    if (shaders_enabled && shader_count == 0u) {
        shader_args[shader_count++] = "shaders/crtscreen.glsl";
    }

    context.shader_bridge = retro_shader_bridge_create(context.window,
                                                       root_dir,
                                                       shaders_enabled ? shader_args : NULL,
                                                       shaders_enabled ? shader_count : 0u);
    if (!context.shader_bridge) {
        fprintf(stderr, "Failed to initialize shader bridge.\n");
        SDL_GL_DeleteContext(context.gl_context);
        SDL_DestroyWindow(context.window);
        SDL_Quit();
        context.core.retro_deinit();
        unload_core(&context.core);
        return 1;
    }

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_zero(desired);
    desired.freq = (int)(context.av_info.timing.sample_rate > 0.0 ? context.av_info.timing.sample_rate : 44100.0);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    context.audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (context.audio_device == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(context.audio_device, 0);
    }

    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.path = context.content_path;
    game_info.data = NULL;
    game_info.size = 0u;
    game_info.meta = NULL;

    if (!context.system_info.need_fullpath) {
        struct stat st;
        if (stat(context.content_path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (load_game_data(context.content_path, &context.game_data, &context.game_data_size) == 0) {
                game_info.data = context.game_data;
                game_info.size = context.game_data_size;
            }
        }
    }

    if (!context.core.retro_load_game(&game_info)) {
        fprintf(stderr, "Failed to load game: %s\n", context.content_path);
        context.core.retro_deinit();
        unload_core(&context.core);
        if (context.audio_device != 0) {
            SDL_CloseAudioDevice(context.audio_device);
        }
        retro_shader_bridge_destroy(context.shader_bridge);
        SDL_GL_DeleteContext(context.gl_context);
        SDL_DestroyWindow(context.window);
        SDL_Quit();
        free(context.game_data);
        return 1;
    }

    while (context.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    context.running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        context.running = false;
                    }
                    update_key(event.key.keysym.sym, true);
                    break;
                case SDL_KEYUP:
                    update_key(event.key.keysym.sym, false);
                    break;
                default:
                    break;
            }
        }

        context.core.retro_run();
        if (retro_shader_bridge_render(context.shader_bridge, context.frame_counter++) != 0) {
            fprintf(stderr, "Render error; exiting.\n");
            context.running = false;
        }
    }

    context.core.retro_unload_game();
    context.core.retro_deinit();
    unload_core(&context.core);

    if (context.audio_device != 0) {
        SDL_CloseAudioDevice(context.audio_device);
    }
    retro_shader_bridge_destroy(context.shader_bridge);
    SDL_GL_DeleteContext(context.gl_context);
    SDL_DestroyWindow(context.window);
    SDL_Quit();
    free(context.game_data);
    return 0;
#endif
}
