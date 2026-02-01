#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
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

#if BUDOSTACK_HAVE_SDL2
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#define DR_MP3_IMPLEMENTATION
#include "../lib/dr_mp3.h"
#define STB_VORBIS_IMPLEMENTATION
#include "../lib/stb_vorbis.h"
#include "../lib/stb_image.h"
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if BUDOSTACK_HAVE_SDL2

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04
#define PSF1_MODE512 0x01
#define PSF2_MAGIC 0x864ab572u
#define PSF2_HEADER_SIZE 32u

#define TERMINAL_COLUMNS 118u
#define TERMINAL_ROWS 66u
#define TERMINAL_HISTORY_LIMIT 10000u
#ifndef TERMINAL_FONT_SCALE
#define TERMINAL_FONT_SCALE 1
#endif
#define TERMINAL_CURSOR_BLINK_INTERVAL 500u
#ifndef TERMINAL_TARGET_FPS
#define TERMINAL_TARGET_FPS 30u
#endif
#ifndef TERMINAL_SHADER_TARGET_FPS
#define TERMINAL_SHADER_TARGET_FPS 30u
#endif

#define TERMINAL_CURSOR_SPRITE_PATH "./tasks/assets/cursor.png"

_Static_assert(TERMINAL_FONT_SCALE > 0, "TERMINAL_FONT_SCALE must be positive");
_Static_assert(TERMINAL_COLUMNS > 0u, "TERMINAL_COLUMNS must be positive");
_Static_assert(TERMINAL_ROWS > 0u, "TERMINAL_ROWS must be positive");
_Static_assert(TERMINAL_TARGET_FPS > 0u, "TERMINAL_TARGET_FPS must be positive");

static SDL_Window *terminal_window_handle = NULL;
static SDL_GLContext terminal_gl_context_handle = NULL;
static int terminal_master_fd_handle = -1;
static int terminal_cell_pixel_width = 0;
static int terminal_cell_pixel_height = 0;
static int terminal_logical_width = 0;
static int terminal_logical_height = 0;
static int terminal_scale_factor = 1;
static int terminal_resolution_override_active = 0;
static int terminal_resolution_width = 0;
static int terminal_resolution_height = 0;
static int terminal_margin_pixels = 0;
static size_t terminal_selection_anchor_row = 0u;
static size_t terminal_selection_anchor_col = 0u;
static size_t terminal_selection_caret_row = 0u;
static size_t terminal_selection_caret_col = 0u;
static int terminal_selection_active = 0;
static int terminal_selection_dragging = 0;
static Uint32 terminal_shader_last_frame_tick = 0u;
static Uint32 terminal_shader_frame_interval_ms = 0u;
static Uint32 terminal_render_last_frame_tick = 0u;
static Uint32 terminal_render_frame_interval_ms = 0u;
static int terminal_shaders_enabled = 1;
static int terminal_vsync_enabled = 0;
static int terminal_input_draw_requested = 0;

static GLuint terminal_gl_texture = 0;
static int terminal_texture_width = 0;
static int terminal_texture_height = 0;
static int terminal_gl_ready = 0;
static GLuint terminal_bound_texture = 0;
static int terminal_history_width = 0;
static int terminal_history_height = 0;
static GLuint terminal_cursor_texture = 0;
static int terminal_cursor_width = 0;
static int terminal_cursor_height = 0;
static int terminal_cursor_hot_x = 0;
static int terminal_cursor_hot_y = 0;
static int terminal_cursor_enabled = 0;
static int terminal_cursor_blink_enabled = 1;
static int terminal_cursor_blink_reset_requested = 0;
static int terminal_cursor_position_valid = 0;
static int terminal_cursor_x = 0;
static int terminal_cursor_y = 0;
static int terminal_cursor_dirty = 0;
static int terminal_mouse_x = 0;
static int terminal_mouse_y = 0;
static unsigned int terminal_mouse_left_clicks = 0u;
static unsigned int terminal_mouse_right_clicks = 0u;
static int terminal_using_alternate = 0;
static int terminal_alternate_initialized = 0;
static struct terminal_buffer *terminal_alternate_buffer_handle = NULL;

struct terminal_quad_vertex {
    GLfloat position[4];
    GLfloat texcoord_cpu[2];
    GLfloat texcoord_fbo[2];
};

static const struct terminal_quad_vertex terminal_quad_vertices[4] = {
    { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f } },
    { {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } }
};

static GLuint terminal_quad_vbo = 0;
static const GLsizei terminal_quad_vertex_count = 4;

static const GLfloat terminal_identity_mvp[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

static uint8_t *terminal_framebuffer_pixels = NULL;
static size_t terminal_framebuffer_capacity = 0u;
static int terminal_framebuffer_width = 0;
static int terminal_framebuffer_height = 0;
static GLuint terminal_gl_framebuffer = 0;
static GLuint terminal_gl_intermediate_textures[2] = {0u, 0u};
static int terminal_intermediate_width = 0;
static int terminal_intermediate_height = 0;

struct terminal_render_cache_entry {
    uint32_t ch;
    uint32_t fg;
    uint32_t bg;
    uint8_t style;
    uint8_t cursor;
    uint8_t selected;
    uint8_t pad;
};

static struct terminal_render_cache_entry *terminal_render_cache = NULL;
static size_t terminal_render_cache_columns = 0u;
static size_t terminal_render_cache_rows = 0u;
static size_t terminal_render_cache_count = 0u;
static int terminal_force_full_redraw = 1;
static int terminal_background_dirty = 1;

struct terminal_custom_pixel {
    int x;
    int y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t layer;
    uint64_t version;
};

static struct terminal_custom_pixel *terminal_custom_pixels = NULL;
static size_t terminal_custom_pixel_count = 0u;
static size_t terminal_custom_pixel_capacity = 0u;
static int terminal_custom_pixels_dirty = 0;
static uint16_t terminal_custom_pixels_pending_layers = 0u;
static int terminal_custom_pixels_active = 0;
static uint64_t terminal_custom_layer_versions[17] = {0};

struct terminal_custom_clear_request {
    int x;
    int y;
    int w;
    int h;
    uint8_t layer;
    uint64_t version;
};

static struct terminal_custom_clear_request *terminal_custom_pending_clears = NULL;
static size_t terminal_custom_pending_clear_count = 0u;
static size_t terminal_custom_pending_clear_capacity = 0u;

struct terminal_gl_shader {
    GLuint program;
    GLint attrib_vertex;
    GLint attrib_color;
    GLint attrib_texcoord;
    GLint uniform_mvp;
    GLint uniform_frame_direction;
    GLint uniform_frame_count;
    GLint uniform_output_size;
    GLint uniform_texture_size;
    GLint uniform_input_size;
    GLint uniform_texture_sampler;
    GLint uniform_prev_sampler;
    GLint uniform_crt_gamma;
    GLint uniform_monitor_gamma;
    GLint uniform_distance;
    GLint uniform_curvature;
    GLint uniform_radius;
    GLint uniform_corner_size;
    GLint uniform_corner_smooth;
    GLint uniform_x_tilt;
    GLint uniform_y_tilt;
    GLint uniform_overscan_x;
    GLint uniform_overscan_y;
    GLint uniform_dotmask;
    GLint uniform_sharper;
    GLint uniform_scanline_weight;
    GLint uniform_luminance;
    GLint uniform_interlace_detect;
    GLint uniform_saturation;
    GLint uniform_inv_gamma;
    GLuint history_texture;
    GLuint history_texture_flipped;
    GLuint quad_vaos[2];
    int has_cached_mvp;
    GLfloat cached_mvp[16];
    int has_cached_output_size;
    GLfloat cached_output_size[2];
    int has_cached_texture_size;
    GLfloat cached_texture_size[2];
    int has_cached_input_size;
    GLfloat cached_input_size[2];
};

static struct terminal_gl_shader *terminal_gl_shaders = NULL;
static size_t terminal_gl_shader_count = 0u;
struct shader_path_entry {
    char path[PATH_MAX];
};
static struct shader_path_entry *terminal_requested_shaders = NULL;
static size_t terminal_requested_shader_count = 0u;

struct psf_unicode_map;

struct psf_font {
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t glyph_size;
    uint8_t *glyphs;
    struct psf_unicode_map *unicode_map;
    size_t unicode_map_count;
};

struct psf_unicode_map {
    uint32_t codepoint;
    uint32_t glyph_index;
};

static struct psf_font terminal_font = {0};

#if BUDOSTACK_HAVE_SDL2
#define TERMINAL_SOUND_CHANNEL_COUNT 32

struct terminal_sound_channel {
    float *samples;
    size_t frame_count;
    size_t position;
    int active;
    float volume;
};

static SDL_AudioDeviceID terminal_audio_device = 0;
static SDL_AudioSpec terminal_audio_spec = {0};
static SDL_mutex *terminal_audio_mutex = NULL;
static struct terminal_sound_channel terminal_sound_channels[TERMINAL_SOUND_CHANNEL_COUNT];

static void SDLCALL terminal_audio_callback(void *userdata, Uint8 *stream, int len);
static void terminal_audio_channel_clear(struct terminal_sound_channel *channel);
static int terminal_initialize_audio(void);
static void terminal_shutdown_audio(void);
static int terminal_audio_convert(const SDL_AudioSpec *source_spec, const void *data, size_t length, float **out_samples, size_t *out_frames);
static int terminal_audio_load_file(const char *path, float **out_samples, size_t *out_frames);
static int terminal_sound_play(int channel_index, const char *path, float volume);
static void terminal_sound_stop(int channel_index);
#endif

struct terminal_buffer;
struct ansi_parser;

static ssize_t safe_write(int fd, const void *buf, size_t count);
static int terminal_send_bytes(int fd, const void *data, size_t length);
static int terminal_send_string(int fd, const char *str);
static int terminal_mouse_reporting_enabled(const struct terminal_buffer *buffer);
static int terminal_send_mouse_report(const struct terminal_buffer *buffer,
                                      int button_code,
                                      int released,
                                      int motion,
                                      size_t column,
                                      size_t row,
                                      SDL_Keymod mod);
static size_t terminal_total_rows(const struct terminal_buffer *buffer);
static const struct terminal_cell *terminal_buffer_row_at(const struct terminal_buffer *buffer, size_t index);
static size_t terminal_clamped_scroll_offset(const struct terminal_buffer *buffer);
static void terminal_visible_row_range(const struct terminal_buffer *buffer, size_t *out_top_index, size_t *out_bottom_index);
static void terminal_buffer_fill_line_current(struct terminal_buffer *buffer, size_t row);
static int terminal_window_point_to_framebuffer(int window_x, int window_y, int *out_x, int *out_y);
static int terminal_screen_point_to_cell(int x,
                                         int y,
                                         size_t columns,
                                         size_t rows,
                                         size_t top_index,
                                         size_t total_rows,
                                         size_t *out_global_row,
                                         size_t *out_column,
                                         int clamp_to_bounds);
static void terminal_selection_begin(size_t global_row, size_t column);
static void terminal_selection_update(size_t global_row, size_t column);
static void terminal_selection_clear(void);
static void terminal_selection_validate(const struct terminal_buffer *buffer);
static int terminal_selection_linear_range(const struct terminal_buffer *buffer, size_t *out_start, size_t *out_end);
static int terminal_selection_contains_cell(size_t global_row,
                                            size_t column,
                                            size_t selection_start,
                                            size_t selection_end,
                                            size_t columns);
static int terminal_copy_selection_to_clipboard(const struct terminal_buffer *buffer);
// FIX: for edit.c paste to work
static int terminal_paste_from_clipboard(struct terminal_buffer *buffer, int fd);
static size_t terminal_encode_utf8(uint32_t codepoint, char *dst);
static uint32_t terminal_map_charset(const struct ansi_parser *parser, uint32_t codepoint);
static int terminal_prepare_alternate_buffer(const struct terminal_buffer *source);
static void terminal_swap_alternate_buffer(struct terminal_buffer *buffer);
static int terminal_initialize_gl_program(const char *shader_path);
static void terminal_release_gl_resources(void);
static void terminal_clear_gl_shaders(void);
static void terminal_free_requested_shaders(void);
static int terminal_reload_requested_shaders(void);
static int terminal_enable_shaders(void);
static void terminal_disable_shaders(void);
static int terminal_shaders_active(void);
static int terminal_initialize_quad_geometry(void);
static void terminal_destroy_quad_geometry(void);
static int terminal_shader_configure_vaos(struct terminal_gl_shader *shader);
static void terminal_shader_clear_vaos(struct terminal_gl_shader *shader);
static void terminal_shader_reset_uniform_cache(struct terminal_gl_shader *shader);
static void terminal_shader_set_matrix(GLint location, GLfloat *cache, int *has_cache, const GLfloat *matrix);
static void terminal_shader_set_vec2(GLint location, GLfloat *cache, int *has_cache, GLfloat x, GLfloat y);
static void terminal_bind_texture(GLuint texture);
static int terminal_resize_render_targets(int width, int height);
static int terminal_upload_framebuffer(const uint8_t *pixels, int width, int height);
static int terminal_prepare_intermediate_targets(int width, int height);
static int terminal_prepare_shader_history(struct terminal_gl_shader *shader, int width, int height, int resized);
static void terminal_update_shader_history(struct terminal_gl_shader *shader, int width, int height);
static void terminal_update_flipped_history_texture(GLuint history_texture,
                                                    GLuint history_texture_flipped,
                                                    int width,
                                                    int height);
static void terminal_clear_shader_history(struct terminal_gl_shader *shader);
static int terminal_load_cursor_sprite(const char *path);
static void terminal_destroy_cursor_sprite(void);
static void terminal_set_mouse_cursor_visible(int visible);
static void terminal_cursor_update_position(int window_x, int window_y);
static void terminal_cursor_render(int framebuffer_width, int framebuffer_height, int drawable_width, int drawable_height);
static int psf_unicode_map_compare(const void *a, const void *b);
static int psf_font_lookup_unicode(const struct psf_font *font, uint32_t codepoint, uint32_t *out_index);
static uint32_t psf_font_resolve_glyph(const struct psf_font *font, uint32_t codepoint);
static void terminal_mark_full_redraw(void);
static void terminal_mark_background_dirty(void);
static uint32_t terminal_rgba_from_components(uint8_t r, uint8_t g, uint8_t b);
static uint32_t terminal_rgba_from_color(uint32_t color);
static int terminal_custom_pixels_set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t layer);
static void terminal_custom_pixels_clear(void);
static int terminal_custom_pixels_clear_rect(int origin_x, int origin_y, int width, int height, uint8_t layer);
static void terminal_custom_pixels_apply(uint8_t *framebuffer, int width, int height);
static int terminal_custom_pixels_reserve(size_t additional);
static int terminal_custom_pixels_draw_sprite(int origin_x, int origin_y, const uint8_t *rgba, int width, int height, uint8_t layer);
static uint16_t terminal_custom_layer_mask(uint8_t layer);
static void terminal_custom_pixels_mark_pending(uint8_t layer);
static int terminal_custom_pixels_apply_pending_clears(uint8_t layer);
static void terminal_custom_pixels_clear_pending_requests(void);
static void terminal_custom_pixels_shutdown(void);
static int terminal_ensure_render_cache(size_t columns, size_t rows);
static void terminal_reset_render_cache(void);
static char *terminal_read_text_file(const char *path, size_t *out_size);
static const char *terminal_skip_utf8_bom(const char *src, size_t *size);
static const char *terminal_skip_leading_space_and_comments(const char *src, const char *end);
static int terminal_utf8_next(const uint8_t *data, size_t length, size_t *offset, uint32_t *out_codepoint);
static int terminal_render_text_sprite(const struct psf_font *font,
                                       const uint8_t *text,
                                       size_t length,
                                       uint32_t color,
                                       uint8_t **out_pixels,
                                       int *out_width,
                                       int *out_height);
static int terminal_scheme_color_for_index(const struct terminal_buffer *buffer, long color_index, uint32_t *out_color);
struct terminal_shader_parameter {
    char *name;
    float default_value;
};
static void terminal_free_shader_parameters(struct terminal_shader_parameter *params, size_t count);
static int terminal_parse_shader_parameters(const char *source, size_t length, struct terminal_shader_parameter **out_params, size_t *out_count);
static float terminal_get_parameter_default(const struct terminal_shader_parameter *params, size_t count, const char *name, float fallback);
static GLuint terminal_compile_shader(GLenum type, const char *source, const char *label);
static void terminal_print_usage(const char *progname);
static int terminal_resolve_shader_path(const char *root_dir, const char *shader_arg, char *out_path, size_t out_size);
static void terminal_handle_osc_777(struct terminal_buffer *buffer, const char *args);

static int terminal_send_response(const char *response) {
    if (!response || response[0] == '\0') {
        return 0;
    }
    if (terminal_master_fd_handle < 0) {
        return 0;
    }
    return terminal_send_string(terminal_master_fd_handle, response);
}


#if BUDOSTACK_HAVE_SDL2
static void terminal_audio_channel_clear(struct terminal_sound_channel *channel) {
    if (!channel) {
        return;
    }
    if (channel->samples) {
        free(channel->samples);
        channel->samples = NULL;
    }
    channel->frame_count = 0u;
    channel->position = 0u;
    channel->active = 0;
    channel->volume = 1.0f;
}

static void SDLCALL terminal_audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    if (!stream || len <= 0) {
        return;
    }

    SDL_memset(stream, 0, (size_t)len);

    if (!terminal_audio_mutex) {
        return;
    }

    int channel_count = (int)terminal_audio_spec.channels;
    if (channel_count <= 0) {
        return;
    }

    if (SDL_LockMutex(terminal_audio_mutex) != 0) {
        return;
    }

    size_t byte_length = (size_t)len;
    size_t frames = byte_length / (sizeof(float) * (size_t)channel_count);
    if (frames == 0u) {
        SDL_UnlockMutex(terminal_audio_mutex);
        return;
    }

    float *output = (float *)stream;

    for (size_t channel_index = 0u; channel_index < TERMINAL_SOUND_CHANNEL_COUNT; channel_index++) {
        struct terminal_sound_channel *channel = &terminal_sound_channels[channel_index];
        if (!channel->active || !channel->samples || channel->frame_count == 0u) {
            continue;
        }

        size_t available_frames = 0u;
        if (channel->position < channel->frame_count) {
            available_frames = channel->frame_count - channel->position;
        }
        if (available_frames == 0u) {
            terminal_audio_channel_clear(channel);
            continue;
        }

        size_t mix_frames = frames;
        if (available_frames < mix_frames) {
            mix_frames = available_frames;
        }

        for (size_t frame_index = 0u; frame_index < mix_frames; frame_index++) {
            size_t output_offset = frame_index * (size_t)channel_count;
            size_t input_offset = (channel->position + frame_index) * (size_t)channel_count;
            for (int sample_channel = 0; sample_channel < channel_count; sample_channel++) {
                size_t sample_index = output_offset + (size_t)sample_channel;
                size_t input_index = input_offset + (size_t)sample_channel;
                output[sample_index] += channel->samples[input_index] * channel->volume;
            }
        }

        channel->position += mix_frames;
        if (channel->position >= channel->frame_count) {
            terminal_audio_channel_clear(channel);
        }
    }

    SDL_UnlockMutex(terminal_audio_mutex);

    size_t total_samples = frames * (size_t)channel_count;
    for (size_t i = 0u; i < total_samples; i++) {
        float sample = output[i];
        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        output[i] = sample;
    }
}

static int terminal_initialize_audio(void) {
    if (terminal_audio_device != 0) {
        return 0;
    }

    SDL_AudioSpec desired;
    SDL_zero(desired);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 4096;
    desired.callback = terminal_audio_callback;

    SDL_zero(terminal_audio_spec);
    terminal_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &terminal_audio_spec, 0);
    if (terminal_audio_device == 0) {
        fprintf(stderr, "terminal: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }

    if (!SDL_AUDIO_ISFLOAT(terminal_audio_spec.format) || SDL_AUDIO_BITSIZE(terminal_audio_spec.format) != 32) {
        fprintf(stderr, "terminal: Unsupported audio format %u\n", (unsigned int)terminal_audio_spec.format);
        SDL_CloseAudioDevice(terminal_audio_device);
        terminal_audio_device = 0;
        SDL_zero(terminal_audio_spec);
        return -1;
    }

    if (terminal_audio_spec.channels == 0) {
        fprintf(stderr, "terminal: Audio device reported zero channels.\n");
        SDL_CloseAudioDevice(terminal_audio_device);
        terminal_audio_device = 0;
        SDL_zero(terminal_audio_spec);
        return -1;
    }

    if (!terminal_audio_mutex) {
        terminal_audio_mutex = SDL_CreateMutex();
        if (!terminal_audio_mutex) {
            fprintf(stderr, "terminal: Failed to create audio mutex: %s\n", SDL_GetError());
            SDL_CloseAudioDevice(terminal_audio_device);
            terminal_audio_device = 0;
            SDL_zero(terminal_audio_spec);
            return -1;
        }
    }

    SDL_memset(terminal_sound_channels, 0, sizeof(terminal_sound_channels));

    SDL_PauseAudioDevice(terminal_audio_device, 0);
    return 0;
}

static void terminal_shutdown_audio(void) {
    if (terminal_audio_mutex) {
        if (SDL_LockMutex(terminal_audio_mutex) == 0) {
            for (size_t i = 0u; i < TERMINAL_SOUND_CHANNEL_COUNT; i++) {
                terminal_audio_channel_clear(&terminal_sound_channels[i]);
            }
            SDL_UnlockMutex(terminal_audio_mutex);
        } else {
            for (size_t i = 0u; i < TERMINAL_SOUND_CHANNEL_COUNT; i++) {
                terminal_audio_channel_clear(&terminal_sound_channels[i]);
            }
        }
    }

    if (terminal_audio_device != 0) {
        SDL_CloseAudioDevice(terminal_audio_device);
        terminal_audio_device = 0;
    }

    SDL_zero(terminal_audio_spec);

    if (terminal_audio_mutex) {
        SDL_DestroyMutex(terminal_audio_mutex);
        terminal_audio_mutex = NULL;
    }
}

static int terminal_audio_convert(const SDL_AudioSpec *source_spec, const void *data, size_t length, float **out_samples, size_t *out_frames) {
    if (!source_spec || !data || !out_samples || !out_frames || length == 0u) {
        return -1;
    }
    if (terminal_audio_device == 0 || terminal_audio_spec.channels == 0) {
        return -1;
    }

    SDL_AudioStream *stream = SDL_NewAudioStream(source_spec->format,
                                                 source_spec->channels,
                                                 source_spec->freq,
                                                 terminal_audio_spec.format,
                                                 terminal_audio_spec.channels,
                                                 terminal_audio_spec.freq);
    if (!stream) {
        fprintf(stderr, "terminal: SDL_NewAudioStream failed: %s\n", SDL_GetError());
        return -1;
    }

    size_t offset = 0u;
    while (offset < length) {
        size_t remaining = length - offset;
        size_t chunk = remaining;
        if (chunk > (size_t)INT_MAX) {
            chunk = (size_t)INT_MAX;
        }
        if (SDL_AudioStreamPut(stream, (const Uint8 *)data + offset, (int)chunk) != 0) {
            fprintf(stderr, "terminal: SDL_AudioStreamPut failed: %s\n", SDL_GetError());
            SDL_FreeAudioStream(stream);
            return -1;
        }
        offset += chunk;
    }

    if (SDL_AudioStreamFlush(stream) != 0) {
        fprintf(stderr, "terminal: SDL_AudioStreamFlush failed: %s\n", SDL_GetError());
        SDL_FreeAudioStream(stream);
        return -1;
    }

    int available = SDL_AudioStreamAvailable(stream);
    if (available <= 0) {
        SDL_FreeAudioStream(stream);
        return -1;
    }

    float *converted = malloc((size_t)available);
    if (!converted) {
        SDL_FreeAudioStream(stream);
        return -1;
    }

    int obtained = SDL_AudioStreamGet(stream, converted, available);
    if (obtained < 0) {
        fprintf(stderr, "terminal: SDL_AudioStreamGet failed: %s\n", SDL_GetError());
        free(converted);
        SDL_FreeAudioStream(stream);
        return -1;
    }

    SDL_FreeAudioStream(stream);

    if (obtained == 0) {
        free(converted);
        return -1;
    }

    if (obtained < available) {
        float *shrunk = realloc(converted, (size_t)obtained);
        if (shrunk) {
            converted = shrunk;
        }
        available = obtained;
    }

    size_t frame_count = (size_t)available / (sizeof(float) * (size_t)terminal_audio_spec.channels);
    if (frame_count == 0u) {
        free(converted);
        return -1;
    }

    *out_samples = converted;
    *out_frames = frame_count;
    return 0;
}

static int terminal_audio_load_file(const char *path, float **out_samples, size_t *out_frames) {
    if (!path || !out_samples || !out_frames) {
        return -1;
    }
    if (terminal_audio_device == 0) {
        fprintf(stderr, "terminal: Audio device is not initialized.\n");
        return -1;
    }

    const char *extension = strrchr(path, '.');
    if (!extension || extension[1] == '\0') {
        fprintf(stderr, "terminal: Unable to determine audio format for '%s'.\n", path);
        return -1;
    }

    char lower_ext[16];
    size_t ext_length = strlen(extension);
    if (ext_length >= sizeof(lower_ext)) {
        fprintf(stderr, "terminal: Audio file extension too long for '%s'.\n", path);
        return -1;
    }
    for (size_t i = 0u; i < ext_length; i++) {
        lower_ext[i] = (char)tolower((unsigned char)extension[i]);
    }
    lower_ext[ext_length] = '\0';

    if (strcmp(lower_ext, ".wav") == 0) {
        SDL_AudioSpec file_spec;
        Uint8 *buffer = NULL;
        Uint32 length = 0u;
        if (!SDL_LoadWAV(path, &file_spec, &buffer, &length)) {
            fprintf(stderr, "terminal: SDL_LoadWAV failed for '%s': %s\n", path, SDL_GetError());
            return -1;
        }
        int result = terminal_audio_convert(&file_spec, buffer, (size_t)length, out_samples, out_frames);
        SDL_FreeWAV(buffer);
        return result;
    } else if (strcmp(lower_ext, ".mp3") == 0) {
        drmp3 mp3;
        if (!drmp3_init_file(&mp3, path, NULL)) {
            fprintf(stderr, "terminal: Failed to open MP3 '%s'.\n", path);
            return -1;
        }

        drmp3_uint64 total_frames = drmp3_get_pcm_frame_count(&mp3);
        if (total_frames == 0) {
            drmp3_uninit(&mp3);
            fprintf(stderr, "terminal: MP3 '%s' contains no audio frames.\n", path);
            return -1;
        }

        if (mp3.channels <= 0) {
            drmp3_uninit(&mp3);
            fprintf(stderr, "terminal: MP3 '%s' has invalid channel count.\n", path);
            return -1;
        }

        drmp3_uint64 channel_count = (drmp3_uint64)mp3.channels;
        if (total_frames > UINT64_C(0) && channel_count > 0) {
            drmp3_uint64 max_frames = (drmp3_uint64)(SIZE_MAX / (sizeof(float) * channel_count));
            if (total_frames > max_frames) {
                drmp3_uninit(&mp3);
                fprintf(stderr, "terminal: MP3 '%s' is too large to decode.\n", path);
                return -1;
            }
        }

        size_t sample_count = (size_t)(total_frames * channel_count);
        float *temp = malloc(sample_count * sizeof(float));
        if (!temp) {
            drmp3_uninit(&mp3);
            return -1;
        }

        drmp3_uint64 frames_decoded = drmp3_read_pcm_frames_f32(&mp3, total_frames, temp);
        if (frames_decoded == 0) {
            free(temp);
            drmp3_uninit(&mp3);
            fprintf(stderr, "terminal: Failed to decode MP3 '%s'.\n", path);
            return -1;
        }

        size_t decoded_samples = (size_t)(frames_decoded * channel_count);
        SDL_AudioSpec file_spec;
        SDL_zero(file_spec);
        file_spec.format = AUDIO_F32SYS;
        file_spec.channels = (Uint8)mp3.channels;
        file_spec.freq = (int)mp3.sampleRate;

        int result = terminal_audio_convert(&file_spec, temp, decoded_samples * sizeof(float), out_samples, out_frames);
        free(temp);
        drmp3_uninit(&mp3);
        return result;
    } else if (strcmp(lower_ext, ".ogg") == 0) {
        int vorbis_error = 0;
        stb_vorbis *vorbis = stb_vorbis_open_filename(path, &vorbis_error, NULL);
        if (!vorbis) {
            fprintf(stderr, "terminal: Failed to open OGG '%s' (error %d).\n", path, vorbis_error);
            return -1;
        }

        stb_vorbis_info info = stb_vorbis_get_info(vorbis);
        if (info.channels <= 0) {
            stb_vorbis_close(vorbis);
            fprintf(stderr, "terminal: OGG '%s' has invalid channel count.\n", path);
            return -1;
        }

        unsigned int total_frames_u = stb_vorbis_stream_length_in_samples(vorbis);
        if (total_frames_u == 0u) {
            stb_vorbis_close(vorbis);
            fprintf(stderr, "terminal: OGG '%s' contains no audio frames.\n", path);
            return -1;
        }

        size_t channel_count = (size_t)info.channels;
        size_t total_frames = (size_t)total_frames_u;
        if (channel_count > 0u) {
            size_t max_frames = SIZE_MAX / (sizeof(float) * channel_count);
            if (total_frames > max_frames) {
                stb_vorbis_close(vorbis);
                fprintf(stderr, "terminal: OGG '%s' is too large to decode.\n", path);
                return -1;
            }
        }

        float *temp = malloc(total_frames * channel_count * sizeof(float));
        if (!temp) {
            stb_vorbis_close(vorbis);
            return -1;
        }

        size_t decoded_frames = 0u;
        while (decoded_frames < total_frames) {
            size_t remaining_frames = total_frames - decoded_frames;
            size_t max_request_frames = (size_t)INT_MAX / channel_count;
            if (max_request_frames == 0u) {
                break;
            }
            if (remaining_frames > max_request_frames) {
                remaining_frames = max_request_frames;
            }
            int frames = stb_vorbis_get_samples_float_interleaved(
                vorbis,
                info.channels,
                temp + decoded_frames * channel_count,
                (int)(remaining_frames * channel_count));
            if (frames <= 0) {
                break;
            }
            decoded_frames += (size_t)frames;
        }

        stb_vorbis_close(vorbis);

        if (decoded_frames == 0u) {
            free(temp);
            fprintf(stderr, "terminal: Failed to decode OGG '%s'.\n", path);
            return -1;
        }

        if (decoded_frames < total_frames) {
            size_t used_samples = decoded_frames * channel_count;
            float *shrunk = realloc(temp, used_samples * sizeof(float));
            if (shrunk) {
                temp = shrunk;
            }
        }

        SDL_AudioSpec file_spec;
        SDL_zero(file_spec);
        file_spec.format = AUDIO_F32SYS;
        file_spec.channels = (Uint8)info.channels;
        file_spec.freq = (int)info.sample_rate;

        int result = terminal_audio_convert(&file_spec, temp, decoded_frames * channel_count * sizeof(float), out_samples, out_frames);
        free(temp);
        return result;
    }

    fprintf(stderr, "terminal: Unsupported audio format '%s'.\n", extension);
    return -1;
}

static int terminal_sound_play(int channel_index, const char *path, float volume) {
    if (channel_index < 0 || channel_index >= (int)TERMINAL_SOUND_CHANNEL_COUNT) {
        fprintf(stderr, "terminal: Sound channel %d out of range.\n", channel_index + 1);
        return -1;
    }
    if (!path || path[0] == '\0') {
        fprintf(stderr, "terminal: Sound path is empty.\n");
        return -1;
    }
    if (terminal_audio_device == 0 || !terminal_audio_mutex) {
        fprintf(stderr, "terminal: Audio subsystem not initialized.\n");
        return -1;
    }

    float *samples = NULL;
    size_t frames = 0u;
    if (terminal_audio_load_file(path, &samples, &frames) != 0) {
        return -1;
    }

    if (SDL_LockMutex(terminal_audio_mutex) != 0) {
        fprintf(stderr, "terminal: Failed to lock audio mutex: %s\n", SDL_GetError());
        free(samples);
        return -1;
    }

    struct terminal_sound_channel *channel = &terminal_sound_channels[(size_t)channel_index];
    terminal_audio_channel_clear(channel);
    channel->samples = samples;
    channel->frame_count = frames;
    channel->position = 0u;
    channel->active = 1;
    if (volume < 0.0f) {
        volume = 0.0f;
    } else if (volume > 1.0f) {
        volume = 1.0f;
    }
    channel->volume = volume;

    SDL_UnlockMutex(terminal_audio_mutex);
    return 0;
}

static void terminal_sound_stop(int channel_index) {
    if (channel_index < 0 || channel_index >= (int)TERMINAL_SOUND_CHANNEL_COUNT) {
        return;
    }
    if (!terminal_audio_mutex) {
        return;
    }

    if (SDL_LockMutex(terminal_audio_mutex) != 0) {
        fprintf(stderr, "terminal: Failed to lock audio mutex for stop: %s\n", SDL_GetError());
        return;
    }

    terminal_audio_channel_clear(&terminal_sound_channels[(size_t)channel_index]);
    SDL_UnlockMutex(terminal_audio_mutex);
}
#endif


struct terminal_cell {
    uint32_t ch;
    uint32_t fg;
    uint32_t bg;
    uint8_t style;
};

struct terminal_attributes {
    uint32_t fg;
    uint32_t bg;
    uint8_t style;
    uint8_t use_default_fg;
    uint8_t use_default_bg;
};

#define TERMINAL_STYLE_BOLD 0x01u
#define TERMINAL_STYLE_UNDERLINE 0x02u
#define TERMINAL_STYLE_REVERSE 0x04u

struct terminal_buffer {
    size_t columns;
    size_t rows;
    size_t cursor_column;
    size_t cursor_row;
    size_t saved_cursor_column;
    size_t saved_cursor_row;
    size_t scroll_top;
    size_t scroll_bottom;
    int cursor_saved;
    int attr_saved;
    struct terminal_cell *cells;
    struct terminal_cell *history;
    struct terminal_attributes current_attr;
    struct terminal_attributes saved_attr;
    uint32_t default_fg;
    uint32_t default_bg;
    uint32_t cursor_color;
    int cursor_visible;
    int saved_cursor_visible;
    int bracketed_paste_enabled; // to pass multi-row paste for edit
    int app_keypad;
    int app_cursor;
    int mouse_tracking;
    int mouse_drag_tracking;
    int mouse_motion_tracking;
    int mouse_sgr;
    size_t history_limit;
    size_t history_rows;
    size_t history_start;
    size_t scroll_offset;
    uint32_t palette[256];
    uint32_t last_emitted;
    int last_emitted_valid;
};

static void terminal_apply_scale(struct terminal_buffer *buffer, int scale);
static void terminal_apply_margin(struct terminal_buffer *buffer, int margin);
static void terminal_apply_resolution(struct terminal_buffer *buffer, int width, int height);
static int terminal_resize_buffer(struct terminal_buffer *buffer, size_t columns, size_t rows);

enum ansi_parser_state {
    ANSI_STATE_GROUND = 0,
    ANSI_STATE_ESCAPE,
    ANSI_STATE_ESCAPE_CHARSET,
    ANSI_STATE_CSI,
    ANSI_STATE_OSC,
    ANSI_STATE_OSC_ESCAPE
};

#define ANSI_MAX_PARAMS 16

struct ansi_parser {
    enum ansi_parser_state state;
    int params[ANSI_MAX_PARAMS];
    size_t param_count;
    int collecting_param;
    int private_marker;
    char charset_g0;
    char charset_g1;
    char charset_target;
    int charset_use_g1;
    char osc_buffer[131072];
    size_t osc_length;
    uint32_t utf8_codepoint;
    uint32_t utf8_min_value;
    uint8_t utf8_bytes_expected;
    uint8_t utf8_bytes_seen;
};

static size_t terminal_total_rows(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0u;
    }
    return buffer->history_rows + buffer->rows;
}

static size_t terminal_clamped_scroll_offset(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0u;
    }
    size_t offset = buffer->scroll_offset;
    if (offset > buffer->history_rows) {
        offset = buffer->history_rows;
    }
    return offset;
}

static void terminal_visible_row_range(const struct terminal_buffer *buffer, size_t *out_top_index, size_t *out_bottom_index) {
    size_t top_index = 0u;
    size_t bottom_index = 0u;
    if (buffer) {
        size_t clamped_scroll = terminal_clamped_scroll_offset(buffer);
        size_t total_available_rows = terminal_total_rows(buffer);
        if (total_available_rows > 0u) {
            bottom_index = total_available_rows - 1u;
            if (clamped_scroll <= bottom_index) {
                bottom_index -= clamped_scroll;
            } else {
                bottom_index = 0u;
            }
        } else {
            bottom_index = 0u;
        }
        if (buffer->rows > 0u && bottom_index + 1u > buffer->rows) {
            top_index = bottom_index + 1u - buffer->rows;
        } else {
            top_index = 0u;
        }
    }
    if (out_top_index) {
        *out_top_index = top_index;
    }
    if (out_bottom_index) {
        *out_bottom_index = bottom_index;
    }
}

static int terminal_window_point_to_framebuffer(int window_x, int window_y, int *out_x, int *out_y) {
    if (!out_x || !out_y) {
        return -1;
    }
    if (!terminal_window_handle || terminal_framebuffer_width <= 0 || terminal_framebuffer_height <= 0) {
        return -1;
    }

    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(terminal_window_handle, &window_width, &window_height);
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(terminal_window_handle, &drawable_width, &drawable_height);

    double reference_width = (window_width > 0) ? (double)window_width : (double)drawable_width;
    double reference_height = (window_height > 0) ? (double)window_height : (double)drawable_height;
    if (reference_width <= 0.0 || reference_height <= 0.0) {
        return -1;
    }

    double normalized_x = (double)window_x / reference_width;
    double normalized_y = (double)window_y / reference_height;
    double framebuffer_x = normalized_x * (double)terminal_framebuffer_width;
    double framebuffer_y = normalized_y * (double)terminal_framebuffer_height;

    if (framebuffer_x < (double)INT_MIN) {
        framebuffer_x = (double)INT_MIN;
    }
    if (framebuffer_x > (double)INT_MAX) {
        framebuffer_x = (double)INT_MAX;
    }
    if (framebuffer_y < (double)INT_MIN) {
        framebuffer_y = (double)INT_MIN;
    }
    if (framebuffer_y > (double)INT_MAX) {
        framebuffer_y = (double)INT_MAX;
    }

    *out_x = (int)framebuffer_x;
    *out_y = (int)framebuffer_y;
    return 0;
}

static int terminal_screen_point_to_cell(int x,
                                         int y,
                                         size_t columns,
                                         size_t rows,
                                         size_t top_index,
                                         size_t total_rows,
                                         size_t *out_global_row,
                                         size_t *out_column,
                                         int clamp_to_bounds) {
    if (!out_global_row || !out_column || columns == 0u || rows == 0u) {
        return -1;
    }
    if (terminal_cell_pixel_width <= 0 || terminal_cell_pixel_height <= 0) {
        return -1;
    }

    int margin = terminal_margin_pixels;
    if (margin < 0) {
        margin = 0;
    }

    int inner_x = x - margin;
    int inner_y = y - margin;
    size_t width_pixels = columns * (size_t)terminal_cell_pixel_width;
    size_t height_pixels = rows * (size_t)terminal_cell_pixel_height;

    if (!clamp_to_bounds) {
        if (inner_x < 0 || inner_y < 0) {
            return -1;
        }
        if ((size_t)inner_x >= width_pixels || (size_t)inner_y >= height_pixels) {
            return -1;
        }
    } else {
        if (inner_x < 0) {
            inner_x = 0;
        }
        if (inner_y < 0) {
            inner_y = 0;
        }
        if ((size_t)inner_x > width_pixels) {
            inner_x = (int)width_pixels;
        }
        if ((size_t)inner_y > height_pixels) {
            inner_y = (int)height_pixels;
        }
    }

    size_t column = 0u;
    size_t row_in_view = 0u;
    if (terminal_cell_pixel_width > 0) {
        column = (size_t)inner_x / (size_t)terminal_cell_pixel_width;
    }
    if (terminal_cell_pixel_height > 0) {
        row_in_view = (size_t)inner_y / (size_t)terminal_cell_pixel_height;
    }

    if (column > columns) {
        column = columns;
    }
    if (row_in_view > rows) {
        row_in_view = rows;
    }

    size_t global_row = top_index + row_in_view;
    if (global_row > total_rows) {
        global_row = total_rows;
    }
    if (global_row == total_rows) {
        column = 0u;
    }

    *out_global_row = global_row;
    *out_column = column;
    return 0;
}

static void terminal_selection_clear(void) {
    terminal_selection_active = 0;
    terminal_selection_dragging = 0;
    terminal_selection_anchor_row = 0u;
    terminal_selection_anchor_col = 0u;
    terminal_selection_caret_row = 0u;
    terminal_selection_caret_col = 0u;
}

static void terminal_selection_begin(size_t global_row, size_t column) {
    terminal_selection_active = 1;
    terminal_selection_anchor_row = global_row;
    terminal_selection_anchor_col = column;
    terminal_selection_caret_row = global_row;
    terminal_selection_caret_col = column;
}

static void terminal_selection_update(size_t global_row, size_t column) {
    if (!terminal_selection_active) {
        terminal_selection_begin(global_row, column);
        return;
    }
    terminal_selection_caret_row = global_row;
    terminal_selection_caret_col = column;
}

static void terminal_selection_validate(const struct terminal_buffer *buffer) {
    if (!terminal_selection_active || !buffer) {
        return;
    }
    size_t total_rows = terminal_total_rows(buffer);
    if (total_rows == 0u) {
        terminal_selection_clear();
        return;
    }
    if (terminal_selection_anchor_row >= total_rows) {
        terminal_selection_clear();
        return;
    }
    if (terminal_selection_caret_row > total_rows) {
        terminal_selection_caret_row = total_rows;
    }
    size_t columns = buffer->columns;
    if (terminal_selection_anchor_col > columns) {
        terminal_selection_anchor_col = columns;
    }
    if (terminal_selection_caret_col > columns) {
        terminal_selection_caret_col = columns;
    }
    if (terminal_selection_caret_row == total_rows) {
        terminal_selection_caret_col = 0u;
    }
}

static int terminal_selection_linear_range(const struct terminal_buffer *buffer, size_t *out_start, size_t *out_end) {
    if (!buffer || buffer->columns == 0u || !terminal_selection_active) {
        return 0;
    }
    size_t total_rows = terminal_total_rows(buffer);
    if (total_rows == 0u) {
        return 0;
    }
    size_t columns = buffer->columns;
    size_t anchor_row = terminal_selection_anchor_row;
    size_t anchor_col = terminal_selection_anchor_col;
    size_t caret_row = terminal_selection_caret_row;
    size_t caret_col = terminal_selection_caret_col;
    if (anchor_row >= total_rows) {
        return 0;
    }
    if (caret_row > total_rows) {
        caret_row = total_rows;
    }
    if (anchor_col > columns) {
        anchor_col = columns;
    }
    if (caret_col > columns) {
        caret_col = columns;
    }
    size_t anchor_linear = anchor_row * columns + anchor_col;
    size_t caret_linear = caret_row * columns + caret_col;
    if (anchor_linear == caret_linear) {
        return 0;
    }
    size_t start = anchor_linear < caret_linear ? anchor_linear : caret_linear;
    size_t end = anchor_linear < caret_linear ? caret_linear : anchor_linear;
    if (out_start) {
        *out_start = start;
    }
    if (out_end) {
        *out_end = end;
    }
    return 1;
}

static int terminal_selection_contains_cell(size_t global_row,
                                            size_t column,
                                            size_t selection_start,
                                            size_t selection_end,
                                            size_t columns) {
    if (columns == 0u || selection_end <= selection_start) {
        return 0;
    }
    size_t cell_index = global_row * columns + column;
    if (cell_index < selection_start || cell_index >= selection_end) {
        return 0;
    }
    return 1;
}

static size_t terminal_encode_utf8(uint32_t codepoint, char *dst) {
    if (!dst) {
        return 0u;
    }
    uint32_t cp = codepoint;
    if (cp > 0x10FFFFu) {
        cp = 0xFFFDu;
    }
    if (cp <= 0x7Fu) {
        dst[0] = (char)cp;
        return 1u;
    }
    if (cp <= 0x7FFu) {
        dst[0] = (char)(0xC0u | (cp >> 6u));
        dst[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp <= 0xFFFFu) {
        dst[0] = (char)(0xE0u | (cp >> 12u));
        dst[1] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
        dst[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    dst[0] = (char)(0xF0u | (cp >> 18u));
    dst[1] = (char)(0x80u | ((cp >> 12u) & 0x3Fu));
    dst[2] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
    dst[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4u;
}

static uint32_t terminal_map_charset(const struct ansi_parser *parser, uint32_t codepoint) {
    if (!parser || codepoint >= 128u) {
        return codepoint;
    }

    char charset = parser->charset_use_g1 ? parser->charset_g1 : parser->charset_g0;
    if (charset != '0') {
        return codepoint;
    }

    switch (codepoint) {
    case 'j': return 0x2518u; /* lower-right corner */
    case 'k': return 0x2510u; /* upper-right corner */
    case 'l': return 0x250Cu; /* upper-left corner */
    case 'm': return 0x2514u; /* lower-left corner */
    case 'n': return 0x253Cu; /* crossing lines */
    case 'q': return 0x2500u; /* horizontal line */
    case 't': return 0x251Cu; /* left tee */
    case 'u': return 0x2524u; /* right tee */
    case 'v': return 0x2534u; /* bottom tee */
    case 'w': return 0x252Cu; /* top tee */
    case 'x': return 0x2502u; /* vertical line */
    default:
        return codepoint;
    }
}

static int terminal_copy_selection_to_clipboard(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0;
    }
    size_t selection_start = 0u;
    size_t selection_end = 0u;
    if (!terminal_selection_linear_range(buffer, &selection_start, &selection_end)) {
        return 0;
    }
    size_t columns = buffer->columns;
    if (columns == 0u) {
        return 0;
    }
    size_t cell_span = selection_end - selection_start;
    size_t newline_count = (selection_end / columns > selection_start / columns)
        ? ((selection_end / columns) - (selection_start / columns))
        : 0u;
    if (cell_span > SIZE_MAX / 4u) {
        return 0;
    }
    size_t max_bytes = cell_span * 4u;
    if (newline_count > SIZE_MAX - max_bytes - 1u) {
        return 0;
    }
    max_bytes += newline_count + 1u;
    char *output = malloc(max_bytes);
    if (!output) {
        return 0;
    }

    size_t start_row = selection_start / columns;
    size_t start_col = selection_start % columns;
    size_t end_row = selection_end / columns;
    size_t end_col = selection_end % columns;

    size_t out_len = 0u;
    for (size_t row = start_row;; row++) {
        size_t first_col = (row == start_row) ? start_col : 0u;
        size_t last_col = (row == end_row) ? end_col : columns;
        if (first_col > columns) {
            first_col = columns;
        }
        if (last_col > columns) {
            last_col = columns;
        }
        if (first_col < last_col) {
            const struct terminal_cell *row_cells = NULL;
            if (row < terminal_total_rows(buffer)) {
                row_cells = terminal_buffer_row_at(buffer, row);
            }
            size_t row_start_len = out_len;
            size_t last_non_space_len = row_start_len;
            int seen_non_space = 0;
            for (size_t col = first_col; col < last_col; col++) {
                uint32_t ch = ' ';
                if (row_cells) {
                    ch = row_cells[col].ch;
                }
                if (ch == 0u) {
                    ch = ' ';
                }
                char encoded[4];
                size_t encoded_len = terminal_encode_utf8(ch, encoded);
                if (encoded_len == 0u) {
                    continue;
                }
                if (out_len + encoded_len >= max_bytes) {
                    free(output);
                    return 0;
                }
                memcpy(output + out_len, encoded, encoded_len);
                out_len += encoded_len;
                if (ch != ' ') {
                    seen_non_space = 1;
                    last_non_space_len = out_len;
                }
            }
            if (seen_non_space) {
                out_len = last_non_space_len;
            } else {
                out_len = row_start_len;
            }
        }
        if (row < end_row) {
            if (out_len + 1u >= max_bytes) {
                free(output);
                return 0;
            }
            output[out_len++] = '\n';
        }
        if (row == end_row) {
            break;
        }
    }

    output[out_len] = '\0';
    if (SDL_SetClipboardText(output) != 0) {
        free(output);
        return 0;
    }
    free(output);
    return 1;
}

/* Old implementation
static int terminal_paste_from_clipboard(int fd) {
    char *text = SDL_GetClipboardText();
    if (!text) {
        return -1;
    }
    size_t len = strlen(text);
    int result = 0;
    if (len > 0u) {
        if (terminal_send_bytes(fd, text, len) < 0) {
            result = -1;
        }
    }
    SDL_free(text);
    return result;
}
*/
static int terminal_paste_from_clipboard(struct terminal_buffer *buffer, int fd) {
    char *text = SDL_GetClipboardText();
    if (!text) {
        return -1;
    }
    size_t len = strlen(text);
    int result = 0;
    
    if (len > 0u) {
        if (buffer && buffer->bracketed_paste_enabled) {
            const char *start = "\x1b[200~";
            const char *end   = "\x1b[201~";
            
            if (terminal_send_bytes(fd, start, strlen(start)) < 0) {
                result = -1;
            }
            if (result == 0 && terminal_send_bytes(fd, text, len) < 0) {
                result = -1;
            }
            if (result == 0 && terminal_send_bytes(fd, end, strlen(end)) < 0) {
                result = -1;
            }
        } else {
            if (terminal_send_bytes(fd, text, len) < 0) {
                result = -1;
            }
        }
        
    }
    
    SDL_free(text);
    return result;
}

static int terminal_mouse_reporting_enabled(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0;
    }
    return buffer->mouse_tracking || buffer->mouse_drag_tracking || buffer->mouse_motion_tracking;
}

static int terminal_send_mouse_report(const struct terminal_buffer *buffer,
                                      int button_code,
                                      int released,
                                      int motion,
                                      size_t column,
                                      size_t row,
                                      SDL_Keymod mod) {
    if (!buffer || !buffer->mouse_sgr) {
        return 0;
    }

    int code = button_code & 0x7F;
    if ((mod & KMOD_SHIFT) != 0) {
        code |= 4;
    }
    if ((mod & KMOD_ALT) != 0) {
        code |= 8;
    }
    if ((mod & KMOD_CTRL) != 0) {
        code |= 16;
    }
    if (motion) {
        code |= 32;
    }

    char sequence[64];
    char final = released ? 'm' : 'M';
    int written = snprintf(sequence, sizeof(sequence), "\x1b[<%d;%zu;%zu%c",
                           code,
                           column,
                           row,
                           final);
    if (written <= 0 || (size_t)written >= sizeof(sequence)) {
        return -1;
    }
    return terminal_send_response(sequence);
}

static const uint32_t terminal_default_palette16[16] = {
    0x000000u, /* black */
    0xAA0000u, /* red */
    0x00AA00u, /* green */
    0xAA5500u, /* yellow/brown */
    0x0000AAu, /* blue */
    0xAA00AAu, /* magenta */
    0x00AAAAu, /* cyan */
    0xAAAAAAu, /* white */
    0x555555u, /* bright black */
    0xFF5555u, /* bright red */
    0x55FF55u, /* bright green */
    0xFFFF55u, /* bright yellow */
    0x5555FFu, /* bright blue */
    0xFF55FFu, /* bright magenta */
    0x55FFFFu, /* bright cyan */
    0xFFFFFFu  /* bright white */
};

static uint32_t terminal_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16u) | ((uint32_t)g << 8u) | (uint32_t)b;
}

static uint8_t terminal_color_r(uint32_t color) {
    return (uint8_t)((color >> 16u) & 0xFFu);
}

static uint8_t terminal_color_g(uint32_t color) {
    return (uint8_t)((color >> 8u) & 0xFFu);
}

static uint8_t terminal_color_b(uint32_t color) {
    return (uint8_t)(color & 0xFFu);
}

static uint8_t terminal_boost_component(uint8_t value) {
    uint16_t boosted = (uint16_t)value + (uint16_t)((255u - value) / 2u);
    if (boosted > 255u) {
        boosted = 255u;
    }
    return (uint8_t)boosted;
}

static uint32_t terminal_bold_variant(uint32_t color) {
    uint8_t r = terminal_color_r(color);
    uint8_t g = terminal_color_g(color);
    uint8_t b = terminal_color_b(color);
    r = terminal_boost_component(r);
    g = terminal_boost_component(g);
    b = terminal_boost_component(b);
    return terminal_pack_rgb(r, g, b);
}

static void terminal_mark_full_redraw(void) {
    terminal_force_full_redraw = 1;
}

static void terminal_mark_background_dirty(void) {
    terminal_background_dirty = 1;
    terminal_force_full_redraw = 1;
}

static uint32_t terminal_rgba_from_components(uint8_t r, uint8_t g, uint8_t b) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    return ((uint32_t)r << 24u) | ((uint32_t)g << 16u) | ((uint32_t)b << 8u) | 0xFFu;
#else
    return (uint32_t)r | ((uint32_t)g << 8u) | ((uint32_t)b << 16u) | 0xFF000000u;
#endif
}

static uint32_t terminal_rgba_from_color(uint32_t color) {
    return terminal_rgba_from_components(terminal_color_r(color),
                                         terminal_color_g(color),
                                         terminal_color_b(color));
}

static uint16_t terminal_custom_layer_mask(uint8_t layer) {
    if (layer < 1u || layer > 16u) {
        return 0u;
    }
    return (uint16_t)(1u << (layer - 1u));
}

static void terminal_custom_pixels_mark_pending(uint8_t layer) {
    uint16_t mask = terminal_custom_layer_mask(layer);
    if (mask != 0u) {
        terminal_custom_pixels_pending_layers |= mask;
    }
}

static void terminal_custom_pixels_clear_pending_requests(void) {
    terminal_custom_pending_clear_count = 0u;
}

static void terminal_custom_pixels_shutdown(void) {
    free(terminal_custom_pixels);
    terminal_custom_pixels = NULL;
    terminal_custom_pixel_count = 0u;
    terminal_custom_pixel_capacity = 0u;
    terminal_custom_pixels_dirty = 0;
    terminal_custom_pixels_pending_layers = 0u;
    terminal_custom_pixels_active = 0;
    free(terminal_custom_pending_clears);
    terminal_custom_pending_clears = NULL;
    terminal_custom_pending_clear_count = 0u;
    terminal_custom_pending_clear_capacity = 0u;
    for (size_t i = 0u; i < sizeof(terminal_custom_layer_versions) / sizeof(terminal_custom_layer_versions[0]); i++) {
        terminal_custom_layer_versions[i] = 0u;
    }
}

static void terminal_custom_pixels_clear(void) {
    terminal_custom_pixel_count = 0u;
    terminal_custom_pixels_pending_layers = 0u;
    terminal_custom_pixels_active = 0;
    terminal_custom_pixels_clear_pending_requests();
    for (size_t i = 0u; i < sizeof(terminal_custom_layer_versions) / sizeof(terminal_custom_layer_versions[0]); i++) {
        terminal_custom_layer_versions[i] = 0u;
    }
    terminal_mark_full_redraw();
}

static int terminal_custom_pixels_reserve_clear_requests(size_t additional) {
    if (additional == 0u) {
        return 0;
    }

    if (terminal_custom_pending_clear_count > SIZE_MAX - additional) {
        return -1;
    }

    size_t required = terminal_custom_pending_clear_count + additional;
    if (required <= terminal_custom_pending_clear_capacity) {
        return 0;
    }

    size_t new_capacity = (terminal_custom_pending_clear_capacity == 0u) ? 4u : terminal_custom_pending_clear_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }

    struct terminal_custom_clear_request *new_reqs = realloc(terminal_custom_pending_clears,
                                                             new_capacity * sizeof(*terminal_custom_pending_clears));
    if (!new_reqs) {
        return -1;
    }

    terminal_custom_pending_clears = new_reqs;
    terminal_custom_pending_clear_capacity = new_capacity;
    return 0;
}

static int terminal_custom_pixels_clear_rect(int origin_x, int origin_y, int width, int height, uint8_t layer) {
    if (width <= 0 || height <= 0) {
        return -1;
    }

    if (origin_x < 0 || origin_y < 0) {
        return -1;
    }

    if (layer < 1u || layer > 16u) {
        return -1;
    }

    if (origin_x > INT_MAX - width || origin_y > INT_MAX - height) {
        return -1;
    }

    if (terminal_custom_pixels_reserve_clear_requests(1u) != 0) {
        return -1;
    }

    uint64_t previous_version = terminal_custom_layer_versions[layer];
    if (terminal_custom_layer_versions[layer] < UINT64_MAX) {
        terminal_custom_layer_versions[layer]++;
    }

    struct terminal_custom_clear_request *req = &terminal_custom_pending_clears[terminal_custom_pending_clear_count++];
    req->x = origin_x;
    req->y = origin_y;
    req->w = width;
    req->h = height;
    req->layer = layer;
    req->version = previous_version;

    terminal_custom_pixels_mark_pending(layer);
    terminal_custom_pixels_active = 1;
    return 0;
}

static int terminal_custom_pixels_apply_clear_request(const struct terminal_custom_clear_request *req) {
    if (!req) {
        return 0;
    }

    int origin_x = req->x;
    int origin_y = req->y;
    int width = req->w;
    int height = req->h;
    uint8_t layer = req->layer;
    uint64_t version = req->version;

    if (width <= 0 || height <= 0) {
        return 0;
    }

    int max_x = origin_x + width;
    int max_y = origin_y + height;
    size_t write_idx = 0u;
    for (size_t read_idx = 0u; read_idx < terminal_custom_pixel_count; read_idx++) {
        struct terminal_custom_pixel *entry = &terminal_custom_pixels[read_idx];
        if (entry->layer == layer && entry->version <= version && entry->x >= origin_x && entry->x < max_x &&
            entry->y >= origin_y && entry->y < max_y) {
            continue;
        }
        if (write_idx != read_idx) {
            terminal_custom_pixels[write_idx] = *entry;
        }
        write_idx++;
    }

    if (write_idx == terminal_custom_pixel_count) {
        return 0;
    }

    terminal_custom_pixel_count = write_idx;
    terminal_custom_pixels_active = (terminal_custom_pixel_count > 0u);
    return 1;
}

static int terminal_custom_pixels_apply_pending_clears(uint8_t layer) {
    if (terminal_custom_pending_clear_count == 0u) {
        return 0;
    }

    size_t write_idx = 0u;
    int modified = 0;
    for (size_t i = 0u; i < terminal_custom_pending_clear_count; i++) {
        struct terminal_custom_clear_request *req = &terminal_custom_pending_clears[i];
        if (layer != 0u && req->layer != layer) {
            if (write_idx != i) {
                terminal_custom_pending_clears[write_idx] = *req;
            }
            write_idx++;
            continue;
        }

        if (terminal_custom_pixels_apply_clear_request(req) != 0) {
            modified = 1;
        }
    }

    terminal_custom_pending_clear_count = write_idx;
    return modified;
}

static int terminal_custom_pixels_reserve(size_t additional) {
    if (additional == 0u) {
        return 0;
    }
    if (terminal_custom_pixel_count > SIZE_MAX - additional) {
        return -1;
    }
    size_t required = terminal_custom_pixel_count + additional;
    if (required <= terminal_custom_pixel_capacity) {
        return 0;
    }

    size_t new_capacity = (terminal_custom_pixel_capacity == 0u) ? 64u : terminal_custom_pixel_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }

    struct terminal_custom_pixel *new_pixels =
        realloc(terminal_custom_pixels, new_capacity * sizeof(*terminal_custom_pixels));
    if (!new_pixels) {
        return -1;
    }
    terminal_custom_pixels = new_pixels;
    terminal_custom_pixel_capacity = new_capacity;
    return 0;
}

static int terminal_custom_pixels_set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t layer) {
    if (x < 0 || y < 0) {
        return -1;
    }

    if (layer < 1u || layer > 16u) {
        return -1;
    }

    for (size_t i = 0u; i < terminal_custom_pixel_count; i++) {
        struct terminal_custom_pixel *entry = &terminal_custom_pixels[i];
        if (entry->x == x && entry->y == y && entry->layer == layer) {
            if (entry->r == r && entry->g == g && entry->b == b) {
                return 0;
            }
            entry->r = r;
            entry->g = g;
            entry->b = b;
            entry->version = terminal_custom_layer_versions[layer];
            terminal_custom_pixels_mark_pending(layer);
            return 0;
        }
    }

    if (terminal_custom_pixels_reserve(1u) != 0) {
        return -1;
    }

    struct terminal_custom_pixel *entry = &terminal_custom_pixels[terminal_custom_pixel_count++];
    entry->x = x;
    entry->y = y;
    entry->r = r;
    entry->g = g;
    entry->b = b;
    entry->layer = layer;
    entry->version = terminal_custom_layer_versions[layer];
    terminal_custom_pixels_mark_pending(layer);
    return 0;
}

static int terminal_custom_pixels_draw_sprite(int origin_x, int origin_y, const uint8_t *rgba, int width, int height, uint8_t layer) {
    if (!rgba || width <= 0 || height <= 0) {
        return -1;
    }
    if (origin_x < 0 || origin_y < 0) {
        return -1;
    }
    if (layer < 1u || layer > 16u) {
        return -1;
    }
    if (width > INT_MAX - origin_x || height > INT_MAX - origin_y) {
        return -1;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (width_sz != 0u && height_sz > SIZE_MAX / width_sz) {
        return -1;
    }

    size_t pixel_count = width_sz * height_sz;
    size_t opaque_pixels = 0u;
    for (size_t i = 0u; i < pixel_count; i++) {
        size_t idx = i * 4u + 3u;
        if (rgba[idx] != 0u) {
            opaque_pixels++;
        }
    }

    if (opaque_pixels == 0u) {
        return 0;
    }

    if (terminal_custom_pixels_reserve(opaque_pixels) != 0) {
        return -1;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ((size_t)y * width_sz + (size_t)x) * 4u;
            uint8_t a = rgba[idx + 3u];
            if (a == 0u) {
                continue;
            }
            uint8_t r = rgba[idx];
            uint8_t g = rgba[idx + 1u];
            uint8_t b = rgba[idx + 2u];
            if (a < 255u) {
                r = (uint8_t)((((uint32_t)r) * (uint32_t)a + 127u) / 255u);
                g = (uint8_t)((((uint32_t)g) * (uint32_t)a + 127u) / 255u);
                b = (uint8_t)((((uint32_t)b) * (uint32_t)a + 127u) / 255u);
            }

            struct terminal_custom_pixel *entry = &terminal_custom_pixels[terminal_custom_pixel_count++];
            entry->x = origin_x + x;
            entry->y = origin_y + y;
            entry->r = r;
            entry->g = g;
            entry->b = b;
            entry->layer = layer;
            entry->version = terminal_custom_layer_versions[layer];
        }
    }

    terminal_custom_pixels_mark_pending(layer);
    terminal_custom_pixels_active = 1;
    return 0;
}

static void terminal_custom_pixels_apply(uint8_t *framebuffer, int width, int height) {
    if (!framebuffer || width <= 0 || height <= 0) {
        return;
    }

    size_t frame_pitch = (size_t)width * 4u;
    uint16_t pending_mask = terminal_custom_pixels_pending_layers;
    for (int layer = 16; layer >= 1; layer--) {
        for (size_t i = 0u; i < terminal_custom_pixel_count; i++) {
            const struct terminal_custom_pixel *entry = &terminal_custom_pixels[i];
            if (entry->layer != (uint8_t)layer) {
                continue;
            }
            if ((pending_mask & terminal_custom_layer_mask(entry->layer)) != 0u) {
                continue;
            }
            if (entry->x < 0 || entry->y < 0) {
                continue;
            }
            if (entry->x >= width || entry->y >= height) {
                continue;
            }
            uint8_t *dst = framebuffer + (size_t)entry->y * frame_pitch + (size_t)entry->x * 4u;
            uint32_t *dst32 = (uint32_t *)dst;
            *dst32 = terminal_rgba_from_components(entry->r, entry->g, entry->b);
        }
    }
}

static int terminal_ensure_render_cache(size_t columns, size_t rows) {
    if (columns == 0u || rows == 0u) {
        return -1;
    }
    if (rows > SIZE_MAX / columns) {
        return -1;
    }
    size_t cell_count = columns * rows;
    if (cell_count == terminal_render_cache_count &&
        columns == terminal_render_cache_columns &&
        rows == terminal_render_cache_rows) {
        return 0;
    }
    if (cell_count > SIZE_MAX / sizeof(*terminal_render_cache)) {
        return -1;
    }
    struct terminal_render_cache_entry *new_cache =
        calloc(cell_count, sizeof(*terminal_render_cache));
    if (!new_cache) {
        return -1;
    }
    free(terminal_render_cache);
    terminal_render_cache = new_cache;
    terminal_render_cache_count = cell_count;
    terminal_render_cache_columns = columns;
    terminal_render_cache_rows = rows;
    terminal_mark_full_redraw();
    return 0;
}

static void terminal_reset_render_cache(void) {
    free(terminal_render_cache);
    terminal_render_cache = NULL;
    terminal_render_cache_count = 0u;
    terminal_render_cache_columns = 0u;
    terminal_render_cache_rows = 0u;
}

static void terminal_buffer_reset_attributes(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    buffer->current_attr.style = 0u;
    buffer->current_attr.use_default_fg = 1u;
    buffer->current_attr.use_default_bg = 1u;
    buffer->current_attr.fg = buffer->default_fg;
    buffer->current_attr.bg = buffer->default_bg;
}

static void terminal_buffer_initialize_palette(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }

    for (size_t i = 0u; i < 16u; i++) {
        buffer->palette[i] = terminal_default_palette16[i];
    }

    static const uint8_t cube_values[6] = {0u, 95u, 135u, 175u, 215u, 255u};
    size_t index = 16u;
    for (size_t r = 0u; r < 6u; r++) {
        for (size_t g = 0u; g < 6u; g++) {
            for (size_t b = 0u; b < 6u; b++) {
                if (index >= 256u) {
                    break;
                }
                buffer->palette[index++] = terminal_pack_rgb(cube_values[r], cube_values[g], cube_values[b]);
            }
        }
    }

    for (size_t i = 0u; i < 24u && index < 256u; i++) {
        uint8_t value = (uint8_t)(8u + i * 10u);
        buffer->palette[index++] = terminal_pack_rgb(value, value, value);
    }

    buffer->default_fg = buffer->palette[7];
    buffer->default_bg = buffer->palette[0];
    buffer->cursor_color = buffer->palette[7];
    buffer->cursor_visible = 1;
    buffer->saved_cursor_visible = 1;
    terminal_buffer_reset_attributes(buffer);
    buffer->attr_saved = 0;
}

static uint32_t terminal_resolve_fg(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0u;
    }
    if (buffer->current_attr.use_default_fg) {
        return buffer->default_fg;
    }
    return buffer->current_attr.fg;
}

static uint32_t terminal_resolve_bg(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0u;
    }
    if (buffer->current_attr.use_default_bg) {
        return buffer->default_bg;
    }
    return buffer->current_attr.bg;
}

static void terminal_cell_apply_defaults(struct terminal_buffer *buffer, struct terminal_cell *cell) {
    if (!buffer || !cell) {
        return;
    }
    cell->ch = 0u;
    cell->fg = buffer->default_fg;
    cell->bg = buffer->default_bg;
    cell->style = 0u;
}

static void terminal_cell_apply_current(struct terminal_buffer *buffer, struct terminal_cell *cell, uint32_t ch) {
    if (!buffer || !cell) {
        return;
    }
    cell->ch = ch;
    cell->fg = terminal_resolve_fg(buffer);
    cell->bg = terminal_resolve_bg(buffer);
    cell->style = buffer->current_attr.style;
}

static void terminal_cell_apply_current_blank(struct terminal_buffer *buffer, struct terminal_cell *cell) {
    if (!buffer || !cell) {
        return;
    }
    cell->ch = 0u;
    cell->fg = terminal_resolve_fg(buffer);
    cell->bg = terminal_resolve_bg(buffer);
    cell->style = buffer->current_attr.style;
}

static void terminal_set_fg_palette_index(struct terminal_buffer *buffer, int index) {
    if (!buffer) {
        return;
    }
    if (index < 0 || index >= 256) {
        return;
    }
    buffer->current_attr.fg = buffer->palette[(size_t)index];
    buffer->current_attr.use_default_fg = 0u;
}

static void terminal_set_bg_palette_index(struct terminal_buffer *buffer, int index) {
    if (!buffer) {
        return;
    }
    if (index < 0 || index >= 256) {
        return;
    }
    buffer->current_attr.bg = buffer->palette[(size_t)index];
    buffer->current_attr.use_default_bg = 0u;
}

static void terminal_set_fg_rgb(struct terminal_buffer *buffer, uint8_t r, uint8_t g, uint8_t b) {
    if (!buffer) {
        return;
    }
    buffer->current_attr.fg = terminal_pack_rgb(r, g, b);
    buffer->current_attr.use_default_fg = 0u;
}

static void terminal_set_bg_rgb(struct terminal_buffer *buffer, uint8_t r, uint8_t g, uint8_t b) {
    if (!buffer) {
        return;
    }
    buffer->current_attr.bg = terminal_pack_rgb(r, g, b);
    buffer->current_attr.use_default_bg = 0u;
}

static void terminal_update_default_fg(struct terminal_buffer *buffer, uint32_t color) {
    if (!buffer) {
        return;
    }
    uint32_t old_color = buffer->default_fg;
    buffer->default_fg = color;
    if (buffer->current_attr.use_default_fg) {
        buffer->current_attr.fg = color;
    }
    if (buffer->attr_saved && buffer->saved_attr.use_default_fg) {
        buffer->saved_attr.fg = color;
    }
    if (buffer->cells) {
        size_t total = buffer->columns * buffer->rows;
        for (size_t i = 0u; i < total; i++) {
            if (buffer->cells[i].fg == old_color) {
                buffer->cells[i].fg = color;
            }
        }
    }
}

static void terminal_update_default_bg(struct terminal_buffer *buffer, uint32_t color) {
    if (!buffer) {
        return;
    }
    uint32_t old_color = buffer->default_bg;
    buffer->default_bg = color;
    if (buffer->current_attr.use_default_bg) {
        buffer->current_attr.bg = color;
    }
    if (buffer->attr_saved && buffer->saved_attr.use_default_bg) {
        buffer->saved_attr.bg = color;
    }
    if (buffer->cells) {
        size_t total = buffer->columns * buffer->rows;
        for (size_t i = 0u; i < total; i++) {
            if (buffer->cells[i].bg == old_color) {
                buffer->cells[i].bg = color;
            }
        }
    }
    terminal_mark_background_dirty();
}

static void terminal_update_cursor_color(struct terminal_buffer *buffer, uint32_t color) {
    if (!buffer) {
        return;
    }
    buffer->cursor_color = color;
}

static int terminal_parse_hex_color(const char *text, uint32_t *out_color) {
    if (!text || !out_color) {
        return -1;
    }
    if (text[0] != '#') {
        return -1;
    }
    char digits[7];
    for (size_t i = 0u; i < 6u; i++) {
        char c = text[i + 1u];
        if (c == '\0' || !isxdigit((unsigned char)c)) {
            return -1;
        }
        digits[i] = c;
    }
    digits[6] = '\0';
    char *endptr = NULL;
    unsigned long value = strtoul(digits, &endptr, 16);
    if (!endptr || *endptr != '\0' || value > 0xFFFFFFul) {
        return -1;
    }
    uint8_t r = (uint8_t)((value >> 16u) & 0xFFu);
    uint8_t g = (uint8_t)((value >> 8u) & 0xFFu);
    uint8_t b = (uint8_t)(value & 0xFFu);
    *out_color = terminal_pack_rgb(r, g, b);
    return 0;
}

static void terminal_apply_sgr(struct terminal_buffer *buffer, const struct ansi_parser *parser) {
    if (!buffer) {
        return;
    }
    size_t count = parser ? parser->param_count : 0u;
    if (count == 0u) {
        terminal_buffer_reset_attributes(buffer);
        return;
    }

    for (size_t i = 0u; i < count; i++) {
        int value = parser->params[i];
        if (value < 0) {
            value = 0;
        }
        switch (value) {
        case 0:
            terminal_buffer_reset_attributes(buffer);
            break;
        case 1:
            buffer->current_attr.style |= TERMINAL_STYLE_BOLD;
            break;
        case 4:
            buffer->current_attr.style |= TERMINAL_STYLE_UNDERLINE;
            break;
        case 7:
            buffer->current_attr.style |= TERMINAL_STYLE_REVERSE;
            break;
        case 22:
            buffer->current_attr.style &= (uint8_t)~TERMINAL_STYLE_BOLD;
            break;
        case 24:
            buffer->current_attr.style &= (uint8_t)~TERMINAL_STYLE_UNDERLINE;
            break;
        case 27:
            buffer->current_attr.style &= (uint8_t)~TERMINAL_STYLE_REVERSE;
            break;
        case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
            terminal_set_fg_palette_index(buffer, value - 30);
            break;
        case 39:
            buffer->current_attr.use_default_fg = 1u;
            buffer->current_attr.fg = buffer->default_fg;
            break;
        case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
            terminal_set_bg_palette_index(buffer, value - 40);
            break;
        case 49:
            buffer->current_attr.use_default_bg = 1u;
            buffer->current_attr.bg = buffer->default_bg;
            break;
        case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97:
            terminal_set_fg_palette_index(buffer, (value - 90) + 8);
            break;
        case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
            terminal_set_bg_palette_index(buffer, (value - 100) + 8);
            break;
        case 38:
        case 48: {
            int is_foreground = (value == 38) ? 1 : 0;
            if (i + 1u >= count) {
                break;
            }
            int mode = parser->params[++i];
            if (mode == 5 && i + 1u < count) {
                int index = parser->params[++i];
                if (index >= 0 && index < 256) {
                    if (is_foreground) {
                        terminal_set_fg_palette_index(buffer, index);
                    } else {
                        terminal_set_bg_palette_index(buffer, index);
                    }
                }
            } else if (mode == 2 && i + 3u < count) {
                int r = parser->params[++i];
                int g = parser->params[++i];
                int b = parser->params[++i];
                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    if (is_foreground) {
                        terminal_set_fg_rgb(buffer, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    } else {
                        terminal_set_bg_rgb(buffer, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    }
                }
            } else {
                /* Unsupported extended color mode; skip remaining parameters if any */
            }
            break;
        }
        default:
            /* Ignore unsupported SGR codes. */
            break;
        }
    }
}

static void free_font(struct psf_font *font) {
    if (!font) {
        return;
    }
    free(font->glyphs);
    font->glyphs = NULL;
    font->glyph_count = 0;
    font->width = 0;
    font->height = 0;
    font->stride = 0;
    font->glyph_size = 0;
    free(font->unicode_map);
    font->unicode_map = NULL;
    font->unicode_map_count = 0u;
}

static char *terminal_read_text_file(const char *path, size_t *out_size) {
    if (!path) {
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    size_t size = (size_t)file_size;
    char *buffer = malloc(size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, fp);
    fclose(fp);
    if (read_bytes != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

static const char *terminal_skip_utf8_bom(const char *src, size_t *size) {
    if (!src || !size) {
        return src;
    }
    if (*size >= 3u) {
        const unsigned char *bytes = (const unsigned char *)src;
        if (bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) {
            *size -= 3u;
            return src + 3;
        }
    }
    return src;
}

static const char *terminal_skip_leading_space_and_comments(const char *src, const char *end) {
    const char *ptr = src;
    while (ptr < end) {
        while (ptr < end && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '/') {
            ptr += 2;
            while (ptr < end && *ptr != '\n') {
                ptr++;
            }
            continue;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '*') {
            ptr += 2;
            while ((end - ptr) >= 2 && !(ptr[0] == '*' && ptr[1] == '/')) {
                ptr++;
            }
            if ((end - ptr) >= 2) {
                ptr += 2;
            }
            continue;
        }
        break;
    }
    return ptr;
}

static void terminal_free_shader_parameters(struct terminal_shader_parameter *params, size_t count) {
    if (!params) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(params[i].name);
    }
    free(params);
}

static int terminal_parse_shader_parameters(const char *source, size_t length, struct terminal_shader_parameter **out_params, size_t *out_count) {
    if (!out_params || !out_count) {
        return -1;
    }
    *out_params = NULL;
    *out_count = 0u;
    if (!source || length == 0u) {
        return 0;
    }

    struct terminal_shader_parameter *params = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    const char *ptr = source;
    const char *end = source + length;

    while (ptr < end) {
        const char *line_start = ptr;
        const char *line_end = line_start;
        while (line_end < end && line_end[0] != '\n' && line_end[0] != '\r') {
            line_end++;
        }

        const char *cursor = line_start;
        while (cursor < line_end && (*cursor == ' ' || *cursor == '\t')) {
            cursor++;
        }

        if ((size_t)(line_end - cursor) >= 7u && strncmp(cursor, "#pragma", 7) == 0) {
            cursor += 7;
            while (cursor < line_end && isspace((unsigned char)*cursor)) {
                cursor++;
            }

            const char keyword[] = "parameter";
            size_t keyword_len = sizeof(keyword) - 1u;
            if ((size_t)(line_end - cursor) >= keyword_len && strncmp(cursor, keyword, keyword_len) == 0) {
                const char *after_keyword = cursor + keyword_len;
                if (after_keyword < line_end && !isspace((unsigned char)*after_keyword)) {
                    /* Likely parameteri or another pragma, ignore. */
                } else {
                    cursor = after_keyword;
                    while (cursor < line_end && isspace((unsigned char)*cursor)) {
                        cursor++;
                    }

                    const char *name_start = cursor;
                    while (cursor < line_end && (isalnum((unsigned char)*cursor) || *cursor == '_')) {
                        cursor++;
                    }
                    const char *name_end = cursor;
                    if (name_end > name_start) {
                        size_t name_len = (size_t)(name_end - name_start);
                        while (cursor < line_end && isspace((unsigned char)*cursor)) {
                            cursor++;
                        }
                        if (cursor < line_end && *cursor == '"') {
                            cursor++;
                            while (cursor < line_end && *cursor != '"') {
                                cursor++;
                            }
                            if (cursor < line_end && *cursor == '"') {
                                cursor++;
                                while (cursor < line_end && isspace((unsigned char)*cursor)) {
                                    cursor++;
                                }
                                if (cursor < line_end) {
                                    const char *value_start = cursor;
                                    while (cursor < line_end && !isspace((unsigned char)*cursor)) {
                                        cursor++;
                                    }
                                    size_t value_len = (size_t)(cursor - value_start);
                                    if (value_len > 0u) {
                                        char stack_buffer[64];
                                        char *value_str = stack_buffer;
                                        char *heap_buffer = NULL;
                                        if (value_len >= sizeof(stack_buffer)) {
                                            heap_buffer = malloc(value_len + 1u);
                                            if (!heap_buffer) {
                                                terminal_free_shader_parameters(params, count);
                                                return -1;
                                            }
                                            value_str = heap_buffer;
                                        }
                                        memcpy(value_str, value_start, value_len);
                                        value_str[value_len] = '\0';

                                        errno = 0;
                                        char *endptr = NULL;
                                        double parsed = strtod(value_str, &endptr);
                                        if (endptr != value_str && errno != ERANGE) {
                                            char *name_copy = malloc(name_len + 1u);
                                            if (!name_copy) {
                                                free(heap_buffer);
                                                terminal_free_shader_parameters(params, count);
                                                return -1;
                                            }
                                            memcpy(name_copy, name_start, name_len);
                                            name_copy[name_len] = '\0';

                                            if (count == capacity) {
                                                size_t new_capacity = capacity == 0u ? 4u : capacity * 2u;
                                                struct terminal_shader_parameter *new_params = realloc(params, new_capacity * sizeof(*new_params));
                                                if (!new_params) {
                                                    free(name_copy);
                                                    free(heap_buffer);
                                                    terminal_free_shader_parameters(params, count);
                                                    return -1;
                                                }
                                                params = new_params;
                                                capacity = new_capacity;
                                            }

                                            params[count].name = name_copy;
                                            params[count].default_value = (float)parsed;
                                            count++;
                                        }
                                        free(heap_buffer);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ptr = line_end;
        while (ptr < end && (*ptr == '\n' || *ptr == '\r')) {
            ptr++;
        }
    }

    if (count == 0u) {
        free(params);
        params = NULL;
    }

    *out_params = params;
    *out_count = count;
    return 0;
}

static void terminal_shader_reset_uniform_cache(struct terminal_gl_shader *shader) {
    if (!shader) {
        return;
    }
    shader->has_cached_mvp = 0;
    shader->has_cached_output_size = 0;
    shader->has_cached_texture_size = 0;
    shader->has_cached_input_size = 0;
}

static void terminal_shader_set_matrix(GLint location, GLfloat *cache, int *has_cache, const GLfloat *matrix) {
    if (location < 0 || !cache || !has_cache || !matrix) {
        return;
    }
    if (*has_cache && memcmp(cache, matrix, sizeof(GLfloat) * 16u) == 0) {
        return;
    }
    memcpy(cache, matrix, sizeof(GLfloat) * 16u);
    *has_cache = 1;
    glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
}

static void terminal_shader_set_vec2(GLint location, GLfloat *cache, int *has_cache, GLfloat x, GLfloat y) {
    if (location < 0 || !cache || !has_cache) {
        return;
    }
    if (*has_cache && cache[0] == x && cache[1] == y) {
        return;
    }
    cache[0] = x;
    cache[1] = y;
    *has_cache = 1;
    glUniform2f(location, x, y);
}

static void terminal_shader_clear_vaos(struct terminal_gl_shader *shader) {
    if (!shader) {
        return;
    }
    for (size_t i = 0; i < 2; i++) {
        if (shader->quad_vaos[i] != 0) {
            glDeleteVertexArrays(1, &shader->quad_vaos[i]);
            shader->quad_vaos[i] = 0;
        }
    }
    terminal_shader_reset_uniform_cache(shader);
}

static int terminal_initialize_quad_geometry(void) {
    if (terminal_quad_vbo != 0) {
        return 0;
    }
    glGenBuffers(1, &terminal_quad_vbo);
    if (terminal_quad_vbo == 0) {
        return -1;
    }
    glBindBuffer(GL_ARRAY_BUFFER, terminal_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(terminal_quad_vertices), terminal_quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
}

static void terminal_destroy_quad_geometry(void) {
    if (terminal_quad_vbo != 0) {
        glDeleteBuffers(1, &terminal_quad_vbo);
        terminal_quad_vbo = 0;
    }
}

static int terminal_shader_configure_vaos(struct terminal_gl_shader *shader) {
    if (!shader) {
        return -1;
    }
    if (terminal_quad_vbo == 0) {
        return -1;
    }

    GLuint vaos[2] = {0u, 0u};
    glGenVertexArrays(2, vaos);
    if (vaos[0] == 0u || vaos[1] == 0u) {
        if (vaos[0] != 0u) {
            glDeleteVertexArrays(1, &vaos[0]);
        }
        if (vaos[1] != 0u) {
            glDeleteVertexArrays(1, &vaos[1]);
        }
        return -1;
    }

    const GLsizei stride = (GLsizei)sizeof(struct terminal_quad_vertex);
    const void *position_offset = (const void *)offsetof(struct terminal_quad_vertex, position);
    const void *cpu_offset = (const void *)offsetof(struct terminal_quad_vertex, texcoord_cpu);
    const void *fbo_offset = (const void *)offsetof(struct terminal_quad_vertex, texcoord_fbo);

    GLuint *targets[2] = {&vaos[0], &vaos[1]};
    const void *texcoord_offsets[2] = {cpu_offset, fbo_offset};

    for (size_t i = 0; i < 2; i++) {
        glBindVertexArray(*targets[i]);
        glBindBuffer(GL_ARRAY_BUFFER, terminal_quad_vbo);
        if (shader->attrib_vertex >= 0) {
            glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
            glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, stride, position_offset);
        }
        if (shader->attrib_texcoord >= 0) {
            glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
            glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, stride, texcoord_offsets[i]);
        }
        if (shader->attrib_color >= 0) {
            glDisableVertexAttribArray((GLuint)shader->attrib_color);
        }
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader->quad_vaos[0] = vaos[0];
    shader->quad_vaos[1] = vaos[1];
    return 0;
}

static void terminal_bind_texture(GLuint texture) {
    if (terminal_bound_texture != texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        terminal_bound_texture = texture;
    }
}

static float terminal_get_parameter_default(const struct terminal_shader_parameter *params, size_t count, const char *name, float fallback) {
    if (!params || !name) {
        return fallback;
    }
    for (size_t i = 0; i < count; i++) {
        if (params[i].name && strcmp(params[i].name, name) == 0) {
            return params[i].default_value;
        }
    }
    return fallback;
}

static GLuint terminal_compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetShaderInfoLog(shader, log_length, NULL, log);
                fprintf(stderr, "Failed to compile %s shader: %s\n", label ? label : "GL", log);
                free(log);
            }
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static int terminal_initialize_gl_program(const char *shader_path) {
    if (!shader_path) {
        return -1;
    }

    int result = -1;
    size_t shader_size = 0u;
    char *shader_source = NULL;
    char *vertex_source = NULL;
    char *fragment_source = NULL;
    struct terminal_shader_parameter *parameters = NULL;
    size_t parameter_count = 0u;
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;

    shader_source = terminal_read_text_file(shader_path, &shader_size);
    if (!shader_source) {
        fprintf(stderr, "Failed to read shader from %s\n", shader_path);
        goto cleanup;
    }

    const char *version_line = "#version 110\n";
    const char *parameter_define = "#define PARAMETER_UNIFORM 1\n";
    const char *vertex_define = "#define VERTEX 1\n";
    const char *fragment_define = "#define FRAGMENT 1\n";

    size_t parameter_len = strlen(parameter_define);
    size_t vertex_define_len = strlen(vertex_define);
    size_t fragment_define_len = strlen(fragment_define);
    size_t version_line_len = strlen(version_line);

    size_t content_size = shader_size;
    const char *content_start = terminal_skip_utf8_bom(shader_source, &content_size);
    const char *content_end = content_start + content_size;

    if (terminal_parse_shader_parameters(content_start, content_size, &parameters, &parameter_count) != 0) {
        goto cleanup;
    }

    const char *version_start = NULL;
    const char *version_end = NULL;
    const char *scan = terminal_skip_leading_space_and_comments(content_start, content_end);
    if (scan < content_end) {
        size_t remaining = (size_t)(content_end - scan);
        if (remaining >= 8u && strncmp(scan, "#version", 8) == 0) {
            if (remaining == 8u || isspace((unsigned char)scan[8])) {
                version_start = scan;
                version_end = scan;
                while (version_end < content_end && *version_end != '\n') {
                    version_end++;
                }
                if (version_end < content_end) {
                    version_end++;
                }
            }
        }
    }

    const char *version_prefix = version_line;
    size_t version_prefix_len = version_line_len;
    const char *shader_body = content_start;
    size_t shader_body_len = content_size;

    if (version_start && version_end) {
        version_prefix = content_start;
        version_prefix_len = (size_t)(version_end - content_start);
        shader_body = version_end;
        shader_body_len = (size_t)(content_end - version_end);
    }

    size_t newline_len = 0u;
    if (version_prefix_len > 0u) {
        char last_char = version_prefix[version_prefix_len - 1u];
        if (last_char != '\n' && last_char != '\r') {
            newline_len = 1u;
        }
    }

    size_t vertex_length = version_prefix_len + newline_len + parameter_len + vertex_define_len + shader_body_len;
    size_t fragment_length = version_prefix_len + newline_len + parameter_len + fragment_define_len + shader_body_len;

    vertex_source = malloc(vertex_length + 1u);
    fragment_source = malloc(fragment_length + 1u);
    if (!vertex_source || !fragment_source) {
        goto cleanup;
    }

    size_t offset = 0u;
    memcpy(vertex_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    if (newline_len > 0u) {
        vertex_source[offset++] = '\n';
    }
    memcpy(vertex_source + offset, parameter_define, parameter_len);
    offset += parameter_len;
    memcpy(vertex_source + offset, vertex_define, vertex_define_len);
    offset += vertex_define_len;
    memcpy(vertex_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    vertex_source[offset] = '\0';

    offset = 0u;
    memcpy(fragment_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    if (newline_len > 0u) {
        fragment_source[offset++] = '\n';
    }
    memcpy(fragment_source + offset, parameter_define, parameter_len);
    offset += parameter_len;
    memcpy(fragment_source + offset, fragment_define, fragment_define_len);
    offset += fragment_define_len;
    memcpy(fragment_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    fragment_source[offset] = '\0';

    vertex_shader = terminal_compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex");
    fragment_shader = terminal_compile_shader(GL_FRAGMENT_SHADER, fragment_source, "fragment");

    if (vertex_shader == 0 || fragment_shader == 0) {
        goto cleanup;
    }

    program = glCreateProgram();
    if (program == 0) {
        goto cleanup;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    vertex_shader = 0;
    fragment_shader = 0;

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetProgramInfoLog(program, log_length, NULL, log);
                fprintf(stderr, "Failed to link shader program: %s\n", log);
                free(log);
            }
        }
        goto cleanup;
    }

    struct terminal_gl_shader shader_info;
    memset(&shader_info, 0, sizeof(shader_info));
    terminal_shader_reset_uniform_cache(&shader_info);
    int shader_registered = 0;
    shader_info.program = program;
    shader_info.attrib_vertex = glGetAttribLocation(program, "VertexCoord");
    shader_info.attrib_color = glGetAttribLocation(program, "COLOR");
    shader_info.attrib_texcoord = glGetAttribLocation(program, "TexCoord");

    shader_info.uniform_mvp = glGetUniformLocation(program, "MVPMatrix");
    shader_info.uniform_frame_direction = glGetUniformLocation(program, "FrameDirection");
    shader_info.uniform_frame_count = glGetUniformLocation(program, "FrameCount");
    shader_info.uniform_output_size = glGetUniformLocation(program, "OutputSize");
    shader_info.uniform_texture_size = glGetUniformLocation(program, "TextureSize");
    shader_info.uniform_input_size = glGetUniformLocation(program, "InputSize");
    shader_info.uniform_texture_sampler = glGetUniformLocation(program, "Texture");
    shader_info.uniform_prev_sampler = glGetUniformLocation(program, "Prev0");
    shader_info.uniform_crt_gamma = glGetUniformLocation(program, "CRTgamma");
    shader_info.uniform_monitor_gamma = glGetUniformLocation(program, "monitorgamma");
    shader_info.uniform_distance = glGetUniformLocation(program, "d");
    shader_info.uniform_curvature = glGetUniformLocation(program, "CURVATURE");
    shader_info.uniform_radius = glGetUniformLocation(program, "R");
    shader_info.uniform_corner_size = glGetUniformLocation(program, "cornersize");
    shader_info.uniform_corner_smooth = glGetUniformLocation(program, "cornersmooth");
    shader_info.uniform_x_tilt = glGetUniformLocation(program, "x_tilt");
    shader_info.uniform_y_tilt = glGetUniformLocation(program, "y_tilt");
    shader_info.uniform_overscan_x = glGetUniformLocation(program, "overscan_x");
    shader_info.uniform_overscan_y = glGetUniformLocation(program, "overscan_y");
    shader_info.uniform_dotmask = glGetUniformLocation(program, "DOTMASK");
    shader_info.uniform_sharper = glGetUniformLocation(program, "SHARPER");
    shader_info.uniform_scanline_weight = glGetUniformLocation(program, "scanline_weight");
    shader_info.uniform_luminance = glGetUniformLocation(program, "lum");
    shader_info.uniform_interlace_detect = glGetUniformLocation(program, "interlace_detect");
    shader_info.uniform_saturation = glGetUniformLocation(program, "SATURATION");
    shader_info.uniform_inv_gamma = glGetUniformLocation(program, "INV");

    glUseProgram(program);
    if (shader_info.uniform_texture_sampler >= 0) {
        glUniform1i(shader_info.uniform_texture_sampler, 0);
    }
    if (shader_info.uniform_prev_sampler >= 0) {
        glUniform1i(shader_info.uniform_prev_sampler, 1);
    }
    if (shader_info.uniform_frame_direction >= 0) {
        glUniform1i(shader_info.uniform_frame_direction, 1);
    }
    if (shader_info.uniform_mvp >= 0) {
        terminal_shader_set_matrix(shader_info.uniform_mvp,
                                   shader_info.cached_mvp,
                                   &shader_info.has_cached_mvp,
                                   terminal_identity_mvp);
    }

    for (size_t i = 0; i < parameter_count; i++) {
        if (!parameters[i].name) {
            continue;
        }
        GLint location = glGetUniformLocation(program, parameters[i].name);
        if (location >= 0) {
            glUniform1f(location, parameters[i].default_value);
        }
    }

    if (shader_info.uniform_crt_gamma >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "CRTgamma", 2.4f);
        glUniform1f(shader_info.uniform_crt_gamma, value);
    }
    if (shader_info.uniform_monitor_gamma >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "monitorgamma", 2.2f);
        glUniform1f(shader_info.uniform_monitor_gamma, value);
    }
    if (shader_info.uniform_distance >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "d", 1.6f);
        glUniform1f(shader_info.uniform_distance, value);
    }
    if (shader_info.uniform_curvature >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "CURVATURE", 1.0f);
        glUniform1f(shader_info.uniform_curvature, value);
    }
    if (shader_info.uniform_radius >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "R", 2.0f);
        glUniform1f(shader_info.uniform_radius, value);
    }
    if (shader_info.uniform_corner_size >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "cornersize", 0.03f);
        glUniform1f(shader_info.uniform_corner_size, value);
    }
    if (shader_info.uniform_corner_smooth >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "cornersmooth", 1000.0f);
        glUniform1f(shader_info.uniform_corner_smooth, value);
    }
    if (shader_info.uniform_x_tilt >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "x_tilt", 0.0f);
        glUniform1f(shader_info.uniform_x_tilt, value);
    }
    if (shader_info.uniform_y_tilt >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "y_tilt", 0.0f);
        glUniform1f(shader_info.uniform_y_tilt, value);
    }
    if (shader_info.uniform_overscan_x >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "overscan_x", 100.0f);
        glUniform1f(shader_info.uniform_overscan_x, value);
    }
    if (shader_info.uniform_overscan_y >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "overscan_y", 100.0f);
        glUniform1f(shader_info.uniform_overscan_y, value);
    }
    if (shader_info.uniform_dotmask >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "DOTMASK", 0.3f);
        glUniform1f(shader_info.uniform_dotmask, value);
    }
    if (shader_info.uniform_sharper >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "SHARPER", 1.0f);
        glUniform1f(shader_info.uniform_sharper, value);
    }
    if (shader_info.uniform_scanline_weight >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "scanline_weight", 0.3f);
        glUniform1f(shader_info.uniform_scanline_weight, value);
    }
    if (shader_info.uniform_luminance >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "lum", 0.0f);
        glUniform1f(shader_info.uniform_luminance, value);
    }
    if (shader_info.uniform_interlace_detect >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "interlace_detect", 1.0f);
        glUniform1f(shader_info.uniform_interlace_detect, value);
    }
    if (shader_info.uniform_saturation >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "SATURATION", 1.0f);
        glUniform1f(shader_info.uniform_saturation, value);
    }
    if (shader_info.uniform_inv_gamma >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "INV", 1.0f);
        glUniform1f(shader_info.uniform_inv_gamma, value);
    }
    glUseProgram(0);

    if (terminal_shader_configure_vaos(&shader_info) != 0) {
        goto cleanup;
    }

    terminal_free_shader_parameters(parameters, parameter_count);
    parameters = NULL;
    parameter_count = 0u;

    struct terminal_gl_shader *new_array = realloc(terminal_gl_shaders, (terminal_gl_shader_count + 1u) * sizeof(*new_array));
    if (!new_array) {
        goto cleanup;
    }
    terminal_gl_shaders = new_array;
    terminal_gl_shaders[terminal_gl_shader_count] = shader_info;
    terminal_gl_shader_count++;
    shader_registered = 1;
    program = 0;
    result = 0;

cleanup:
    if (!shader_registered) {
        terminal_shader_clear_vaos(&shader_info);
    }
    if (program != 0) {
        glDeleteProgram(program);
    }
    if (fragment_shader != 0) {
        glDeleteShader(fragment_shader);
    }
    if (vertex_shader != 0) {
        glDeleteShader(vertex_shader);
    }
    terminal_free_shader_parameters(parameters, parameter_count);
    free(fragment_source);
    free(vertex_source);
    free(shader_source);
    return result;
}

static int terminal_resize_render_targets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }

    size_t required_size = (size_t)width * (size_t)height * 4u;
    if (required_size > terminal_framebuffer_capacity) {
        uint8_t *new_pixels = realloc(terminal_framebuffer_pixels, required_size);
        if (!new_pixels) {
            return -1;
        }
        terminal_framebuffer_pixels = new_pixels;
        terminal_framebuffer_capacity = required_size;
    }

    terminal_framebuffer_width = width;
    terminal_framebuffer_height = height;
    if (terminal_framebuffer_pixels) {
        memset(terminal_framebuffer_pixels, 0, required_size);
    }

    terminal_mark_background_dirty();
    terminal_mark_full_redraw();

    if (terminal_gl_texture == 0) {
        glGenTextures(1, &terminal_gl_texture);
    }

    if (terminal_gl_texture == 0) {
        return -1;
    }

    terminal_texture_width = width;
    terminal_texture_height = height;

    terminal_bind_texture(terminal_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, terminal_framebuffer_pixels);
    terminal_bind_texture(0);

    return 0;
}

static int terminal_upload_framebuffer(const uint8_t *pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0 || terminal_gl_texture == 0) {
        return -1;
    }

    terminal_bind_texture(terminal_gl_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "glTexSubImage2D failed with error 0x%x\n", error);
        terminal_bind_texture(0);
        return -1;
    }
    terminal_bind_texture(0);
    return 0;
}

static int terminal_prepare_intermediate_targets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }

    if (terminal_gl_framebuffer == 0) {
        glGenFramebuffers(1, &terminal_gl_framebuffer);
    }
    if (terminal_gl_framebuffer == 0) {
        return -1;
    }

    int resized = 0;
    for (size_t i = 0; i < 2; i++) {
        if (terminal_gl_intermediate_textures[i] == 0) {
            glGenTextures(1, &terminal_gl_intermediate_textures[i]);
            if (terminal_gl_intermediate_textures[i] == 0) {
                return -1;
            }
            resized = 1;
        }
    }

    if (width != terminal_intermediate_width || height != terminal_intermediate_height) {
        resized = 1;
    }

    if (resized) {
        for (size_t i = 0; i < 2; i++) {
            terminal_bind_texture(terminal_gl_intermediate_textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        terminal_bind_texture(0);
        terminal_intermediate_width = width;
        terminal_intermediate_height = height;
    }

    return 0;
}

static void terminal_clear_history_texture(GLuint texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return;
    }
    if (terminal_gl_framebuffer == 0) {
        glGenFramebuffers(1, &terminal_gl_framebuffer);
        if (terminal_gl_framebuffer == 0) {
            return;
        }
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, terminal_gl_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

static int terminal_prepare_shader_history(struct terminal_gl_shader *shader, int width, int height, int resized) {
    if (!shader || width <= 0 || height <= 0) {
        return -1;
    }

    int created_history = 0;
    int created_flipped = 0;

    if (shader->history_texture == 0) {
        glGenTextures(1, &shader->history_texture);
        if (shader->history_texture != 0) {
            created_history = 1;
        }
    }
    if (shader->history_texture == 0) {
        return -1;
    }

    if (shader->history_texture_flipped == 0) {
        glGenTextures(1, &shader->history_texture_flipped);
        if (shader->history_texture_flipped == 0) {
            fprintf(stderr, "Failed to create flipped history texture.\n");
        } else {
            created_flipped = 1;
        }
    }

    if (created_history || resized || terminal_history_width == 0 || terminal_history_height == 0) {
        terminal_bind_texture(shader->history_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        terminal_bind_texture(0);
        terminal_clear_history_texture(shader->history_texture, width, height);

        if (shader->history_texture_flipped != 0 &&
            (created_flipped || resized || terminal_history_width == 0 || terminal_history_height == 0)) {
            terminal_bind_texture(shader->history_texture_flipped);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            terminal_bind_texture(0);
            terminal_clear_history_texture(shader->history_texture_flipped, width, height);
        }
    }

    return 0;
}

static void terminal_update_shader_history(struct terminal_gl_shader *shader, int width, int height) {
    if (!shader || shader->history_texture == 0 || width <= 0 || height <= 0) {
        return;
    }

    glActiveTexture(GL_TEXTURE1);
    terminal_bind_texture(shader->history_texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    terminal_bind_texture(0);
    glActiveTexture(GL_TEXTURE0);

    terminal_update_flipped_history_texture(shader->history_texture,
                                            shader->history_texture_flipped,
                                            width,
                                            height);
}

static void terminal_update_flipped_history_texture(GLuint history_texture,
                                                    GLuint history_texture_flipped,
                                                    int width,
                                                    int height) {
    if (history_texture == 0 || history_texture_flipped == 0 || width <= 0 || height <= 0) {
        return;
    }

    if (terminal_gl_framebuffer == 0) {
        glGenFramebuffers(1, &terminal_gl_framebuffer);
        if (terminal_gl_framebuffer == 0) {
            return;
        }
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, terminal_gl_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, history_texture_flipped, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    terminal_bind_texture(history_texture);
    glEnable(GL_TEXTURE_2D);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(1.0f, -1.0f);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, 1.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    terminal_bind_texture(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

static void terminal_clear_shader_history(struct terminal_gl_shader *shader) {
    if (!shader) {
        return;
    }
    if (shader->history_texture != 0) {
        glDeleteTextures(1, &shader->history_texture);
        shader->history_texture = 0;
    }
    if (shader->history_texture_flipped != 0) {
        glDeleteTextures(1, &shader->history_texture_flipped);
        shader->history_texture_flipped = 0;
    }
}

static int terminal_load_cursor_sprite(const char *path) {
    if (!path || !terminal_gl_ready) {
        return -1;
    }

    terminal_destroy_cursor_sprite();

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        fprintf(stderr, "Failed to load cursor sprite from %s: %s\n", path, stbi_failure_reason());
        return -1;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        stbi_image_free(pixels);
        return -1;
    }

    terminal_bind_texture(texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    terminal_bind_texture(0);

    stbi_image_free(pixels);

    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        fprintf(stderr, "Failed to upload cursor sprite texture (0x%x).\n", error);
        return -1;
    }

    terminal_cursor_texture = texture;
    terminal_cursor_width = width;
    terminal_cursor_height = height;
    terminal_cursor_hot_x = width / 2;
    terminal_cursor_hot_y = height / 2;
    terminal_cursor_enabled = 1;
    terminal_cursor_dirty = 1;

    return 0;
}

static void terminal_destroy_cursor_sprite(void) {
    if (terminal_cursor_texture != 0) {
        glDeleteTextures(1, &terminal_cursor_texture);
        terminal_cursor_texture = 0;
    }
    terminal_cursor_width = 0;
    terminal_cursor_height = 0;
    terminal_cursor_hot_x = 0;
    terminal_cursor_hot_y = 0;
    terminal_cursor_enabled = 0;
    terminal_cursor_position_valid = 0;
    terminal_cursor_dirty = 0;
}

static void terminal_set_mouse_cursor_visible(int visible) {
#if BUDOSTACK_HAVE_SDL2
    if (visible) {
        if (terminal_cursor_texture != 0) {
            terminal_cursor_enabled = 1;
            terminal_cursor_dirty = 1;
            SDL_ShowCursor(SDL_DISABLE);
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            terminal_cursor_update_position(mouse_x, mouse_y);
        } else {
            terminal_cursor_enabled = 0;
            terminal_cursor_position_valid = 0;
            terminal_cursor_dirty = 0;
            SDL_ShowCursor(SDL_ENABLE);
        }
    } else {
        terminal_cursor_enabled = 0;
        terminal_cursor_position_valid = 0;
        terminal_cursor_dirty = 1;
        SDL_ShowCursor(SDL_DISABLE);
    }
    terminal_mark_full_redraw();
#else
    (void)visible;
#endif
}

static void terminal_cursor_update_position(int window_x, int window_y) {
    int framebuffer_x = 0;
    int framebuffer_y = 0;
    if (terminal_window_point_to_framebuffer(window_x, window_y, &framebuffer_x, &framebuffer_y) == 0) {
        terminal_mouse_x = framebuffer_x;
        terminal_mouse_y = framebuffer_y;
    }

    if (!terminal_cursor_enabled) {
        return;
    }

    if (terminal_window_point_to_framebuffer(window_x, window_y, &framebuffer_x, &framebuffer_y) == 0) {
        if (!terminal_cursor_position_valid ||
            framebuffer_x != terminal_cursor_x ||
            framebuffer_y != terminal_cursor_y) {
            terminal_cursor_dirty = 1;
        }
        terminal_cursor_x = framebuffer_x;
        terminal_cursor_y = framebuffer_y;
        terminal_cursor_position_valid = 1;
    } else if (terminal_cursor_position_valid) {
        terminal_cursor_position_valid = 0;
        terminal_cursor_dirty = 1;
    }
}

static void terminal_cursor_render(int framebuffer_width, int framebuffer_height, int drawable_width, int drawable_height) {
    if (!terminal_cursor_enabled || terminal_cursor_texture == 0 || !terminal_cursor_position_valid) {
        return;
    }
    if (framebuffer_width <= 0 || framebuffer_height <= 0 || drawable_width <= 0 || drawable_height <= 0) {
        return;
    }

    double scale_x = (framebuffer_width > 0) ? ((double)drawable_width / (double)framebuffer_width) : 1.0;
    double scale_y = (framebuffer_height > 0) ? ((double)drawable_height / (double)framebuffer_height) : 1.0;

    GLfloat left = (GLfloat)(((double)terminal_cursor_x - (double)terminal_cursor_hot_x) * scale_x);
    GLfloat top = (GLfloat)(((double)terminal_cursor_y - (double)terminal_cursor_hot_y) * scale_y);
    GLfloat right = left + (GLfloat)((double)terminal_cursor_width * scale_x);
    GLfloat bottom = top + (GLfloat)((double)terminal_cursor_height * scale_y);

    glUseProgram(0);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)drawable_width, (GLdouble)drawable_height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    terminal_bind_texture(terminal_cursor_texture);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(left, top);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(right, top);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(left, bottom);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(right, bottom);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    terminal_bind_texture(0);
    glDisable(GL_BLEND);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void terminal_clear_gl_shaders(void) {
    if (terminal_gl_shaders) {
        for (size_t i = 0; i < terminal_gl_shader_count; i++) {
            if (terminal_gl_shaders[i].program != 0) {
                glDeleteProgram(terminal_gl_shaders[i].program);
            }
            terminal_clear_shader_history(&terminal_gl_shaders[i]);
            terminal_shader_clear_vaos(&terminal_gl_shaders[i]);
        }
        free(terminal_gl_shaders);
        terminal_gl_shaders = NULL;
    }
    terminal_history_width = 0;
    terminal_history_height = 0;
    terminal_gl_shader_count = 0u;
    terminal_shader_last_frame_tick = SDL_GetTicks();
}

static void terminal_release_gl_resources(void) {
    if (terminal_gl_texture != 0) {
        glDeleteTextures(1, &terminal_gl_texture);
        terminal_gl_texture = 0;
    }
    terminal_destroy_cursor_sprite();
    terminal_clear_gl_shaders();
    if (terminal_gl_intermediate_textures[0] != 0) {
        glDeleteTextures(1, &terminal_gl_intermediate_textures[0]);
        terminal_gl_intermediate_textures[0] = 0;
    }
    if (terminal_gl_intermediate_textures[1] != 0) {
        glDeleteTextures(1, &terminal_gl_intermediate_textures[1]);
        terminal_gl_intermediate_textures[1] = 0;
    }
    if (terminal_gl_framebuffer != 0) {
        glDeleteFramebuffers(1, &terminal_gl_framebuffer);
        terminal_gl_framebuffer = 0;
    }
    terminal_intermediate_width = 0;
    terminal_intermediate_height = 0;
    free(terminal_framebuffer_pixels);
    terminal_framebuffer_pixels = NULL;
    terminal_framebuffer_capacity = 0u;
    terminal_framebuffer_width = 0;
    terminal_framebuffer_height = 0;
    terminal_texture_width = 0;
    terminal_texture_height = 0;
    terminal_bind_texture(0);
    terminal_destroy_quad_geometry();
    terminal_reset_render_cache();
    terminal_custom_pixels_shutdown();
    terminal_mark_full_redraw();
    terminal_mark_background_dirty();
    terminal_gl_ready = 0;
}

static void terminal_free_requested_shaders(void) {
    free(terminal_requested_shaders);
    terminal_requested_shaders = NULL;
    terminal_requested_shader_count = 0u;
}

static int terminal_reload_requested_shaders(void) {
    if (!terminal_shaders_enabled || !terminal_gl_ready) {
        return 0;
    }
    if (terminal_requested_shader_count == 0u) {
        terminal_clear_gl_shaders();
        return 0;
    }

    terminal_clear_gl_shaders();
    for (size_t i = 0; i < terminal_requested_shader_count; i++) {
        if (terminal_initialize_gl_program(terminal_requested_shaders[i].path) != 0) {
            fprintf(stderr, "terminal: Failed to load shader '%s'.\n", terminal_requested_shaders[i].path);
            terminal_clear_gl_shaders();
            return -1;
        }
    }

    terminal_shader_last_frame_tick = SDL_GetTicks();
    return 0;
}

static void terminal_disable_shaders(void) {
    terminal_shaders_enabled = 0;
    terminal_clear_gl_shaders();
}

static int terminal_enable_shaders(void) {
    if (terminal_shaders_enabled) {
        return 0;
    }

    terminal_shaders_enabled = 1;
    if (terminal_reload_requested_shaders() != 0) {
        terminal_shaders_enabled = 0;
        return -1;
    }

    terminal_shader_last_frame_tick = SDL_GetTicks();
    return 0;
}

static int terminal_shaders_active(void) {
    return terminal_shaders_enabled && terminal_gl_shader_count > 0u;
}

static void terminal_print_usage(const char *progname) {
    const char *name = (progname && progname[0] != '\0') ? progname : "terminal";
    fprintf(stderr, "Usage: %s [-s shader_path]...\n", name);
    fprintf(stderr, "  Send OSC 777 'shader=enable|disable' via _TERM_SHADER to toggle shaders at runtime.\n");
    fprintf(stderr, "  Send OSC 777 'cursor_blink=enable|disable' via _TERM_CURSOR_BLINK to toggle cursor blinking.\n");
}

static int psf_unicode_map_compare(const void *a, const void *b) {
    const struct psf_unicode_map *ma = (const struct psf_unicode_map *)a;
    const struct psf_unicode_map *mb = (const struct psf_unicode_map *)b;
    if (ma->codepoint < mb->codepoint) {
        return -1;
    }
    if (ma->codepoint > mb->codepoint) {
        return 1;
    }
    if (ma->glyph_index < mb->glyph_index) {
        return -1;
    }
    if (ma->glyph_index > mb->glyph_index) {
        return 1;
    }
    return 0;
}

static int psf_font_lookup_unicode(const struct psf_font *font, uint32_t codepoint, uint32_t *out_index) {
    if (!font) {
        return 0;
    }
    size_t count = font->unicode_map_count;
    const struct psf_unicode_map *map = font->unicode_map;
    if (count > 0u && map) {
        size_t left = 0u;
        size_t right = count;
        while (left < right) {
            size_t mid = left + (right - left) / 2u;
            uint32_t mid_code = map[mid].codepoint;
            if (mid_code == codepoint) {
                if (out_index) {
                    *out_index = map[mid].glyph_index;
                }
                return 1;
            }
            if (mid_code < codepoint) {
                left = mid + 1u;
            } else {
                right = mid;
            }
        }
        return 0;
    }
    if (codepoint < font->glyph_count) {
        if (out_index) {
            *out_index = codepoint;
        }
        return 1;
    }
    return 0;
}

static uint32_t psf_font_resolve_glyph(const struct psf_font *font, uint32_t codepoint) {
    if (!font) {
        return 0u;
    }
    uint32_t glyph_index = 0u;
    if (psf_font_lookup_unicode(font, codepoint, &glyph_index)) {
        return glyph_index;
    }
    if (psf_font_lookup_unicode(font, '?', &glyph_index)) {
        return glyph_index;
    }
    if ((uint32_t)'?' < font->glyph_count) {
        return (uint32_t)'?';
    }
    return 0u;
}

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static int load_psf_font(const char *path, struct psf_font *out_font, char *errbuf, size_t errbuf_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to open '%s': %s", path, strerror(errno));
        }
        return -1;
    }

    unsigned char header[PSF2_HEADER_SIZE];
    size_t header_read = fread(header, 1, sizeof(header), fp);
    if (header_read < 4) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "File too small to be a PSF font");
        }
        fclose(fp);
        return -1;
    }

    struct psf_font font = {0};

    if (header[0] == PSF1_MAGIC0 && header[1] == PSF1_MAGIC1) {
        if (header_read < 4) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Incomplete PSF1 header");
            }
            fclose(fp);
            return -1;
        }

        uint32_t glyph_count = (header[2] & PSF1_MODE512) ? 512u : 256u;
        uint32_t charsize = header[3];

        font.width = 8u;
        font.height = charsize;
        font.stride = 1u;
        font.glyph_size = font.height * font.stride;
        font.glyph_count = glyph_count;

        if (font.glyph_size == 0 || glyph_count == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid PSF1 font dimensions");
            }
            fclose(fp);
            return -1;
        }

        if ((size_t)glyph_count > SIZE_MAX / font.glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }

        if (fseek(fp, 4L, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }

        size_t total = (size_t)glyph_count * font.glyph_size;
        font.glyphs = calloc(total, 1);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory allocating font");
            }
            fclose(fp);
            return -1;
        }

        if (fread(font.glyphs, 1, total, fp) != total) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to read glyph data");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }

        if ((header[2] & 0x02u) != 0u) {
            size_t map_capacity = 0u;
            size_t map_count = 0u;
            struct psf_unicode_map *map = NULL;
            uint32_t glyph_index = 0u;
            while (glyph_index < glyph_count) {
                unsigned char entry[2];
                if (fread(entry, 1, sizeof(entry), fp) != sizeof(entry)) {
                    if (errbuf && errbuf_size > 0) {
                        snprintf(errbuf, errbuf_size, "Failed to read PSF1 Unicode table");
                    }
                    free(map);
                    free_font(&font);
                    fclose(fp);
                    return -1;
                }
                uint16_t code = (uint16_t)entry[0] | ((uint16_t)entry[1] << 8u);
                if (code == 0xFFFFu) {
                    glyph_index++;
                    continue;
                }
                if (code == 0xFFFEu) {
                    glyph_index++;
                    continue;
                }
                if (map_count == map_capacity) {
                    size_t new_capacity = map_capacity ? map_capacity * 2u : 128u;
                    if (new_capacity > SIZE_MAX / sizeof(*map)) {
                        if (errbuf && errbuf_size > 0) {
                            snprintf(errbuf, errbuf_size, "Unicode map too large");
                        }
                        free(map);
                        free_font(&font);
                        fclose(fp);
                        return -1;
                    }
                    struct psf_unicode_map *new_map = realloc(map, new_capacity * sizeof(*map));
                    if (!new_map) {
                        if (errbuf && errbuf_size > 0) {
                            snprintf(errbuf, errbuf_size, "Out of memory for Unicode map");
                        }
                        free(map);
                        free_font(&font);
                        fclose(fp);
                        return -1;
                    }
                    map = new_map;
                    map_capacity = new_capacity;
                }
                map[map_count].codepoint = (uint32_t)code;
                map[map_count].glyph_index = glyph_index;
                map_count++;
            }
            if (map_count > 1u) {
                qsort(map, map_count, sizeof(*map), psf_unicode_map_compare);
            }
            font.unicode_map = map;
            font.unicode_map_count = map_count;
        }
    } else if (read_u32_le(header) == PSF2_MAGIC) {
        if (header_read < PSF2_HEADER_SIZE) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Incomplete PSF2 header");
            }
            fclose(fp);
            return -1;
        }

        uint32_t version = read_u32_le(header + 4);
        (void)version;
        uint32_t header_size = read_u32_le(header + 8);
        uint32_t flags = read_u32_le(header + 12);
        (void)flags;
        uint32_t glyph_count = read_u32_le(header + 16);
        uint32_t glyph_size = read_u32_le(header + 20);
        uint32_t height = read_u32_le(header + 24);
        uint32_t width = read_u32_le(header + 28);

        if (glyph_count == 0 || glyph_size == 0 || height == 0 || width == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid PSF2 font dimensions");
            }
            fclose(fp);
            return -1;
        }

        if ((size_t)glyph_count > SIZE_MAX / glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }

        if (fseek(fp, (long)header_size, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }

        font.width = width;
        font.height = height;
        font.stride = (width + 7u) / 8u;
        font.glyph_size = glyph_size;
        font.glyph_count = glyph_count;

        font.glyphs = calloc((size_t)glyph_count, glyph_size);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory allocating font");
            }
            fclose(fp);
            return -1;
        }

        size_t glyph_bytes = (size_t)glyph_count * glyph_size;
        if (fread(font.glyphs, 1, glyph_bytes, fp) != glyph_bytes) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to read glyph data");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }

        if ((flags & 0x01u) != 0u) {
            size_t map_capacity = 0u;
            size_t map_count = 0u;
            struct psf_unicode_map *map = NULL;
            uint32_t glyph_index = 0u;
            while (glyph_index < glyph_count) {
                unsigned char entry[4];
                if (fread(entry, 1, sizeof(entry), fp) != sizeof(entry)) {
                    if (errbuf && errbuf_size > 0) {
                        snprintf(errbuf, errbuf_size, "Failed to read PSF2 Unicode table");
                    }
                    free(map);
                    free_font(&font);
                    fclose(fp);
                    return -1;
                }
                uint32_t code = read_u32_le(entry);
                if (code == 0xFFFFFFFFu) {
                    glyph_index++;
                    continue;
                }
                if (code == 0xFFFEu) {
                    glyph_index++;
                    continue;
                }
                if (map_count == map_capacity) {
                    size_t new_capacity = map_capacity ? map_capacity * 2u : 128u;
                    if (new_capacity > SIZE_MAX / sizeof(*map)) {
                        if (errbuf && errbuf_size > 0) {
                            snprintf(errbuf, errbuf_size, "Unicode map too large");
                        }
                        free(map);
                        free_font(&font);
                        fclose(fp);
                        return -1;
                    }
                    struct psf_unicode_map *new_map = realloc(map, new_capacity * sizeof(*map));
                    if (!new_map) {
                        if (errbuf && errbuf_size > 0) {
                            snprintf(errbuf, errbuf_size, "Out of memory for Unicode map");
                        }
                        free(map);
                        free_font(&font);
                        fclose(fp);
                        return -1;
                    }
                    map = new_map;
                    map_capacity = new_capacity;
                }
                map[map_count].codepoint = code;
                map[map_count].glyph_index = glyph_index;
                map_count++;
            }
            if (map_count > 1u) {
                qsort(map, map_count, sizeof(*map), psf_unicode_map_compare);
            }
            font.unicode_map = map;
            font.unicode_map_count = map_count;
        }
    } else {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Unsupported font format");
        }
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_font = font;
    return 0;
}

static int terminal_buffer_init(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    buffer->columns = columns;
    buffer->rows = rows;
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
    buffer->saved_cursor_column = 0u;
    buffer->saved_cursor_row = 0u;
    buffer->scroll_top = 0u;
    buffer->scroll_bottom = rows > 0u ? rows - 1u : 0u;
    buffer->cursor_saved = 0;
    buffer->attr_saved = 0;
    buffer->cursor_visible = 1;
    buffer->bracketed_paste_enabled = 0; // Default: OFF
    buffer->saved_cursor_visible = 1;
    buffer->app_keypad = 0;
    buffer->app_cursor = 0;
    buffer->mouse_tracking = 0;
    buffer->mouse_drag_tracking = 0;
    buffer->mouse_motion_tracking = 0;
    buffer->mouse_sgr = 0;
    buffer->history_limit = TERMINAL_HISTORY_LIMIT;
    buffer->history_rows = 0u;
    buffer->history_start = 0u;
    buffer->scroll_offset = 0u;
    buffer->last_emitted = 0u;
    buffer->last_emitted_valid = 0;

    if (columns == 0u || rows == 0u) {
        buffer->cells = NULL;
        buffer->history = NULL;
        return -1;
    }

    if (columns > SIZE_MAX / rows) {
        buffer->cells = NULL;
        buffer->history = NULL;
        return -1;
    }

    size_t total_cells = columns * rows;
    buffer->cells = calloc(total_cells, sizeof(struct terminal_cell));
    if (!buffer->cells) {
        buffer->history = NULL;
        return -1;
    }
    for (size_t i = 0u; i < total_cells; i++) {
        terminal_cell_apply_defaults(buffer, &buffer->cells[i]);
    }

    if (buffer->history_limit > 0u) {
        if (columns > SIZE_MAX / buffer->history_limit) {
            free(buffer->cells);
            buffer->cells = NULL;
            buffer->history = NULL;
            return -1;
        }
        size_t history_cells = buffer->history_limit * columns;
        buffer->history = calloc(history_cells, sizeof(struct terminal_cell));
        if (!buffer->history) {
            free(buffer->cells);
            buffer->cells = NULL;
            return -1;
        }
        for (size_t i = 0u; i < history_cells; i++) {
            terminal_cell_apply_defaults(buffer, &buffer->history[i]);
        }
    } else {
        buffer->history = NULL;
    }

    terminal_buffer_reset_attributes(buffer);
    return 0;
}

static int terminal_buffer_resize_single(struct terminal_buffer *buffer, size_t new_columns, size_t new_rows) {
    if (!buffer || new_columns == 0u || new_rows == 0u) {
        return -1;
    }

    if (buffer->columns == new_columns && buffer->rows == new_rows) {
        return 0;
    }

    if (new_columns > SIZE_MAX / new_rows) {
        return -1;
    }

    size_t old_columns = buffer->columns;
    size_t old_rows = buffer->rows;
    struct terminal_cell *old_cells = buffer->cells;

    size_t total_cells = new_columns * new_rows;
    struct terminal_cell *new_cells = calloc(total_cells, sizeof(struct terminal_cell));
    if (!new_cells) {
        return -1;
    }

    for (size_t i = 0u; i < total_cells; i++) {
        terminal_cell_apply_defaults(buffer, &new_cells[i]);
    }

    size_t copy_rows = old_rows < new_rows ? old_rows : new_rows;
    size_t copy_cols = old_columns < new_columns ? old_columns : new_columns;

    if (copy_rows > 0u && copy_cols > 0u && old_cells) {
        for (size_t row = 0u; row < copy_rows; row++) {
            struct terminal_cell *dst = new_cells + row * new_columns;
            struct terminal_cell *src = old_cells + row * old_columns;
            memcpy(dst, src, copy_cols * sizeof(struct terminal_cell));
        }
    }

    struct terminal_cell *new_history = NULL;
    if (buffer->history_limit > 0u) {
        if (new_columns > SIZE_MAX / buffer->history_limit) {
            free(new_cells);
            return -1;
        }
        size_t history_cells = buffer->history_limit * new_columns;
        new_history = calloc(history_cells, sizeof(struct terminal_cell));
        if (!new_history) {
            free(new_cells);
            return -1;
        }
        for (size_t i = 0u; i < history_cells; i++) {
            terminal_cell_apply_defaults(buffer, &new_history[i]);
        }
    }

    free(buffer->cells);
    free(buffer->history);
    buffer->cells = new_cells;
    buffer->history = new_history;
    buffer->columns = new_columns;
    buffer->rows = new_rows;

    if (buffer->cursor_column >= new_columns) {
        buffer->cursor_column = new_columns - 1u;
    }
    if (buffer->cursor_row >= new_rows) {
        buffer->cursor_row = new_rows - 1u;
    }
    if (buffer->cursor_saved) {
        if (buffer->saved_cursor_column >= new_columns) {
            buffer->saved_cursor_column = new_columns - 1u;
        }
        if (buffer->saved_cursor_row >= new_rows) {
            buffer->saved_cursor_row = new_rows - 1u;
        }
    }

    buffer->history_rows = 0u;
    buffer->history_start = 0u;
    buffer->scroll_offset = 0u;
    buffer->scroll_top = 0u;
    buffer->scroll_bottom = new_rows > 0u ? new_rows - 1u : 0u;

    return 0;
}

static int terminal_buffer_resize(struct terminal_buffer *buffer, size_t new_columns, size_t new_rows) {
    if (!buffer || new_columns == 0u || new_rows == 0u) {
        return -1;
    }

    if (terminal_buffer_resize_single(buffer, new_columns, new_rows) != 0) {
        return -1;
    }

    if (terminal_alternate_buffer_handle && terminal_alternate_initialized &&
        terminal_alternate_buffer_handle != buffer) {
        if (terminal_buffer_resize_single(terminal_alternate_buffer_handle, new_columns, new_rows) != 0) {
            return -1;
        }
    }

    return 0;
}

static void terminal_buffer_free(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->cells);
    buffer->cells = NULL;
    free(buffer->history);
    buffer->history = NULL;
    buffer->columns = 0u;
    buffer->rows = 0u;
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
    buffer->saved_cursor_column = 0u;
    buffer->saved_cursor_row = 0u;
    buffer->scroll_top = 0u;
    buffer->scroll_bottom = 0u;
    buffer->cursor_saved = 0;
    buffer->cursor_visible = 1;
    buffer->saved_cursor_visible = 1;
    buffer->history_rows = 0u;
    buffer->history_start = 0u;
    buffer->scroll_offset = 0u;
}

static int terminal_prepare_alternate_buffer(const struct terminal_buffer *source) {
    if (!terminal_alternate_buffer_handle || !source) {
        return -1;
    }
    if (terminal_alternate_initialized) {
        return 0;
    }

    terminal_buffer_initialize_palette(terminal_alternate_buffer_handle);
    if (terminal_buffer_init(terminal_alternate_buffer_handle, source->columns, source->rows) != 0) {
        terminal_buffer_free(terminal_alternate_buffer_handle);
        return -1;
    }
    terminal_alternate_buffer_handle->history_limit = source->history_limit;
    terminal_alternate_initialized = 1;
    return 0;
}

static void terminal_swap_alternate_buffer(struct terminal_buffer *buffer) {
    if (!buffer || !terminal_alternate_buffer_handle) {
        return;
    }
    if (!terminal_alternate_initialized) {
        if (terminal_prepare_alternate_buffer(buffer) != 0) {
            return;
        }
    }

    int mouse_tracking = buffer->mouse_tracking;
    int mouse_drag_tracking = buffer->mouse_drag_tracking;
    int mouse_motion_tracking = buffer->mouse_motion_tracking;
    int mouse_sgr = buffer->mouse_sgr;

    struct terminal_buffer temp = *buffer;
    *buffer = *terminal_alternate_buffer_handle;
    *terminal_alternate_buffer_handle = temp;

    buffer->mouse_tracking = mouse_tracking;
    buffer->mouse_drag_tracking = mouse_drag_tracking;
    buffer->mouse_motion_tracking = mouse_motion_tracking;
    buffer->mouse_sgr = mouse_sgr;

    terminal_using_alternate = !terminal_using_alternate;
    terminal_mark_full_redraw();
    terminal_mark_background_dirty();
}

static void terminal_buffer_clamp_scroll(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    if (buffer->scroll_offset > buffer->history_rows) {
        buffer->scroll_offset = buffer->history_rows;
    }
}

static void terminal_buffer_resolve_scroll_region(const struct terminal_buffer *buffer,
                                                  size_t *out_top,
                                                  size_t *out_bottom) {
    size_t top = 0u;
    size_t bottom = 0u;
    if (buffer && buffer->rows > 0u) {
        top = buffer->scroll_top;
        bottom = buffer->scroll_bottom;
        if (top >= buffer->rows) {
            top = 0u;
        }
        if (bottom >= buffer->rows) {
            bottom = buffer->rows - 1u;
        }
        if (top > bottom) {
            top = 0u;
            bottom = buffer->rows - 1u;
        }
    }
    if (out_top) {
        *out_top = top;
    }
    if (out_bottom) {
        *out_bottom = bottom;
    }
}

static void terminal_buffer_push_history(struct terminal_buffer *buffer, const struct terminal_cell *row) {
    if (!buffer || !row || buffer->columns == 0u) {
        return;
    }
    if (buffer->history_limit == 0u || !buffer->history) {
        return;
    }

    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    size_t target_index;
    if (buffer->history_rows < buffer->history_limit) {
        target_index = (buffer->history_start + buffer->history_rows) % buffer->history_limit;
        buffer->history_rows++;
    } else {
        target_index = buffer->history_start;
        buffer->history_start = (buffer->history_start + 1u) % buffer->history_limit;
    }

    struct terminal_cell *dest = buffer->history + target_index * buffer->columns;
    memcpy(dest, row, row_size);
    terminal_buffer_clamp_scroll(buffer);
}

static void terminal_buffer_scroll(struct terminal_buffer *buffer) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    struct terminal_cell *first_row = buffer->cells;
    terminal_buffer_push_history(buffer, first_row);
    memmove(buffer->cells, buffer->cells + buffer->columns, row_size * (buffer->rows - 1u));
    struct terminal_cell *last_row = buffer->cells + buffer->columns * (buffer->rows - 1u);
    for (size_t col = 0u; col < buffer->columns; col++) {
        terminal_cell_apply_defaults(buffer, &last_row[col]);
    }
    if (buffer->scroll_offset > 0u) {
        buffer->scroll_offset++;
        terminal_buffer_clamp_scroll(buffer);
    }
}

static void terminal_buffer_scroll_region_up(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }

    size_t top = 0u;
    size_t bottom = 0u;
    terminal_buffer_resolve_scroll_region(buffer, &top, &bottom);
    if (top == 0u && bottom == buffer->rows - 1u) {
        for (size_t i = 0u; i < count; i++) {
            terminal_buffer_scroll(buffer);
        }
        return;
    }
    if (bottom <= top) {
        return;
    }

    size_t region_rows = bottom - top + 1u;
    if (count >= region_rows) {
        for (size_t row = top; row <= bottom; row++) {
            terminal_buffer_fill_line_current(buffer, row);
        }
        return;
    }

    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    memmove(buffer->cells + top * buffer->columns,
            buffer->cells + (top + count) * buffer->columns,
            row_size * (region_rows - count));

    for (size_t row = bottom + 1u - count; row <= bottom; row++) {
        terminal_buffer_fill_line_current(buffer, row);
    }
}

static void terminal_buffer_scroll_region_down(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }

    size_t top = 0u;
    size_t bottom = 0u;
    terminal_buffer_resolve_scroll_region(buffer, &top, &bottom);
    if (top == 0u && bottom == buffer->rows - 1u) {
        size_t row_size = buffer->columns * sizeof(struct terminal_cell);
        for (size_t i = 0u; i < count; i++) {
            memmove(buffer->cells + buffer->columns,
                    buffer->cells,
                    row_size * (buffer->rows - 1u));
            terminal_buffer_fill_line_current(buffer, 0u);
        }
        return;
    }
    if (bottom <= top) {
        return;
    }

    size_t region_rows = bottom - top + 1u;
    if (count >= region_rows) {
        for (size_t row = top; row <= bottom; row++) {
            terminal_buffer_fill_line_current(buffer, row);
        }
        return;
    }

    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    memmove(buffer->cells + (top + count) * buffer->columns,
            buffer->cells + top * buffer->columns,
            row_size * (region_rows - count));

    for (size_t row = top; row < top + count; row++) {
        terminal_buffer_fill_line_current(buffer, row);
    }
}

static void terminal_buffer_index(struct terminal_buffer *buffer) {
    if (!buffer || buffer->rows == 0u) {
        return;
    }

    if (buffer->cursor_row >= buffer->rows) {
        buffer->cursor_row = buffer->rows - 1u;
    }

    size_t top = 0u;
    size_t bottom = 0u;
    terminal_buffer_resolve_scroll_region(buffer, &top, &bottom);
        if (buffer->cursor_row >= top && buffer->cursor_row <= bottom) {
            if (buffer->cursor_row == bottom) {
                terminal_buffer_scroll_region_up(buffer, 1u);
            } else {
                buffer->cursor_row++;
            }
    } else if (buffer->cursor_row + 1u < buffer->rows) {
        buffer->cursor_row++;
    }
}

static void terminal_buffer_reverse_index(struct terminal_buffer *buffer) {
    if (!buffer || buffer->rows == 0u) {
        return;
    }

    if (buffer->cursor_row >= buffer->rows) {
        buffer->cursor_row = buffer->rows - 1u;
    }

    size_t top = 0u;
    size_t bottom = 0u;
    terminal_buffer_resolve_scroll_region(buffer, &top, &bottom);
        if (buffer->cursor_row >= top && buffer->cursor_row <= bottom) {
            if (buffer->cursor_row == top) {
                terminal_buffer_scroll_region_down(buffer, 1u);
            } else {
                buffer->cursor_row--;
            }
    } else if (buffer->cursor_row > 0u) {
        buffer->cursor_row--;
    }
}

static const struct terminal_cell *terminal_buffer_row_at(const struct terminal_buffer *buffer, size_t index) {
    if (!buffer) {
        return NULL;
    }
    if (index < buffer->history_rows) {
        if (buffer->history_limit == 0u || !buffer->history) {
            return NULL;
        }
        size_t ring_index = (buffer->history_start + index) % buffer->history_limit;
        return buffer->history + ring_index * buffer->columns;
    }
    index -= buffer->history_rows;
    if (index >= buffer->rows || !buffer->cells) {
        return NULL;
    }
    return buffer->cells + index * buffer->columns;
}

static void terminal_buffer_set_cursor(struct terminal_buffer *buffer, size_t column, size_t row) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (column >= buffer->columns) {
        column = buffer->columns - 1u;
    }
    if (row >= buffer->rows) {
        row = buffer->rows - 1u;
    }
    buffer->cursor_column = column;
    buffer->cursor_row = row;
}

static void terminal_buffer_move_relative(struct terminal_buffer *buffer, int column_delta, int row_delta) {
    if (!buffer) {
        return;
    }
    int new_column = (int)buffer->cursor_column + column_delta;
    int new_row = (int)buffer->cursor_row + row_delta;
    if (new_column < 0) {
        new_column = 0;
    }
    if (new_row < 0) {
        new_row = 0;
    }
    if (buffer->columns > 0u && (size_t)new_column >= buffer->columns) {
        new_column = (int)buffer->columns - 1;
    }
    if (buffer->rows > 0u && (size_t)new_row >= buffer->rows) {
        new_row = (int)buffer->rows - 1;
    }
    buffer->cursor_column = (size_t)new_column;
    buffer->cursor_row = (size_t)new_row;
}

static void terminal_buffer_clear_line_segment(struct terminal_buffer *buffer,
                                               size_t row,
                                               size_t start_column,
                                               size_t end_column) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (row >= buffer->rows || buffer->columns == 0u) {
        return;
    }
    if (start_column >= buffer->columns) {
        return;
    }
    if (end_column > buffer->columns) {
        end_column = buffer->columns;
    }
    struct terminal_cell *line = buffer->cells + row * buffer->columns;
    for (size_t col = start_column; col < end_column; col++) {
        terminal_cell_apply_current_blank(buffer, &line[col]);
    }
}

static void terminal_buffer_clear_entire_line(struct terminal_buffer *buffer, size_t row) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (row >= buffer->rows) {
        return;
    }
    struct terminal_cell *line = buffer->cells + row * buffer->columns;
    for (size_t col = 0u; col < buffer->columns; col++) {
        terminal_cell_apply_current_blank(buffer, &line[col]);
    }
}

static void terminal_buffer_clear_to_end_of_display(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    terminal_buffer_clear_line_segment(buffer,
                                       buffer->cursor_row,
                                       buffer->cursor_column,
                                       buffer->columns);
    for (size_t row = buffer->cursor_row + 1u; row < buffer->rows; row++) {
        terminal_buffer_clear_entire_line(buffer, row);
    }
}

static void terminal_buffer_clear_from_start_of_display(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    for (size_t row = 0u; row < buffer->cursor_row; row++) {
        terminal_buffer_clear_entire_line(buffer, row);
    }
    terminal_buffer_clear_line_segment(buffer, buffer->cursor_row, 0u, buffer->cursor_column + 1u);
}

static void terminal_buffer_clear_display(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    size_t total = buffer->columns * buffer->rows;
    for (size_t i = 0u; i < total; i++) {
        terminal_cell_apply_current_blank(buffer, &buffer->cells[i]);
    }
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
}

static void terminal_buffer_clear_line_from_cursor(struct terminal_buffer *buffer) {
    terminal_buffer_clear_line_segment(buffer,
                                       buffer->cursor_row,
                                       buffer->cursor_column,
                                       buffer->columns);
}

static void terminal_buffer_clear_line_to_cursor(struct terminal_buffer *buffer) {
    terminal_buffer_clear_line_segment(buffer, buffer->cursor_row, 0u, buffer->cursor_column + 1u);
}

static void terminal_buffer_clear_line(struct terminal_buffer *buffer) {
    terminal_buffer_clear_entire_line(buffer, buffer->cursor_row);
}

static void terminal_buffer_erase_chars(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (buffer->cursor_row >= buffer->rows) {
        return;
    }
    if (buffer->cursor_column >= buffer->columns) {
        return;
    }
    if (count == 0u) {
        return;
    }
    size_t end_column = buffer->cursor_column + count;
    if (end_column > buffer->columns) {
        end_column = buffer->columns;
    }
    terminal_buffer_clear_line_segment(buffer,
                                       buffer->cursor_row,
                                       buffer->cursor_column,
                                       end_column);
}

static void terminal_buffer_fill_line_current(struct terminal_buffer *buffer, size_t row) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (row >= buffer->rows || buffer->columns == 0u) {
        return;
    }
    struct terminal_cell *line = buffer->cells + row * buffer->columns;
    for (size_t col = 0u; col < buffer->columns; col++) {
        terminal_cell_apply_current_blank(buffer, &line[col]);
    }
}

static void terminal_buffer_fill_line_segment_current(struct terminal_buffer *buffer,
                                                      size_t row,
                                                      size_t start_column,
                                                      size_t end_column) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (row >= buffer->rows || buffer->columns == 0u) {
        return;
    }
    if (start_column >= buffer->columns) {
        return;
    }
    if (end_column > buffer->columns) {
        end_column = buffer->columns;
    }
    struct terminal_cell *line = buffer->cells + row * buffer->columns;
    for (size_t col = start_column; col < end_column; col++) {
        terminal_cell_apply_current_blank(buffer, &line[col]);
    }
}

static void terminal_buffer_insert_chars(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (buffer->cursor_row >= buffer->rows || buffer->cursor_column >= buffer->columns) {
        return;
    }
    if (count == 0u) {
        return;
    }
    size_t available = buffer->columns - buffer->cursor_column;
    if (count > available) {
        count = available;
    }
    size_t tail_count = available - count;
    struct terminal_cell *line = buffer->cells + buffer->cursor_row * buffer->columns;
    if (tail_count > 0u) {
        memmove(line + buffer->cursor_column + count,
                line + buffer->cursor_column,
                tail_count * sizeof(struct terminal_cell));
    }
    terminal_buffer_fill_line_segment_current(buffer,
                                              buffer->cursor_row,
                                              buffer->cursor_column,
                                              buffer->cursor_column + count);
}

static void terminal_buffer_delete_chars(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (buffer->cursor_row >= buffer->rows || buffer->cursor_column >= buffer->columns) {
        return;
    }
    if (count == 0u) {
        return;
    }
    size_t available = buffer->columns - buffer->cursor_column;
    if (count > available) {
        count = available;
    }
    size_t tail_count = available - count;
    struct terminal_cell *line = buffer->cells + buffer->cursor_row * buffer->columns;
    if (tail_count > 0u) {
        memmove(line + buffer->cursor_column,
                line + buffer->cursor_column + count,
                tail_count * sizeof(struct terminal_cell));
    }
    terminal_buffer_fill_line_segment_current(buffer,
                                              buffer->cursor_row,
                                              buffer->columns - count,
                                              buffer->columns);
}

static void terminal_buffer_insert_lines(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (buffer->cursor_row >= buffer->rows) {
        return;
    }
    if (count == 0u) {
        return;
    }

    size_t top = 0u;
    size_t bottom = 0u;
    terminal_buffer_resolve_scroll_region(buffer, &top, &bottom);
    if (buffer->cursor_row < top || buffer->cursor_row > bottom) {
        return;
    }
    size_t available = bottom - buffer->cursor_row + 1u;
    if (count > available) {
        count = available;
    }
    size_t rows_to_move = available - count;
    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    if (rows_to_move > 0u) {
        memmove(buffer->cells + (buffer->cursor_row + count) * buffer->columns,
                buffer->cells + buffer->cursor_row * buffer->columns,
                rows_to_move * row_size);
    }
    for (size_t row = buffer->cursor_row; row < buffer->cursor_row + count; row++) {
        terminal_buffer_fill_line_current(buffer, row);
    }
}

static void terminal_buffer_delete_lines(struct terminal_buffer *buffer, size_t count) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (buffer->cursor_row >= buffer->rows) {
        return;
    }
    if (count == 0u) {
        return;
    }

    size_t top = 0u;
    size_t bottom = 0u;
    terminal_buffer_resolve_scroll_region(buffer, &top, &bottom);
    if (buffer->cursor_row < top || buffer->cursor_row > bottom) {
        return;
    }
    size_t available = bottom - buffer->cursor_row + 1u;
    if (count > available) {
        count = available;
    }
    size_t rows_to_move = available - count;
    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    if (rows_to_move > 0u) {
        memmove(buffer->cells + buffer->cursor_row * buffer->columns,
                buffer->cells + (buffer->cursor_row + count) * buffer->columns,
                rows_to_move * row_size);
    }
    for (size_t row = bottom + 1u - count; row <= bottom; row++) {
        terminal_buffer_fill_line_current(buffer, row);
    }
}

static void terminal_buffer_save_cursor(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    buffer->saved_cursor_column = buffer->cursor_column;
    buffer->saved_cursor_row = buffer->cursor_row;
    buffer->cursor_saved = 1;
    buffer->saved_cursor_visible = buffer->cursor_visible;
    buffer->saved_attr = buffer->current_attr;
    buffer->attr_saved = 1;
}

static void terminal_buffer_restore_cursor(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cursor_saved) {
        return;
    }
    terminal_buffer_set_cursor(buffer, buffer->saved_cursor_column, buffer->saved_cursor_row);
    buffer->cursor_visible = buffer->saved_cursor_visible;
    if (buffer->attr_saved) {
        buffer->current_attr = buffer->saved_attr;
    }
}

static void terminal_put_char(struct terminal_buffer *buffer, uint32_t ch) {
    if (!buffer || !buffer->cells) {
        return;
    }

    switch (ch) {
    case '\r':
        buffer->cursor_column = 0u;
        return;
    case '\n':
        buffer->cursor_column = 0u;
        terminal_buffer_index(buffer);
        return;
    case '\t': {
        size_t next_tab = ((buffer->cursor_column / 8u) + 1u) * 8u;
        size_t spaces = 0u;
        if (next_tab >= buffer->columns) {
            spaces = buffer->columns > buffer->cursor_column ? buffer->columns - buffer->cursor_column : 0u;
        } else {
            spaces = next_tab - buffer->cursor_column;
        }
        if (spaces == 0u) {
            spaces = 1u;
        }
        for (size_t i = 0u; i < spaces; i++) {
            terminal_put_char(buffer, ' ');
        }
        return;
    }
//    case '\b':
//        if (buffer->cursor_column > 0u) {
//            buffer->cursor_column--;
//        } else if (buffer->cursor_row > 0u) {
//            buffer->cursor_row--;
//            buffer->cursor_column = buffer->columns ? buffer->columns - 1u : 0u;
//        }
//        if (buffer->cursor_row < buffer->rows && buffer->cursor_column < buffer->columns) {
//            terminal_cell_apply_defaults(buffer,
//                                         &buffer->cells[buffer->cursor_row * buffer->columns + buffer->cursor_column]);
//        }
//        return;
//  New Implementation: Fixes the left arrow delete
    case '\b':
        /* Backspace: move cursor one cell left, wrapping to previous row.
         *
         * FIX: Do NOT clear the character here.
         * - Our line editor uses "\b \b" (backspace, space, backspace)
         *   when it actually wants to delete a character.
         * - Left arrow now sends plain '\b' to move the cursor left
         *   without erasing text.
         *
         * If we cleared the cell here, every cursor move left would
         * visually delete characters, which is what we are seeing now.
         */
        if (buffer->cursor_column > 0u) {
            buffer->cursor_column--;
        } else if (buffer->cursor_row > 0u) {
            buffer->cursor_row--;
            buffer->cursor_column = buffer->columns ? buffer->columns - 1u : 0u;
        }
        return;
    default:
        if (ch < 32u && ch != '\t') {
            return;
        }
        if (buffer->cursor_row >= buffer->rows) {
            terminal_buffer_index(buffer);
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        if (buffer->cursor_column >= buffer->columns) {
            buffer->cursor_column = 0u;
            terminal_buffer_index(buffer);
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        if (buffer->cursor_column >= buffer->columns) {
            buffer->cursor_column = 0u;
            terminal_buffer_index(buffer);
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        struct terminal_cell *cell = &buffer->cells[buffer->cursor_row * buffer->columns + buffer->cursor_column];
        terminal_cell_apply_current(buffer, cell, ch);
        buffer->last_emitted = ch;
        buffer->last_emitted_valid = 1;
        buffer->cursor_column++;
        return;
    }

    if (buffer->cursor_row >= buffer->rows) {
        terminal_buffer_index(buffer);
    }
}

static void ansi_parser_reset_parameters(struct ansi_parser *parser) {
    if (!parser) {
        return;
    }
    parser->param_count = 0u;
    parser->collecting_param = 0;
    parser->private_marker = 0;
    for (size_t i = 0u; i < ANSI_MAX_PARAMS; i++) {
        parser->params[i] = -1;
    }
}

static void ansi_parser_reset_utf8(struct ansi_parser *parser) {
    if (!parser) {
        return;
    }
    parser->utf8_codepoint = 0u;
    parser->utf8_min_value = 0u;
    parser->utf8_bytes_expected = 0u;
    parser->utf8_bytes_seen = 0u;
}

static void ansi_parser_emit_codepoint(struct ansi_parser *parser, struct terminal_buffer *buffer, uint32_t codepoint) {
    if (!buffer) {
        return;
    }
    uint32_t mapped = terminal_map_charset(parser, codepoint);
    terminal_put_char(buffer, mapped);
}

static void ansi_parser_emit_replacement(struct ansi_parser *parser, struct terminal_buffer *buffer) {
    ansi_parser_emit_codepoint(parser, buffer, 0xFFFDu);
}

static void ansi_parser_feed_utf8(struct ansi_parser *parser, struct terminal_buffer *buffer, unsigned char byte) {
    if (!parser) {
        return;
    }

    while (1) {
        if (parser->utf8_bytes_expected > 0u) {
            if ((byte & 0xC0u) == 0x80u) {
                parser->utf8_codepoint = (parser->utf8_codepoint << 6u) | (uint32_t)(byte & 0x3Fu);
                parser->utf8_bytes_seen++;
                if (parser->utf8_bytes_seen == parser->utf8_bytes_expected) {
                    uint32_t codepoint = parser->utf8_codepoint;
                    uint32_t min_value = parser->utf8_min_value;
                    ansi_parser_reset_utf8(parser);
                    if (codepoint < min_value || codepoint > 0x10FFFFu ||
                        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
                        ansi_parser_emit_replacement(parser, buffer);
                    } else {
                        ansi_parser_emit_codepoint(parser, buffer, codepoint);
                    }
                }
                return;
            }

            ansi_parser_emit_replacement(parser, buffer);
            ansi_parser_reset_utf8(parser);
            continue;
        }

        if (byte == 0x1b) {
            ansi_parser_reset_utf8(parser);
            parser->state = ANSI_STATE_ESCAPE;
            return;
        }

        if ((byte & 0x80u) == 0u) {
            ansi_parser_emit_codepoint(parser, buffer, (uint32_t)byte);
            return;
        }

        if ((byte & 0xC0u) == 0x80u) {
            ansi_parser_emit_replacement(parser, buffer);
            return;
        }

        if ((byte & 0xE0u) == 0xC0u) {
            if (byte < 0xC2u) {
                ansi_parser_emit_replacement(parser, buffer);
                return;
            }
            parser->utf8_bytes_expected = 2u;
            parser->utf8_bytes_seen = 1u;
            parser->utf8_codepoint = (uint32_t)(byte & 0x1Fu);
            parser->utf8_min_value = 0x80u;
            return;
        }

        if ((byte & 0xF0u) == 0xE0u) {
            parser->utf8_bytes_expected = 3u;
            parser->utf8_bytes_seen = 1u;
            parser->utf8_codepoint = (uint32_t)(byte & 0x0Fu);
            parser->utf8_min_value = 0x800u;
            return;
        }

        if ((byte & 0xF8u) == 0xF0u) {
            if (byte > 0xF4u) {
                ansi_parser_emit_replacement(parser, buffer);
                return;
            }
            parser->utf8_bytes_expected = 4u;
            parser->utf8_bytes_seen = 1u;
            parser->utf8_codepoint = (uint32_t)(byte & 0x07u);
            parser->utf8_min_value = 0x10000u;
            return;
        }

        ansi_parser_emit_replacement(parser, buffer);
        return;
    }
}

static void ansi_parser_init(struct ansi_parser *parser) {
    if (!parser) {
        return;
    }
    parser->state = ANSI_STATE_GROUND;
    ansi_parser_reset_parameters(parser);
    parser->charset_g0 = 'B';
    parser->charset_g1 = 'B';
    parser->charset_target = 0;
    parser->charset_use_g1 = 0;
    parser->osc_length = 0u;
    if (sizeof(parser->osc_buffer) > 0u) {
        parser->osc_buffer[0] = '\0';
    }
    ansi_parser_reset_utf8(parser);
}

static int terminal_base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    if (ch == '=') {
        return 64;
    }
    return -1;
}

static int terminal_base64_decode(const char *input, uint8_t **out_data, size_t *out_size) {
    if (!input || !out_data || !out_size) {
        return -1;
    }

    size_t len = strlen(input);
    if (len == 0u || (len % 4u) != 0u) {
        return -1;
    }

    size_t max_output = (len / 4u) * 3u;
    const size_t decode_limit = 16u * 1024u * 1024u;
    if (max_output > decode_limit) {
        return -1;
    }

    uint8_t *decoded = malloc(max_output);
    if (!decoded) {
        return -1;
    }

    size_t out_idx = 0u;
    for (size_t i = 0u; i < len; i += 4u) {
        int v0 = terminal_base64_value(input[i]);
        int v1 = terminal_base64_value(input[i + 1u]);
        int v2 = terminal_base64_value(input[i + 2u]);
        int v3 = terminal_base64_value(input[i + 3u]);
        if (v0 < 0 || v1 < 0 || v0 == 64 || v1 == 64) {
            free(decoded);
            return -1;
        }

        if (v2 == 64) {
            if (v3 != 64 || i + 4u != len) {
                free(decoded);
                return -1;
            }
            decoded[out_idx++] = (uint8_t)(((uint32_t)v0 << 2) | ((uint32_t)v1 >> 4));
            break;
        }

        if (v3 == 64) {
            if (i + 4u != len) {
                free(decoded);
                return -1;
            }
            decoded[out_idx++] = (uint8_t)(((uint32_t)v0 << 2) | ((uint32_t)v1 >> 4));
            decoded[out_idx++] = (uint8_t)((((uint32_t)v1 & 0x0Fu) << 4) | ((uint32_t)v2 >> 2));
            break;
        }

        if (v2 < 0 || v3 < 0) {
            free(decoded);
            return -1;
        }

        decoded[out_idx++] = (uint8_t)(((uint32_t)v0 << 2) | ((uint32_t)v1 >> 4));
        decoded[out_idx++] = (uint8_t)((((uint32_t)v1 & 0x0Fu) << 4) | ((uint32_t)v2 >> 2));
        decoded[out_idx++] = (uint8_t)((((uint32_t)v2 & 0x03u) << 6) | (uint32_t)v3);
    }

    *out_data = decoded;
    *out_size = out_idx;
    return 0;
}

static int terminal_utf8_next(const uint8_t *data, size_t length, size_t *offset, uint32_t *out_codepoint) {
    if (!data || !offset || !out_codepoint || *offset >= length) {
        return -1;
    }

    uint8_t first = data[*offset];
    (*offset)++;

    if ((first & 0x80u) == 0u) {
        *out_codepoint = (uint32_t)first;
        return 0;
    }

    uint32_t codepoint = 0u;
    size_t expected = 0u;
    uint32_t min_value = 0u;

    if ((first & 0xE0u) == 0xC0u) {
        codepoint = (uint32_t)(first & 0x1Fu);
        expected = 1u;
        min_value = 0x80u;
    } else if ((first & 0xF0u) == 0xE0u) {
        codepoint = (uint32_t)(first & 0x0Fu);
        expected = 2u;
        min_value = 0x800u;
    } else if ((first & 0xF8u) == 0xF0u) {
        codepoint = (uint32_t)(first & 0x07u);
        expected = 3u;
        min_value = 0x10000u;
    } else {
        *out_codepoint = '?';
        return 0;
    }

    if (*offset + expected > length) {
        *out_codepoint = '?';
        *offset = length;
        return 0;
    }

    for (size_t i = 0u; i < expected; i++) {
        uint8_t byte = data[*offset];
        (*offset)++;
        if ((byte & 0xC0u) != 0x80u) {
            *out_codepoint = '?';
            return 0;
        }
        codepoint = (codepoint << 6u) | (uint32_t)(byte & 0x3Fu);
    }

    if (codepoint < min_value || codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
        *out_codepoint = '?';
        return 0;
    }

    *out_codepoint = codepoint;
    return 0;
}

static int terminal_render_text_sprite(const struct psf_font *font,
                                       const uint8_t *text,
                                       size_t length,
                                       uint32_t color,
                                       uint8_t **out_pixels,
                                       int *out_width,
                                       int *out_height) {
    if (!font || !text || length == 0u || !out_pixels || !out_width || !out_height) {
        return -1;
    }
    if (!font->glyphs || font->glyph_size == 0u || font->width == 0u || font->height == 0u) {
        return -1;
    }

    int glyph_scale = TERMINAL_FONT_SCALE;
    if (glyph_scale <= 0) {
        glyph_scale = 1;
    }

    size_t glyph_width_size = (size_t)font->width * (size_t)glyph_scale;
    size_t glyph_height_size = (size_t)font->height * (size_t)glyph_scale;
    if (glyph_width_size == 0u || glyph_height_size == 0u ||
        glyph_width_size > (size_t)INT_MAX || glyph_height_size > (size_t)INT_MAX) {
        return -1;
    }

    size_t offset = 0u;
    size_t glyph_count = 0u;
    while (offset < length) {
        uint32_t cp = 0u;
        if (terminal_utf8_next(text, length, &offset, &cp) != 0) {
            return -1;
        }
        glyph_count++;
    }

    if (glyph_count == 0u || glyph_count > SIZE_MAX / glyph_width_size) {
        return -1;
    }

    size_t text_width_size = glyph_count * glyph_width_size;
    size_t text_height_size = glyph_height_size;
    if (text_width_size == 0u || text_height_size == 0u ||
        text_width_size > (size_t)INT_MAX || text_height_size > (size_t)INT_MAX) {
        return -1;
    }

    if (text_width_size > SIZE_MAX / text_height_size) {
        return -1;
    }

    size_t pixel_count = text_width_size * text_height_size;
    if (pixel_count == 0u || pixel_count > SIZE_MAX / 4u) {
        return -1;
    }

    uint8_t *pixels = calloc(pixel_count, 4u);
    if (!pixels) {
        return -1;
    }

    uint8_t r = terminal_color_r(color);
    uint8_t g = terminal_color_g(color);
    uint8_t b = terminal_color_b(color);

    offset = 0u;
    size_t glyph_index = 0u;
    while (offset < length) {
        uint32_t cp = 0u;
        if (terminal_utf8_next(text, length, &offset, &cp) != 0) {
            free(pixels);
            return -1;
        }

        uint32_t glyph_id = psf_font_resolve_glyph(font, cp);
        if (glyph_id >= font->glyph_count) {
            glyph_id = 0u;
        }
        const uint8_t *glyph_bitmap = font->glyphs + glyph_id * font->glyph_size;

        size_t dest_x = glyph_index * glyph_width_size;
        for (size_t py = 0u; py < text_height_size; py++) {
            size_t src_y = py / (size_t)glyph_scale;
            if (src_y >= font->height) {
                break;
            }
            const uint8_t *glyph_row = glyph_bitmap + src_y * font->stride;
            for (uint32_t src_x = 0u; src_x < font->width; src_x++) {
                uint8_t mask = (uint8_t)(0x80u >> (src_x & 7u));
                if ((glyph_row[src_x / 8u] & mask) == 0u) {
                    continue;
                }
                size_t start_px = (size_t)src_x * (size_t)glyph_scale;
                size_t end_px = start_px + (size_t)glyph_scale;
                if (end_px > glyph_width_size) {
                    end_px = glyph_width_size;
                }
                for (size_t px = start_px; px < end_px; px++) {
                    size_t dst_index = ((py * text_width_size) + dest_x + px) * 4u;
                    pixels[dst_index + 0u] = r;
                    pixels[dst_index + 1u] = g;
                    pixels[dst_index + 2u] = b;
                    pixels[dst_index + 3u] = 255u;
                }
            }
        }

        glyph_index++;
    }

    *out_pixels = pixels;
    *out_width = (int)text_width_size;
    *out_height = (int)text_height_size;
    return 0;
}

static int terminal_scheme_color_for_index(const struct terminal_buffer *buffer, long color_index, uint32_t *out_color) {
    if (!buffer || !out_color) {
        return -1;
    }

    uint32_t color = 0u;
    if (color_index >= 1 && color_index <= 16) {
        color = buffer->palette[(size_t)(color_index - 1)];
    } else if (color_index == 17) {
        color = buffer->default_fg;
    } else if (color_index == 18) {
        color = buffer->default_bg;
    } else {
        return -1;
    }

    *out_color = color;
    return 0;
}

static void terminal_handle_osc_777(struct terminal_buffer *buffer, const char *args) {
    if (!buffer) {
        return;
    }

    int scale = 0;
    int margin = -1;
    int resolution_width = -1;
    int resolution_height = -1;
    int resolution_width_set = 0;
    int resolution_height_set = 0;
    int resolution_requested = 0;
    int mouse_query_requested = 0;
    int mouse_visibility_requested = 0;
    int mouse_visibility_show = 0;
    int shader_toggle_requested = 0;
    int shader_enable_requested = 0;
    int cursor_blink_toggle_requested = 0;
    int cursor_blink_enable_requested = 1;

    if (args && args[0] != '\0') {
        char *copy = strdup(args);
        if (copy) {
            char *saveptr = NULL;
            char *token = strtok_r(copy, ";", &saveptr);
#if BUDOSTACK_HAVE_SDL2
            char *sound_action = NULL;
            char *sound_path = NULL;
            int sound_channel = -1;
            float sound_volume = 1.0f;
            int sound_volume_set = 0;
#endif
            enum terminal_pixel_action {
                TERMINAL_PIXEL_ACTION_NONE = 0,
                TERMINAL_PIXEL_ACTION_DRAW = 1,
                TERMINAL_PIXEL_ACTION_CLEAR = 2,
                TERMINAL_PIXEL_ACTION_RENDER = 3
            };
            enum terminal_pixel_action pixel_action = TERMINAL_PIXEL_ACTION_NONE;
            enum terminal_sprite_action {
                TERMINAL_SPRITE_ACTION_NONE = 0,
                TERMINAL_SPRITE_ACTION_DRAW = 1,
                TERMINAL_SPRITE_ACTION_CLEAR = 2
            };
            enum terminal_sprite_action sprite_action = TERMINAL_SPRITE_ACTION_NONE;
            enum terminal_text_action {
                TERMINAL_TEXT_ACTION_NONE = 0,
                TERMINAL_TEXT_ACTION_DRAW = 1
            };
            enum terminal_text_action text_action = TERMINAL_TEXT_ACTION_NONE;
            long pixel_x = -1;
            long pixel_y = -1;
            long pixel_r = -1;
            long pixel_g = -1;
            long pixel_b = -1;
            long pixel_layer = 0;
            long sprite_x = -1;
            long sprite_y = -1;
            long sprite_w = -1;
            long sprite_h = -1;
            long sprite_layer = 1;
            long text_x = -1;
            long text_y = -1;
            long text_layer = 1;
            long text_color = -1;
            char *sprite_data_value = NULL;
            char *text_data_value = NULL;
            uint8_t *sprite_pixels = NULL;
            size_t sprite_bytes = 0u;
            uint8_t *text_bytes = NULL;
            size_t text_length = 0u;
            while (token) {
                char *value = strchr(token, '=');
                char *key = token;
                if (value) {
                    *value = '\0';
                    value++;
                }
                if (key && key[0] != '\0') {
                    if (strcmp(key, "scale") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed > 0 && parsed <= INT_MAX) {
                            scale = (int)parsed;
                        }
                    } else if (strcmp(key, "margin") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 0 && parsed <= INT_MAX) {
                            margin = (int)parsed;
                        }
                    } else if (strcmp(key, "resolution") == 0 && value && *value != '\0') {
                        char *sep = strchr(value, 'x');
                        if (!sep) {
                            sep = strchr(value, 'X');
                        }
                        if (sep) {
                            *sep = '\0';
                            const char *height_str = sep + 1;
                            char *endptr = NULL;
                            errno = 0;
                            long parsed_width = strtol(value, &endptr, 10);
                            if (errno == 0 && endptr && *endptr == '\0' && parsed_width >= 0 && parsed_width <= INT_MAX) {
                                resolution_width = (int)parsed_width;
                                resolution_width_set = 1;
                            }
                            errno = 0;
                            endptr = NULL;
                            long parsed_height = strtol(height_str, &endptr, 10);
                            if (errno == 0 && endptr && *endptr == '\0' && parsed_height >= 0 && parsed_height <= INT_MAX) {
                                resolution_height = (int)parsed_height;
                                resolution_height_set = 1;
                            }
                            if (resolution_width_set && resolution_height_set) {
                                resolution_requested = 1;
                            }
                        }
                    } else if (strcmp(key, "resolution_width") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 0 && parsed <= INT_MAX) {
                            resolution_width = (int)parsed;
                            resolution_width_set = 1;
                            resolution_requested = 1;
                        }
                    } else if (strcmp(key, "resolution_height") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 0 && parsed <= INT_MAX) {
                            resolution_height = (int)parsed;
                            resolution_height_set = 1;
                            resolution_requested = 1;
                        }
                    } else if (strcmp(key, "shader") == 0 && value && *value != '\0') {
                        if (strcmp(value, "enable") == 0) {
                            shader_toggle_requested = 1;
                            shader_enable_requested = 1;
                        } else if (strcmp(value, "disable") == 0) {
                            shader_toggle_requested = 1;
                            shader_enable_requested = 0;
                        }
                    } else if (strcmp(key, "cursor_blink") == 0 && value && *value != '\0') {
                        if (strcmp(value, "enable") == 0) {
                            cursor_blink_toggle_requested = 1;
                            cursor_blink_enable_requested = 1;
                        } else if (strcmp(value, "disable") == 0) {
                            cursor_blink_toggle_requested = 1;
                            cursor_blink_enable_requested = 0;
                        }
#if BUDOSTACK_HAVE_SDL2
                    } else if (strcmp(key, "sound") == 0 && value && *value != '\0') {
                        sound_action = value;
                    } else if (strcmp(key, "channel") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 1 && parsed <= TERMINAL_SOUND_CHANNEL_COUNT) {
                            sound_channel = (int)(parsed - 1);
                        }
                    } else if (strcmp(key, "volume") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 0 && parsed <= 100) {
                            sound_volume = (float)parsed / 100.0f;
                            sound_volume_set = 1;
                        }
                    } else if (strcmp(key, "path") == 0 && value) {
                        sound_path = value;
#endif
                    } else if (strcmp(key, "pixel") == 0 && value && *value != '\0') {
                        if (strcmp(value, "draw") == 0 || strcmp(value, "set") == 0) {
                            pixel_action = TERMINAL_PIXEL_ACTION_DRAW;
                        } else if (strcmp(value, "clear") == 0) {
                            pixel_action = TERMINAL_PIXEL_ACTION_CLEAR;
                        } else if (strcmp(value, "render") == 0) {
                            pixel_action = TERMINAL_PIXEL_ACTION_RENDER;
                        }
                    } else if (strcmp(key, "pixel_x") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            pixel_x = parsed;
                        }
                    } else if (strcmp(key, "pixel_y") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            pixel_y = parsed;
                        }
                    } else if (strcmp(key, "pixel_r") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            pixel_r = parsed;
                        }
                    } else if (strcmp(key, "pixel_g") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            pixel_g = parsed;
                        }
                    } else if (strcmp(key, "pixel_b") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            pixel_b = parsed;
                        }
                    } else if (strcmp(key, "pixel_layer") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 1 && parsed <= 16) {
                            pixel_layer = parsed;
                        }
                    } else if (strcmp(key, "sprite") == 0 && value && *value != '\0') {
                        if (strcmp(value, "draw") == 0) {
                            sprite_action = TERMINAL_SPRITE_ACTION_DRAW;
                        } else if (strcmp(value, "clear") == 0) {
                            sprite_action = TERMINAL_SPRITE_ACTION_CLEAR;
                        }
                    } else if (strcmp(key, "sprite_x") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            sprite_x = parsed;
                        }
                    } else if (strcmp(key, "sprite_y") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            sprite_y = parsed;
                        }
                    } else if (strcmp(key, "sprite_w") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            sprite_w = parsed;
                        }
                    } else if (strcmp(key, "sprite_h") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            sprite_h = parsed;
                        }
                    } else if (strcmp(key, "sprite_layer") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 1 && parsed <= 16) {
                            sprite_layer = parsed;
                        }
                    } else if (strcmp(key, "sprite_data") == 0 && value) {
                        sprite_data_value = value;
                    } else if (strcmp(key, "text") == 0 && value && *value != '\0') {
                        if (strcmp(value, "draw") == 0) {
                            text_action = TERMINAL_TEXT_ACTION_DRAW;
                        }
                    } else if (strcmp(key, "text_x") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            text_x = parsed;
                        }
                    } else if (strcmp(key, "text_y") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0') {
                            text_y = parsed;
                        }
                    } else if (strcmp(key, "text_layer") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 1 && parsed <= 16) {
                            text_layer = parsed;
                        }
                    } else if (strcmp(key, "text_color") == 0 && value && *value != '\0') {
                        char *endptr = NULL;
                        errno = 0;
                        long parsed = strtol(value, &endptr, 10);
                        if (errno == 0 && endptr && *endptr == '\0' && parsed >= 1 && parsed <= 18) {
                            text_color = parsed;
                        }
                    } else if (strcmp(key, "text_data") == 0 && value) {
                        text_data_value = value;
                    } else if (strcmp(key, "mouse") == 0 && value && *value != '\0') {
                        if (strcmp(value, "query") == 0) {
                            mouse_query_requested = 1;
                        } else if (strcmp(value, "show") == 0) {
                            mouse_visibility_requested = 1;
                            mouse_visibility_show = 1;
                        } else if (strcmp(value, "hide") == 0) {
                            mouse_visibility_requested = 1;
                            mouse_visibility_show = 0;
                        }
                    }
                }
                token = strtok_r(NULL, ";", &saveptr);
            }

#if BUDOSTACK_HAVE_SDL2
            if (sound_action) {
                if (strcmp(sound_action, "play") == 0) {
                    if (sound_channel >= 0 && sound_path && sound_path[0] != '\0') {
                        float play_volume = sound_volume_set ? sound_volume : 1.0f;
                        if (terminal_sound_play(sound_channel, sound_path, play_volume) != 0) {
                            fprintf(stderr, "terminal: Failed to play sound on channel %d.\n", sound_channel + 1);
                        }
                    } else {
                        fprintf(stderr, "terminal: Sound play requires a valid channel and path.\n");
                    }
                } else if (strcmp(sound_action, "stop") == 0) {
                    if (sound_channel >= 0) {
                        terminal_sound_stop(sound_channel);
                    } else {
                        fprintf(stderr, "terminal: Sound stop requires a valid channel.\n");
                    }
                }
            }
#endif

            if (sprite_action == TERMINAL_SPRITE_ACTION_DRAW) {
                if (sprite_x < 0 || sprite_y < 0 || sprite_w <= 0 || sprite_h <= 0 ||
                    sprite_x > INT_MAX || sprite_y > INT_MAX || sprite_w > INT_MAX || sprite_h > INT_MAX) {
                    fprintf(stderr, "terminal: Invalid sprite parameters.\n");
                } else if (!sprite_data_value) {
                    fprintf(stderr, "terminal: Missing sprite data.\n");
                } else if (terminal_base64_decode(sprite_data_value, &sprite_pixels, &sprite_bytes) != 0) {
                    fprintf(stderr, "terminal: Failed to decode sprite data.\n");
                } else {
                    size_t width_sz = (size_t)sprite_w;
                    size_t height_sz = (size_t)sprite_h;
                    if (width_sz != 0u && height_sz > SIZE_MAX / width_sz) {
                        fprintf(stderr, "terminal: Sprite dimensions overflow.\n");
                    } else {
                        size_t expected_pixels = width_sz * height_sz;
                        if (expected_pixels > SIZE_MAX / 4u) {
                            fprintf(stderr, "terminal: Sprite dimensions too large.\n");
                        } else {
                            size_t expected_bytes = expected_pixels * 4u;
                            if (expected_bytes != sprite_bytes) {
                                fprintf(stderr, "terminal: Sprite data size mismatch.\n");
                            } else if (terminal_custom_pixels_draw_sprite((int)sprite_x,
                                                                           (int)sprite_y,
                                                                           sprite_pixels,
                                                                           (int)sprite_w,
                                                                           (int)sprite_h,
                                                                           (uint8_t)sprite_layer) != 0) {
                                fprintf(stderr, "terminal: Failed to draw sprite.\n");
                            }
                        }
                    }
                }
            } else if (sprite_action == TERMINAL_SPRITE_ACTION_CLEAR) {
                if (sprite_x < 0 || sprite_y < 0 || sprite_w <= 0 || sprite_h <= 0 ||
                    sprite_x > INT_MAX || sprite_y > INT_MAX || sprite_w > INT_MAX || sprite_h > INT_MAX) {
                    fprintf(stderr, "terminal: Invalid sprite clear parameters.\n");
                } else {
                    if (terminal_custom_pixels_clear_rect((int)sprite_x,
                                                         (int)sprite_y,
                                                         (int)sprite_w,
                                                         (int)sprite_h,
                                                         (uint8_t)sprite_layer) != 0) {
                        fprintf(stderr, "terminal: Failed to queue sprite clear.\n");
                    }
                }
            }

            free(sprite_pixels);

            if (text_action == TERMINAL_TEXT_ACTION_DRAW) {
                if (text_x < 0 || text_y < 0 || text_x > INT_MAX || text_y > INT_MAX) {
                    fprintf(stderr, "terminal: Invalid text coordinates.\n");
                } else if (text_layer < 1 || text_layer > 16) {
                    fprintf(stderr, "terminal: Text layer must be between 1 and 16.\n");
                } else if (text_color < 1 || text_color > 18) {
                    fprintf(stderr, "terminal: Text color must be between 1 and 18.\n");
                } else if (!text_data_value) {
                    fprintf(stderr, "terminal: Missing text data.\n");
                } else if (!terminal_font.glyphs) {
                    fprintf(stderr, "terminal: Font is not available for text rendering.\n");
                } else if (terminal_base64_decode(text_data_value, &text_bytes, &text_length) != 0 || text_length == 0u) {
                    fprintf(stderr, "terminal: Failed to decode text data.\n");
                } else {
                    uint32_t resolved_color = 0u;
                    if (terminal_scheme_color_for_index(buffer, text_color, &resolved_color) != 0) {
                        fprintf(stderr, "terminal: Invalid text color index.\n");
                    } else {
                        uint8_t *text_pixels = NULL;
                        int text_w = 0;
                        int text_h = 0;
                        if (terminal_render_text_sprite(&terminal_font,
                                                        text_bytes,
                                                        text_length,
                                                        resolved_color,
                                                        &text_pixels,
                                                        &text_w,
                                                        &text_h) != 0) {
                            fprintf(stderr, "terminal: Failed to render text sprite.\n");
                        } else {
                            if (terminal_custom_pixels_draw_sprite((int)text_x,
                                                                   (int)text_y,
                                                                   text_pixels,
                                                                   text_w,
                                                                   text_h,
                                                                   (uint8_t)text_layer) != 0) {
                                fprintf(stderr, "terminal: Failed to draw text.\n");
                            }
                        }
                        free(text_pixels);
                    }
                }
            }

            free(text_bytes);

            if (mouse_query_requested) {
#if BUDOSTACK_HAVE_SDL2
                int mouse_x = 0;
                int mouse_y = 0;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                terminal_cursor_update_position(mouse_x, mouse_y);
#endif
                char response[128];
                int written = snprintf(response,
                                       sizeof(response),
                                       "_TERM_MOUSE %d %d %u %u\n",
                                       terminal_mouse_x,
                                       terminal_mouse_y,
                                       terminal_mouse_left_clicks,
                                       terminal_mouse_right_clicks);
                if (written > 0 && (size_t)written < sizeof(response)) {
                    terminal_send_response(response);
                }
                terminal_mouse_left_clicks = 0u;
                terminal_mouse_right_clicks = 0u;
            }

            if (mouse_visibility_requested) {
                terminal_set_mouse_cursor_visible(mouse_visibility_show);
            }

            if (pixel_action == TERMINAL_PIXEL_ACTION_DRAW) {
                if (pixel_x >= 0 && pixel_y >= 0 && pixel_x <= INT_MAX && pixel_y <= INT_MAX &&
                    pixel_r >= 0 && pixel_r <= 255 && pixel_g >= 0 && pixel_g <= 255 && pixel_b >= 0 && pixel_b <= 255) {
                    if (terminal_custom_pixels_set((int)pixel_x,
                                                   (int)pixel_y,
                                                   (uint8_t)pixel_r,
                                                   (uint8_t)pixel_g,
                                                   (uint8_t)pixel_b,
                                                   1u) != 0) {
                        fprintf(stderr, "terminal: Failed to draw custom pixel.\n");
                    }
                } else {
                    fprintf(stderr, "terminal: Invalid pixel draw parameters.\n");
                }
            } else if (pixel_action == TERMINAL_PIXEL_ACTION_CLEAR) {
                terminal_custom_pixels_clear();
            } else if (pixel_action == TERMINAL_PIXEL_ACTION_RENDER) {
                int modified = 0;
                if (pixel_layer == 0) {
                    modified = terminal_custom_pixels_apply_pending_clears(0u);
                    if (terminal_custom_pixels_pending_layers != 0u) {
                        terminal_custom_pixels_pending_layers = 0u;
                        terminal_custom_pixels_active = (terminal_custom_pixel_count > 0u);
                        terminal_custom_pixels_dirty = 1;
                    } else if (modified) {
                        terminal_custom_pixels_active = (terminal_custom_pixel_count > 0u);
                        terminal_custom_pixels_dirty = 1;
                    }
                } else if (pixel_layer >= 1 && pixel_layer <= 16) {
                    modified = terminal_custom_pixels_apply_pending_clears((uint8_t)pixel_layer);
                    uint16_t layer_mask = terminal_custom_layer_mask((uint8_t)pixel_layer);
                    if ((terminal_custom_pixels_pending_layers & layer_mask) != 0u) {
                        terminal_custom_pixels_pending_layers &= (uint16_t)(~layer_mask);
                        terminal_custom_pixels_active = (terminal_custom_pixel_count > 0u);
                        terminal_custom_pixels_dirty = 1;
                    } else if (modified) {
                        terminal_custom_pixels_active = (terminal_custom_pixel_count > 0u);
                        terminal_custom_pixels_dirty = 1;
                    }
                }
            }

            if (shader_toggle_requested) {
                if (shader_enable_requested) {
                    if (terminal_enable_shaders() != 0) {
                        fprintf(stderr, "terminal: Failed to enable shaders; remaining disabled.\n");
                    }
                } else {
                    terminal_disable_shaders();
                }
                terminal_mark_full_redraw();
            }

            if (cursor_blink_toggle_requested) {
                terminal_cursor_blink_enabled = cursor_blink_enable_requested;
                terminal_cursor_blink_reset_requested = 1;
                terminal_mark_full_redraw();
            }

            free(copy);
        }

        if (scale == 0) {
            char *endptr = NULL;
            errno = 0;
            long parsed = strtol(args, &endptr, 10);
            if (errno == 0 && endptr && *endptr == '\0' && parsed > 0 && parsed <= INT_MAX) {
                scale = (int)parsed;
            }
        }
    }

    if (scale > 0) {
        terminal_apply_scale(buffer, scale);
    }
    if (margin >= 0) {
        terminal_apply_margin(buffer, margin);
    }
    if (resolution_requested && resolution_width_set && resolution_height_set) {
        terminal_apply_resolution(buffer, resolution_width, resolution_height);
    }
}

static void ansi_handle_osc(struct ansi_parser *parser, struct terminal_buffer *buffer) {
    if (!parser || !buffer) {
        return;
    }
    if (parser->osc_length >= sizeof(parser->osc_buffer)) {
        parser->osc_length = sizeof(parser->osc_buffer) - 1u;
    }
    parser->osc_buffer[parser->osc_length] = '\0';

    char *data = parser->osc_buffer;
    char *args = strchr(data, ';');
    if (args) {
        *args = '\0';
        args++;
    }

    int command = atoi(data);
    switch (command) {
    case 4: { /* Set palette colors */
        char *cursor = args;
        while (cursor && *cursor != '\0') {
            char *end_index = NULL;
            long index = strtol(cursor, &end_index, 10);
            if (end_index == cursor) {
                break;
            }
            if (*end_index != ';') {
                break;
            }
            cursor = end_index + 1;
            if (*cursor == '\0') {
                break;
            }
            char *end_color = strchr(cursor, ';');
            size_t len = end_color ? (size_t)(end_color - cursor) : strlen(cursor);
            if (len >= sizeof(parser->osc_buffer)) {
                len = sizeof(parser->osc_buffer) - 1u;
            }
            char color_spec[32];
            if (len >= sizeof(color_spec)) {
                len = sizeof(color_spec) - 1u;
            }
            memcpy(color_spec, cursor, len);
            color_spec[len] = '\0';
            uint32_t color_value = 0u;
            if (index >= 0 && index < 256 && terminal_parse_hex_color(color_spec, &color_value) == 0) {
                size_t palette_index = (size_t)index;
                uint32_t old_color = buffer->palette[palette_index];
                buffer->palette[palette_index] = color_value;
                if (buffer->cells) {
                    size_t total = buffer->columns * buffer->rows;
                    for (size_t cell_index = 0u; cell_index < total; cell_index++) {
                        if (buffer->cells[cell_index].fg == old_color) {
                            buffer->cells[cell_index].fg = color_value;
                        }
                        if (buffer->cells[cell_index].bg == old_color) {
                            buffer->cells[cell_index].bg = color_value;
                        }
                    }
                }
                if (buffer->default_fg == old_color) {
                    terminal_update_default_fg(buffer, color_value);
                }
                if (buffer->default_bg == old_color) {
                    terminal_update_default_bg(buffer, color_value);
                }
                if (buffer->cursor_color == old_color) {
                    terminal_update_cursor_color(buffer, color_value);
                }
            }
            if (!end_color) {
                break;
            }
            cursor = end_color + 1;
        }
        break;
    }
    case 10: { /* Set default foreground */
        if (args && args[0] != '\0') {
            uint32_t color = 0u;
            if (terminal_parse_hex_color(args, &color) == 0) {
                terminal_update_default_fg(buffer, color);
            }
        }
        break;
    }
    case 11: { /* Set default background */
        if (args && args[0] != '\0') {
            uint32_t color = 0u;
            if (terminal_parse_hex_color(args, &color) == 0) {
                terminal_update_default_bg(buffer, color);
            }
        }
        break;
    }
    case 12: { /* Set cursor color */
        if (args && args[0] != '\0') {
            uint32_t color = 0u;
            if (terminal_parse_hex_color(args, &color) == 0) {
                terminal_update_cursor_color(buffer, color);
            }
        }
        break;
    }
    case 777:
        terminal_handle_osc_777(buffer, args);
        break;
    case 104: /* Reset palette */
        if (!args || args[0] == '\0') {
            for (size_t i = 0u; i < 16u; i++) {
                buffer->palette[i] = terminal_default_palette16[i];
            }
        }
        break;
    case 110: /* Reset default foreground */
        terminal_update_default_fg(buffer, terminal_default_palette16[7]);
        break;
    case 111: /* Reset default background */
        terminal_update_default_bg(buffer, terminal_default_palette16[0]);
        break;
    case 112: /* Reset cursor color */
        terminal_update_cursor_color(buffer, terminal_default_palette16[7]);
        break;
    default:
        break;
    }

    parser->osc_length = 0u;
    if (sizeof(parser->osc_buffer) > 0u) {
        parser->osc_buffer[0] = '\0';
    }
}

static int ansi_parser_get_param(const struct ansi_parser *parser, size_t index, int default_value) {
    if (!parser) {
        return default_value;
    }
    if (index >= parser->param_count) {
        return default_value;
    }
    int value = parser->params[index];
    if (value < 0) {
        return default_value;
    }
    return value;
}

static void ansi_apply_csi(struct ansi_parser *parser, struct terminal_buffer *buffer, unsigned char command) {
    if (!buffer) {
        return;
    }

    switch (command) {
    case 'A': { /* Cursor Up */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, 0, -amount);
        break;
    }
    case 'B': { /* Cursor Down */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, 0, amount);
        break;
    }
    case 'C': { /* Cursor Forward */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, amount, 0);
        break;
    }
    case 'D': { /* Cursor Back */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, -amount, 0);
        break;
    }
    case 'E': { /* Cursor Next Line */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, 0, amount);
        buffer->cursor_column = 0u;
        break;
    }
    case 'F': { /* Cursor Previous Line */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, 0, -amount);
        buffer->cursor_column = 0u;
        break;
    }
    case 'G': { /* Cursor Horizontal Absolute */
        int column = ansi_parser_get_param(parser, 0u, 1);
        if (column < 1) {
            column = 1;
        }
        terminal_buffer_set_cursor(buffer, (size_t)(column - 1), buffer->cursor_row);
        break;
    }
    case 'H':
    case 'f': { /* Cursor Position */
        int row = ansi_parser_get_param(parser, 0u, 1);
        int column = ansi_parser_get_param(parser, 1u, 1);
        if (row < 1) {
            row = 1;
        }
        if (column < 1) {
            column = 1;
        }
        terminal_buffer_set_cursor(buffer, (size_t)(column - 1), (size_t)(row - 1));
        break;
    }
    case 'L': { /* Insert Line */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_insert_lines(buffer, (size_t)count);
        break;
    }
    case 'M': { /* Delete Line */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_delete_lines(buffer, (size_t)count);
        break;
    }
    case 'J': { /* Erase in Display */
        int mode = ansi_parser_get_param(parser, 0u, 0);
        switch (mode) {
        case 0:
            terminal_buffer_clear_to_end_of_display(buffer);
            break;
        case 1:
            terminal_buffer_clear_from_start_of_display(buffer);
            break;
        case 2:
        case 3:
            terminal_buffer_clear_display(buffer);
            break;
        default:
            break;
        }
        break;
    }
    case 'P': { /* Delete Character */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_delete_chars(buffer, (size_t)count);
        break;
    }
    case 'S': { /* Scroll Up */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_scroll_region_up(buffer, (size_t)count);
        break;
    }
    case 'T': { /* Scroll Down */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_scroll_region_down(buffer, (size_t)count);
        break;
    }
    case 'X': { /* Erase Character */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_erase_chars(buffer, (size_t)count);
        break;
    }
    case 'b': { /* Repeat the preceding character */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        if (buffer->last_emitted_valid) {
            for (int i = 0; i < count; i++) {
                terminal_put_char(buffer, buffer->last_emitted);
            }
        }
        break;
    }
    case '@': { /* Insert Character */
        int count = ansi_parser_get_param(parser, 0u, 1);
        if (count < 1) {
            count = 1;
        }
        terminal_buffer_insert_chars(buffer, (size_t)count);
        break;
    }
    case 'K': { /* Erase in Line */
        int mode = ansi_parser_get_param(parser, 0u, 0);
        switch (mode) {
        case 0:
            terminal_buffer_clear_line_from_cursor(buffer);
            break;
        case 1:
            terminal_buffer_clear_line_to_cursor(buffer);
            break;
        case 2:
            terminal_buffer_clear_line(buffer);
            break;
        default:
            break;
        }
        break;
    }
    case 's': /* Save cursor */
        terminal_buffer_save_cursor(buffer);
        break;
    case 'u': /* Restore cursor */
        terminal_buffer_restore_cursor(buffer);
        break;
    case 'm': /* Select Graphic Rendition - unsupported, ignore */
        terminal_apply_sgr(buffer, parser);
        break;
    case '`': { /* Horizontal Position Absolute */
        int column = ansi_parser_get_param(parser, 0u, 1);
        if (column < 1) {
            column = 1;
        }
        terminal_buffer_set_cursor(buffer, (size_t)(column - 1), buffer->cursor_row);
        break;
    }
    case 'd': { /* Vertical Position Absolute */
        int row = ansi_parser_get_param(parser, 0u, 1);
        if (row < 1) {
            row = 1;
        }
        terminal_buffer_set_cursor(buffer, buffer->cursor_column, (size_t)(row - 1));
        break;
    }
    case 'r': { /* Set scrolling region */
        int top = ansi_parser_get_param(parser, 0u, 1);
        int bottom = ansi_parser_get_param(parser, 1u, (int)buffer->rows);
        if (top < 1) {
            top = 1;
        }
        if (bottom < 1) {
            bottom = (int)buffer->rows;
        }
        if ((size_t)top > buffer->rows || (size_t)bottom > buffer->rows || top >= bottom) {
            buffer->scroll_top = 0u;
            buffer->scroll_bottom = buffer->rows > 0u ? buffer->rows - 1u : 0u;
        } else {
            buffer->scroll_top = (size_t)(top - 1);
            buffer->scroll_bottom = (size_t)(bottom - 1);
        }
        terminal_buffer_set_cursor(buffer, 0u, 0u);
        break;
    }
    case 'h':
    case 'l':
        if (parser && parser->private_marker == '?') {
            for (size_t i = 0u; i < parser->param_count; i++) {
                int mode = parser->params[i];
                if (mode < 0) {
                    continue;
                }
                switch (mode) {
                case 1: /* application cursor keys */
                    if (command == 'h') {
                        buffer->app_cursor = 1;
                    } else {
                        buffer->app_cursor = 0;
                    }
                    break;
                case 25: /* cursor visibility */
                    if (command == 'h') {
                        buffer->cursor_visible = 1;
                    } else {
                        buffer->cursor_visible = 0;
                    }
                    break;
                case 1000: /* mouse click tracking */
                    if (command == 'h') {
                        buffer->mouse_tracking = 1;
                    } else {
                        buffer->mouse_tracking = 0;
                    }
                    break;
                case 1002: /* mouse drag tracking */
                    if (command == 'h') {
                        buffer->mouse_drag_tracking = 1;
                    } else {
                        buffer->mouse_drag_tracking = 0;
                    }
                    break;
                case 1003: /* mouse motion tracking */
                    if (command == 'h') {
                        buffer->mouse_motion_tracking = 1;
                    } else {
                        buffer->mouse_motion_tracking = 0;
                    }
                    break;
                case 1006: /* SGR mouse mode */
                    if (command == 'h') {
                        buffer->mouse_sgr = 1;
                    } else {
                        buffer->mouse_sgr = 0;
                    }
                    break;
                case 2004: /* bracketed paste */
                    /* This is to enable multi-row paste in edit.c */
                    if (command == 'h') {
                        buffer->bracketed_paste_enabled = 1;
                    } else { /* 'l' */
                        buffer->bracketed_paste_enabled = 0;
                    }
                    break;
                case 47:
                case 1047:
                case 1049:
                    /* Alternate screen buffer. Clear to approximate behaviour. */
                    if (command == 'h') {
                        if (!terminal_using_alternate) {
                            terminal_buffer_save_cursor(buffer);
                            terminal_swap_alternate_buffer(buffer);
                            terminal_buffer_clear_display(buffer);
                        }
                        buffer->scroll_top = 0u;
                        buffer->scroll_bottom = buffer->rows > 0u ? buffer->rows - 1u : 0u;
                        buffer->scroll_offset = 0u;
                    } else {
                        if (terminal_using_alternate) {
                            terminal_swap_alternate_buffer(buffer);
                            terminal_buffer_restore_cursor(buffer);
                        }
                        buffer->scroll_top = 0u;
                        buffer->scroll_bottom = buffer->rows > 0u ? buffer->rows - 1u : 0u;
                        buffer->scroll_offset = 0u;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case 'n': {
        if (parser) {
            int query = ansi_parser_get_param(parser, 0u, 0);
            if (query == 5) {
                terminal_send_response("\x1b[0n");
            } else if (query == 6 && buffer) {
                size_t row = 1u;
                size_t column = 1u;
                if (buffer->rows > 0u) {
                    size_t cursor_row = buffer->cursor_row;
                    if (cursor_row >= buffer->rows) {
                        cursor_row = buffer->rows - 1u;
                    }
                    row = cursor_row + 1u;
                }
                if (buffer->columns > 0u) {
                    size_t cursor_column = buffer->cursor_column;
                    if (cursor_column >= buffer->columns) {
                        cursor_column = buffer->columns - 1u;
                    }
                    column = cursor_column + 1u;
                }
                char response[64];
                int written = snprintf(response, sizeof(response), "\x1b[%zu;%zuR", row, column);
                if (written > 0 && (size_t)written < sizeof(response)) {
                    terminal_send_response(response);
                }
            }
        }
        break;
    }
    case 'c': {
        if (!parser) {
            break;
        }
        const char *response = NULL;
        if (parser->private_marker == '?') {
            response = "\x1b[?1;0c";
        } else if (parser->private_marker == '>') {
            response = "\x1b[>0;95;0c";
        } else {
            response = "\x1b[?1;0c";
        }
        terminal_send_response(response);
        break;
    }
    default:
        break;
    }
}

static void ansi_parser_feed(struct ansi_parser *parser, struct terminal_buffer *buffer, unsigned char ch) {
    if (!parser) {
        return;
    }

    switch (parser->state) {
    case ANSI_STATE_GROUND:
        if (ch == 0x0Eu) {
            parser->charset_use_g1 = 1;
            return;
        }
        if (ch == 0x0Fu) {
            parser->charset_use_g1 = 0;
            return;
        }
        ansi_parser_feed_utf8(parser, buffer, ch);
        break;
    case ANSI_STATE_ESCAPE:
        if (ch == '[') {
            parser->state = ANSI_STATE_CSI;
            ansi_parser_reset_parameters(parser);
        } else if (ch == ']') {
            parser->state = ANSI_STATE_OSC;
            parser->osc_length = 0u;
            if (sizeof(parser->osc_buffer) > 0u) {
                parser->osc_buffer[0] = '\0';
            }
        } else if (ch == '(' || ch == ')' || ch == '*' || ch == '+' || ch == '-' || ch == '.') {
            parser->charset_target = (char)ch;
            parser->state = ANSI_STATE_ESCAPE_CHARSET;
        } else if (ch == 'c') {
            terminal_buffer_clear_display(buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == '7') {
            terminal_buffer_save_cursor(buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == '8') {
            terminal_buffer_restore_cursor(buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == 'D') {
            terminal_buffer_index(buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == 'M') {
            terminal_buffer_reverse_index(buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == '=') {
            if (buffer) {
                buffer->app_keypad = 1;
            }
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == '>') {
            if (buffer) {
                buffer->app_keypad = 0;
            }
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else {
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        }
        break;
    case ANSI_STATE_ESCAPE_CHARSET:
        if (parser->charset_target == '(') {
            parser->charset_g0 = (char)ch;
        } else if (parser->charset_target == ')') {
            parser->charset_g1 = (char)ch;
        }
        parser->state = ANSI_STATE_GROUND;
        ansi_parser_reset_utf8(parser);
        break;
    case ANSI_STATE_CSI:
        if (ch >= '0' && ch <= '9') {
            if (!parser->collecting_param) {
                if (parser->param_count < ANSI_MAX_PARAMS) {
                    parser->params[parser->param_count] = 0;
                    parser->param_count++;
                    parser->collecting_param = 1;
                }
            }
            if (parser->collecting_param && parser->param_count > 0u) {
                size_t index = parser->param_count - 1u;
                if (parser->params[index] >= 0) {
                    parser->params[index] = parser->params[index] * 10 + (ch - '0');
                }
            }
        } else if (ch == ';') {
            if (!parser->collecting_param) {
                if (parser->param_count < ANSI_MAX_PARAMS) {
                    parser->params[parser->param_count] = -1;
                    parser->param_count++;
                }
            }
            parser->collecting_param = 0;
        } else if (ch == '?') {
            parser->private_marker = '?';
        } else if (ch == '>') {
            parser->private_marker = '>';
        } else if (ch >= 0x40 && ch <= 0x7E) {
            ansi_apply_csi(parser, buffer, ch);
            ansi_parser_reset_parameters(parser);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else {
            /* Ignore unsupported intermediate bytes. */
        }
        break;
    case ANSI_STATE_OSC:
        if (ch == 0x07) {
            ansi_handle_osc(parser, buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else if (ch == 0x1b) {
            parser->state = ANSI_STATE_OSC_ESCAPE;
        } else {
            if (parser->osc_length + 1u < sizeof(parser->osc_buffer)) {
                parser->osc_buffer[parser->osc_length++] = (char)ch;
                parser->osc_buffer[parser->osc_length] = '\0';
            }
        }
        break;
    case ANSI_STATE_OSC_ESCAPE:
        if (ch == '\\') {
            ansi_handle_osc(parser, buffer);
            parser->state = ANSI_STATE_GROUND;
            ansi_parser_reset_utf8(parser);
        } else {
            parser->state = ANSI_STATE_OSC;
        }
        break;
    }
}

static int compute_root_directory(const char *argv0, char *out_path, size_t out_size) {
    if (!argv0 || !out_path || out_size == 0u) {
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            return -1;
        }
        size_t len = strlen(cwd);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, cwd, len + 1u);
        return 0;
    }

    char *last_sep = strrchr(resolved, '/');
    if (!last_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *last_sep = '\0';

    char *apps_sep = strrchr(resolved, '/');
    if (!apps_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *apps_sep = '\0';

    size_t len = strlen(resolved);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, resolved, len + 1u);
    return 0;
}

static int build_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
    if (!dest || dest_size == 0u || !base || !suffix) {
        return -1;
    }
    int written = snprintf(dest, dest_size, "%s/%s", base, suffix);
    if (written < 0 || (size_t)written >= dest_size) {
        return -1;
    }
    return 0;
}

static int terminal_resolve_shader_path(const char *root_dir, const char *shader_arg, char *out_path, size_t out_size) {
    if (!shader_arg || !out_path || out_size == 0u) {
        return -1;
    }

    if (shader_arg[0] == '/') {
        size_t len = strlen(shader_arg);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, shader_arg, len + 1u);
        return 0;
    }

    if (root_dir) {
        if (build_path(out_path, out_size, root_dir, shader_arg) == 0 && access(out_path, R_OK) == 0) {
            return 0;
        }
    }

    size_t len = strlen(shader_arg);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, shader_arg, len + 1u);
    return 0;
}

static void update_pty_size(int fd, size_t columns, size_t rows) {
    if (fd < 0) {
        return;
    }
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (columns > 0u) {
        ws.ws_col = (unsigned short)columns;
    }
    if (rows > 0u) {
        ws.ws_row = (unsigned short)rows;
    }
    ioctl(fd, TIOCSWINSZ, &ws);
}

static void terminal_update_render_size(size_t columns, size_t rows) {
    if (columns == 0u || rows == 0u) {
        return;
    }
    if (terminal_cell_pixel_width <= 0 || terminal_cell_pixel_height <= 0) {
        return;
    }
    if (columns > (size_t)(INT_MAX / terminal_cell_pixel_width) ||
        rows > (size_t)(INT_MAX / terminal_cell_pixel_height)) {
        return;
    }

    size_t base_width = columns * (size_t)terminal_cell_pixel_width;
    size_t base_height = rows * (size_t)terminal_cell_pixel_height;
    int margin = terminal_margin_pixels;
    if (margin < 0) {
        margin = 0;
    }

    size_t extra_width = (size_t)margin * 2u;
    size_t extra_height = (size_t)margin * 2u;
    if (base_width > SIZE_MAX - extra_width || base_height > SIZE_MAX - extra_height) {
        return;
    }

    size_t total_width = base_width + extra_width;
    size_t total_height = base_height + extra_height;

    if (total_width > (size_t)INT_MAX || total_height > (size_t)INT_MAX) {
        return;
    }

    int width = (int)total_width;
    int height = (int)total_height;
    terminal_logical_width = width;
    terminal_logical_height = height;

    if (terminal_gl_ready) {
        if (terminal_resize_render_targets(width, height) != 0) {
            fprintf(stderr, "Failed to resize terminal render targets.\n");
        }
    }

    terminal_mark_background_dirty();
    terminal_mark_full_redraw();
}

static int terminal_resize_buffer(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    if (!buffer || columns == 0u || rows == 0u) {
        return -1;
    }

    if (terminal_buffer_resize(buffer, columns, rows) != 0) {
        return -1;
    }

    terminal_update_render_size(columns, rows);

    if (terminal_window_handle && terminal_logical_width > 0 && terminal_logical_height > 0) {
        Uint32 flags = SDL_GetWindowFlags(terminal_window_handle);
        if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
            SDL_SetWindowSize(terminal_window_handle, terminal_logical_width, terminal_logical_height);
        }
    }

    if (terminal_master_fd_handle >= 0) {
        update_pty_size(terminal_master_fd_handle, columns, rows);
    }

    return 0;
}

static void terminal_apply_scale(struct terminal_buffer *buffer, int scale) {
    if (!buffer || scale <= 0) {
        return;
    }

    if (scale > 4) {
        scale = 4;
    }

    if (scale == terminal_scale_factor && !terminal_resolution_override_active) {
        return;
    }

    size_t base_columns = (size_t)TERMINAL_COLUMNS;
    size_t base_rows = (size_t)TERMINAL_ROWS;
    size_t scale_value = (size_t)scale;
    if (scale_value > 0u) {
        if (scale_value > SIZE_MAX / base_columns || scale_value > SIZE_MAX / base_rows) {
            return;
        }
    }
    size_t new_columns = base_columns * scale_value;
    size_t new_rows = base_rows * scale_value;

    if (terminal_resize_buffer(buffer, new_columns, new_rows) != 0) {
        return;
    }

    terminal_scale_factor = scale;
    terminal_resolution_override_active = 0;
    terminal_resolution_width = 0;
    terminal_resolution_height = 0;
}

static void terminal_apply_resolution(struct terminal_buffer *buffer, int width, int height) {
    if (!buffer) {
        return;
    }

    if (width <= 0 || height <= 0) {
        if (terminal_resolution_override_active) {
            int scale = terminal_scale_factor;
            if (scale <= 0) {
                scale = 1;
            }
            terminal_apply_scale(buffer, scale);
        }
        return;
    }

    if (terminal_cell_pixel_width <= 0 || terminal_cell_pixel_height <= 0) {
        return;
    }

    size_t cell_width = (size_t)terminal_cell_pixel_width;
    size_t cell_height = (size_t)terminal_cell_pixel_height;
    size_t requested_width = (size_t)width;
    size_t requested_height = (size_t)height;
    size_t columns = requested_width / cell_width;
    size_t rows = requested_height / cell_height;

    if (columns == 0u && requested_width > 0u) {
        columns = 1u;
    }
    if (rows == 0u && requested_height > 0u) {
        rows = 1u;
    }

    if (columns == 0u || rows == 0u) {
        return;
    }

    if (buffer->columns == columns && buffer->rows == rows) {
        terminal_resolution_override_active = 1;
        terminal_resolution_width = width;
        terminal_resolution_height = height;
        return;
    }

    if (terminal_resize_buffer(buffer, columns, rows) != 0) {
        return;
    }

    terminal_resolution_override_active = 1;
    terminal_resolution_width = width;
    terminal_resolution_height = height;
}

static void terminal_apply_margin(struct terminal_buffer *buffer, int margin) {
    if (!buffer) {
        return;
    }

    if (margin < 0) {
        margin = 0;
    }

    if (margin > 0) {
        int max_margin = INT_MAX / 4;
        if (margin > max_margin) {
            margin = max_margin;
        }
    }

    if (margin == terminal_margin_pixels) {
        return;
    }

    terminal_margin_pixels = margin;
    terminal_update_render_size(buffer->columns, buffer->rows);

    if (terminal_window_handle && terminal_logical_width > 0 && terminal_logical_height > 0) {
        Uint32 flags = SDL_GetWindowFlags(terminal_window_handle);
        if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
            SDL_SetWindowSize(terminal_window_handle, terminal_logical_width, terminal_logical_height);
        }
    }
}

static pid_t spawn_budostack(const char *exe_path, int *out_master_fd) {
    if (!exe_path || !out_master_fd) {
        return -1;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        if (setsid() == -1) {
            perror("setsid");
            _exit(EXIT_FAILURE);
        }

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            perror("open slave pty");
            _exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY");
            _exit(EXIT_FAILURE);
        }

        if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 || dup2(slave_fd, STDERR_FILENO) < 0) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        close(master_fd);

        const char *term_value = getenv("TERM");
        if (!term_value || term_value[0] == '\0') {
            setenv("TERM", "xterm-256color", 1);
        }

        execl(exe_path, exe_path, (char *)NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    *out_master_fd = master_fd;
    return pid;
}

static ssize_t safe_write(int fd, const void *buf, size_t count) {
    const unsigned char *ptr = (const unsigned char *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += (size_t)written;
        remaining -= (size_t)written;
    }
    return (ssize_t)count;
}

static int terminal_send_bytes(int fd, const void *data, size_t length) {
    if (safe_write(fd, data, length) < 0) {
        return -1;
    }
    return 0;
}

static int terminal_send_string(int fd, const char *str) {
    return terminal_send_bytes(fd, str, strlen(str));
}

static int terminal_mod_state_has_altgr(SDL_Keymod mod) {
    if ((mod & KMOD_MODE) != 0) {
        return 1;
    }

    if ((mod & KMOD_RALT) != 0) {
        SDL_Keymod ctrl_mask = (SDL_Keymod)(KMOD_LCTRL | KMOD_RCTRL);
        if ((mod & ctrl_mask) != 0) {
            return 1;
        }
        if ((mod & KMOD_LALT) == 0) {
            return 1;
        }
    }

    return 0;
}

static SDL_Keymod terminal_normalize_modifiers(SDL_Keymod mod) {
    if (terminal_mod_state_has_altgr(mod)) {
        mod = (SDL_Keymod)(mod & ~(KMOD_CTRL | KMOD_ALT));
    }
    return mod;
}

static unsigned int terminal_modifier_param(SDL_Keymod mod) {
    SDL_Keymod normalized = terminal_normalize_modifiers(mod);
    unsigned int value = 1u;
    if ((normalized & KMOD_SHIFT) != 0) {
        value += 1u;
    }
    if ((normalized & KMOD_ALT) != 0) {
        value += 2u;
    }
    if ((normalized & KMOD_CTRL) != 0) {
        value += 4u;
    }
    return value;
}

static int terminal_send_csi_final(int fd, SDL_Keymod mod, char final_char) {
    unsigned int modifier = terminal_modifier_param(mod);
    char sequence[32];
    if (modifier == 1u) {
        sequence[0] = '\x1b';
        sequence[1] = '[';
        sequence[2] = final_char;
        sequence[3] = '\0';
    } else {
        if (snprintf(sequence, sizeof(sequence), "\x1b[1;%u%c", modifier, final_char) < 0) {
            return -1;
        }
    }
    return terminal_send_string(fd, sequence);
}

static int terminal_send_csi_number(int fd, SDL_Keymod mod, unsigned int number) {
    unsigned int modifier = terminal_modifier_param(mod);
    char sequence[32];
    if (modifier == 1u) {
        if (snprintf(sequence, sizeof(sequence), "\x1b[%u~", number) < 0) {
            return -1;
        }
    } else {
        if (snprintf(sequence, sizeof(sequence), "\x1b[%u;%u~", number, modifier) < 0) {
            return -1;
        }
    }
    return terminal_send_string(fd, sequence);
}

static int terminal_send_ss3_final(int fd, SDL_Keymod mod, char final_char) {
    unsigned int modifier = terminal_modifier_param(mod);
    char sequence[32];
    if (modifier == 1u) {
        sequence[0] = '\x1b';
        sequence[1] = 'O';
        sequence[2] = final_char;
        sequence[3] = '\0';
    } else {
        if (snprintf(sequence, sizeof(sequence), "\x1b[1;%u%c", modifier, final_char) < 0) {
            return -1;
        }
    }
    return terminal_send_string(fd, sequence);
}

static int terminal_send_escape_prefix(int fd) {
    const unsigned char esc = 0x1Bu;
    return terminal_send_bytes(fd, &esc, 1u);
}

int main(int argc, char **argv) {
    const char *progname = (argc > 0 && argv && argv[0]) ? argv[0] : "terminal";
    const char **shader_args = NULL;
    size_t shader_arg_count = 0u;
    struct shader_path_entry *shader_paths = NULL;
    size_t shader_path_count = 0u;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--shader") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing shader path after %s.\n", arg);
                terminal_print_usage(progname);
                free(shader_args);
                return EXIT_FAILURE;
            }
            const char *value = argv[++i];
            const char **new_args = realloc(shader_args, (shader_arg_count + 1u) * sizeof(*new_args));
            if (!new_args) {
                fprintf(stderr, "Failed to allocate memory for shader arguments.\n");
                free(shader_args);
                return EXIT_FAILURE;
            }
            shader_args = new_args;
            shader_args[shader_arg_count++] = value;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            terminal_print_usage(progname);
            free(shader_args);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Unrecognized argument: %s\n", arg);
            terminal_print_usage(progname);
            free(shader_args);
            return EXIT_FAILURE;
        }
    }

    char root_dir[PATH_MAX];
    if (compute_root_directory(argv[0], root_dir, sizeof(root_dir)) != 0) {
        fprintf(stderr, "Failed to resolve BUDOSTACK root directory.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }

    char budostack_path[PATH_MAX];
    if (build_path(budostack_path, sizeof(budostack_path), root_dir, "budostack") != 0) {
        fprintf(stderr, "Failed to resolve budostack executable path.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }

    if (access(budostack_path, X_OK) != 0) {
        fprintf(stderr, "Could not find executable at %s.\n", budostack_path);
        free(shader_args);
        return EXIT_FAILURE;
    }

    char font_path[PATH_MAX];
    if (build_path(font_path, sizeof(font_path), root_dir, "fonts/system.psf") != 0) {
        fprintf(stderr, "Failed to resolve font path.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }

    char errbuf[256];
    if (load_psf_font(font_path, &terminal_font, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Failed to load font: %s\n", errbuf);
        free(shader_args);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < shader_arg_count; i++) {
        struct shader_path_entry *new_paths = realloc(shader_paths, (shader_path_count + 1u) * sizeof(*new_paths));
        if (!new_paths) {
            fprintf(stderr, "Failed to allocate memory for shader paths.\n");
            free(shader_paths);
            free(shader_args);
            free_font(&terminal_font);
            return EXIT_FAILURE;
        }
        shader_paths = new_paths;
        if (terminal_resolve_shader_path(root_dir, shader_args[i], shader_paths[shader_path_count].path, sizeof(shader_paths[shader_path_count].path)) != 0) {
            fprintf(stderr, "Shader path is too long.\n");
            free(shader_paths);
            free(shader_args);
            free_font(&terminal_font);
            return EXIT_FAILURE;
        }
        shader_path_count++;
    }

    free(shader_args);
    shader_args = NULL;

    terminal_free_requested_shaders();
    if (shader_path_count > 0u) {
        terminal_requested_shaders = malloc(shader_path_count * sizeof(*terminal_requested_shaders));
        if (!terminal_requested_shaders) {
            fprintf(stderr, "Failed to store requested shader paths.\n");
            free_font(&terminal_font);
            free(shader_paths);
            return EXIT_FAILURE;
        }
        for (size_t i = 0; i < shader_path_count; i++) {
            memcpy(terminal_requested_shaders[i].path, shader_paths[i].path, sizeof(terminal_requested_shaders[i].path));
        }
        terminal_requested_shader_count = shader_path_count;
    }

    size_t glyph_width_size = (size_t)terminal_font.width * (size_t)TERMINAL_FONT_SCALE;
    size_t glyph_height_size = (size_t)terminal_font.height * (size_t)TERMINAL_FONT_SCALE;
    if (glyph_width_size == 0u || glyph_height_size == 0u ||
        glyph_width_size > (size_t)INT_MAX || glyph_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Scaled font dimensions invalid.\n");
        free_font(&terminal_font);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }
    int glyph_width = (int)glyph_width_size;
    int glyph_height = (int)glyph_height_size;

    size_t window_width_size = glyph_width_size * (size_t)TERMINAL_COLUMNS;
    size_t window_height_size = glyph_height_size * (size_t)TERMINAL_ROWS;
    int initial_margin = terminal_margin_pixels;
    if (initial_margin < 0) {
        initial_margin = 0;
    }
    size_t margin_component = (size_t)initial_margin * 2u;
    if (window_width_size <= SIZE_MAX - margin_component &&
        window_height_size <= SIZE_MAX - margin_component) {
        window_width_size += margin_component;
        window_height_size += margin_component;
    }
    if (window_width_size == 0u || window_height_size == 0u ||
        window_width_size > (size_t)INT_MAX || window_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Computed window dimensions invalid.\n");
        free_font(&terminal_font);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }
    int window_width = (int)window_width_size;
    int window_height = (int)window_height_size;
    int drawable_width = 0;
    int drawable_height = 0;

    int master_fd = -1;
    pid_t child_pid = spawn_budostack(budostack_path, &master_fd);
    if (child_pid < 0) {
        free_font(&terminal_font);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

#if BUDOSTACK_HAVE_SDL2
    if (terminal_initialize_audio() != 0) {
        fprintf(stderr, "terminal: Audio subsystem disabled due to initialization failure.\n");
    }
#endif

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#ifdef SDL_GL_CONTEXT_PROFILE_MASK
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Window *window = SDL_CreateWindow("BUDOSTACK Terminal",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          window_width,
                                          window_height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    if (terminal_initialize_quad_geometry() != 0) {
        fprintf(stderr, "Failed to initialize fullscreen quad geometry.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        free(shader_paths);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "Warning: Unable to enable VSync: %s\n", SDL_GetError());
    }
    terminal_vsync_enabled = SDL_GL_GetSwapInterval() > 0 ? 1 : 0;

    for (size_t i = 0; i < shader_path_count; i++) {
        if (terminal_initialize_gl_program(shader_paths[i].path) != 0) {
            terminal_release_gl_resources();
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
            terminal_shutdown_audio();
#endif
            SDL_Quit();
            kill(child_pid, SIGKILL);
            free_font(&terminal_font);
            free(shader_paths);
            close(master_fd);
            terminal_free_requested_shaders();
            return EXIT_FAILURE;
        }
    }

    free(shader_paths);
    shader_paths = NULL;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    terminal_window_handle = window;
    terminal_gl_context_handle = gl_context;
    terminal_master_fd_handle = master_fd;
    terminal_cell_pixel_width = glyph_width;
    terminal_cell_pixel_height = glyph_height;
    terminal_scale_factor = 1;
    terminal_gl_ready = 1;

    drawable_width = 0;
    drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        fprintf(stderr, "Drawable size is invalid.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }
    glViewport(0, 0, drawable_width, drawable_height);

    size_t columns = (size_t)TERMINAL_COLUMNS;
    size_t rows = (size_t)TERMINAL_ROWS;

    terminal_update_render_size(columns, rows);
    if (!terminal_framebuffer_pixels || terminal_framebuffer_width <= 0 || terminal_framebuffer_height <= 0) {
        fprintf(stderr, "Failed to allocate terminal framebuffer.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }

    struct terminal_buffer buffer = {0};
    terminal_buffer_initialize_palette(&buffer);
    if (terminal_buffer_init(&buffer, columns, rows) != 0) {
        fprintf(stderr, "Failed to allocate terminal buffer.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
#if BUDOSTACK_HAVE_SDL2
        terminal_shutdown_audio();
#endif
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&terminal_font);
        close(master_fd);
        terminal_free_requested_shaders();
        return EXIT_FAILURE;
    }
    struct terminal_buffer alternate_buffer = {0};
    terminal_alternate_buffer_handle = &alternate_buffer;
    terminal_alternate_initialized = 0;
    terminal_using_alternate = 0;

    update_pty_size(master_fd, columns, rows);

    struct ansi_parser parser;
    ansi_parser_init(&parser);

    SDL_StartTextInput();

    if (terminal_load_cursor_sprite(TERMINAL_CURSOR_SPRITE_PATH) == 0) {
        SDL_ShowCursor(SDL_DISABLE);
        int mouse_x = 0;
        int mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        terminal_cursor_update_position(mouse_x, mouse_y);
    } else {
        SDL_ShowCursor(SDL_ENABLE);
    }

    if (TERMINAL_SHADER_TARGET_FPS > 0u) {
        terminal_shader_frame_interval_ms = 1000u / (Uint32)TERMINAL_SHADER_TARGET_FPS;
        if (terminal_shader_frame_interval_ms == 0u) {
            terminal_shader_frame_interval_ms = 1u;
        }
    } else {
        terminal_shader_frame_interval_ms = 0u;
    }
    terminal_shader_last_frame_tick = SDL_GetTicks();

    if (terminal_vsync_enabled) {
        terminal_render_frame_interval_ms = 0u;
    } else if (TERMINAL_TARGET_FPS > 0u) {
        terminal_render_frame_interval_ms = 1000u / (Uint32)TERMINAL_TARGET_FPS;
        if (terminal_render_frame_interval_ms == 0u) {
            terminal_render_frame_interval_ms = 1u;
        }
    } else {
        terminal_render_frame_interval_ms = 0u;
    }
    terminal_render_last_frame_tick = SDL_GetTicks();

    int status = 0;
    int child_exited = 0;
    unsigned char input_buffer[512];
    int running = 1;
    const Uint32 cursor_blink_interval = TERMINAL_CURSOR_BLINK_INTERVAL;
    Uint32 cursor_last_toggle = SDL_GetTicks();
    int cursor_phase_visible = 1;

    while (running) {
        terminal_selection_validate(&buffer);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_WINDOWEVENT &&
                       (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                Uint32 flags = SDL_GetWindowFlags(window);
                if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
                    if (terminal_logical_width > 0 && terminal_logical_height > 0) {
                        SDL_SetWindowSize(window, terminal_logical_width, terminal_logical_height);
                    }
                }
                SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
                if (drawable_width > 0 && drawable_height > 0) {
                    glViewport(0, 0, drawable_width, drawable_height);
                }
                if (terminal_cursor_enabled) {
                    terminal_cursor_dirty = 1;
                    int mouse_x = 0;
                    int mouse_y = 0;
                    SDL_GetMouseState(&mouse_x, &mouse_y);
                    terminal_cursor_update_position(mouse_x, mouse_y);
                }
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_LEAVE) {
                if (terminal_cursor_enabled && terminal_cursor_position_valid) {
                    terminal_cursor_position_valid = 0;
                    terminal_cursor_dirty = 1;
                }
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_ENTER) {
                if (terminal_cursor_enabled) {
                    int mouse_x = 0;
                    int mouse_y = 0;
                    SDL_GetMouseState(&mouse_x, &mouse_y);
                    terminal_cursor_update_position(mouse_x, mouse_y);
                }
#if SDL_MAJOR_VERSION >= 2
            } else if (event.type == SDL_MOUSEWHEEL) {
                int wheel_y = event.wheel.y;
#if defined(SDL_MOUSEWHEEL_FLIPPED)
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_y = -wheel_y;
                }
#endif
                if (terminal_mouse_reporting_enabled(&buffer)) {
                    int button_code = (wheel_y > 0) ? 64 : 65;
                    size_t top_index = 0u;
                    terminal_visible_row_range(&buffer, &top_index, NULL);
                    size_t total_rows = terminal_total_rows(&buffer);
                    size_t global_row = 0u;
                    size_t column = 0u;
                    int logical_x = 0;
                    int logical_y = 0;
                    int mouse_x = 0;
                    int mouse_y = 0;
                    SDL_GetMouseState(&mouse_x, &mouse_y);
                    if (terminal_window_point_to_framebuffer(mouse_x, mouse_y, &logical_x, &logical_y) == 0 &&
                        terminal_screen_point_to_cell(logical_x,
                                                      logical_y,
                                                      buffer.columns,
                                                      buffer.rows,
                                                      top_index,
                                                      total_rows,
                                                      &global_row,
                                                      &column,
                                                      1) == 0) {
                        size_t row_in_view = global_row >= top_index ? global_row - top_index : 0u;
                        if (row_in_view >= buffer.rows && buffer.rows > 0u) {
                            row_in_view = buffer.rows - 1u;
                        }
                        if (column >= buffer.columns && buffer.columns > 0u) {
                            column = buffer.columns - 1u;
                        }
                        terminal_send_mouse_report(&buffer,
                                                   button_code,
                                                   0,
                                                   0,
                                                   column + 1u,
                                                   row_in_view + 1u,
                                                   SDL_GetModState());
                    }
                } else if (wheel_y > 0) {
                    size_t delta = (size_t)wheel_y;
                    buffer.scroll_offset += delta;
                    terminal_buffer_clamp_scroll(&buffer);
                } else if (wheel_y < 0) {
                    size_t delta = (size_t)(-wheel_y);
                    if (delta >= buffer.scroll_offset) {
                        buffer.scroll_offset = 0u;
                    } else {
                        buffer.scroll_offset -= delta;
                    }
                }
#endif
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                terminal_input_draw_requested = 1;
                terminal_cursor_update_position(event.button.x, event.button.y);
                int mouse_reporting = terminal_mouse_reporting_enabled(&buffer);
                if (mouse_reporting) {
                    size_t top_index = 0u;
                    terminal_visible_row_range(&buffer, &top_index, NULL);
                    size_t total_rows = terminal_total_rows(&buffer);
                    size_t global_row = 0u;
                    size_t column = 0u;
                    int logical_x = 0;
                    int logical_y = 0;
                    if (terminal_window_point_to_framebuffer(event.button.x, event.button.y, &logical_x, &logical_y) == 0 &&
                        terminal_screen_point_to_cell(logical_x,
                                                      logical_y,
                                                      buffer.columns,
                                                      buffer.rows,
                                                      top_index,
                                                      total_rows,
                                                      &global_row,
                                                      &column,
                                                      1) == 0) {
                        size_t row_in_view = global_row >= top_index ? global_row - top_index : 0u;
                        if (row_in_view >= buffer.rows && buffer.rows > 0u) {
                            row_in_view = buffer.rows - 1u;
                        }
                        if (column >= buffer.columns && buffer.columns > 0u) {
                            column = buffer.columns - 1u;
                        }
                        int button_code = -1;
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            button_code = 0;
                        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                            button_code = 1;
                        } else if (event.button.button == SDL_BUTTON_RIGHT) {
                            button_code = 2;
                        }
                        if (button_code >= 0) {
                            terminal_send_mouse_report(&buffer,
                                                       button_code,
                                                       0,
                                                       0,
                                                       column + 1u,
                                                       row_in_view + 1u,
                                                       SDL_GetModState());
                        }
                    }
                    terminal_selection_clear();
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    size_t top_index = 0u;
                    terminal_visible_row_range(&buffer, &top_index, NULL);
                    size_t total_rows = terminal_total_rows(&buffer);
                    size_t global_row = 0u;
                    size_t column = 0u;
                    int logical_x = 0;
                    int logical_y = 0;
                    if (terminal_window_point_to_framebuffer(event.button.x, event.button.y, &logical_x, &logical_y) == 0 &&
                        terminal_screen_point_to_cell(logical_x,
                                                      logical_y,
                                                      buffer.columns,
                                                      buffer.rows,
                                                      top_index,
                                                      total_rows,
                                                      &global_row,
                                                      &column,
                                                      0) == 0) {
                        terminal_selection_begin(global_row, column);
                        terminal_selection_dragging = 1;
                    } else {
                        terminal_selection_clear();
                    }
                    terminal_mouse_left_clicks++;
                } else {
                    terminal_selection_clear();
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        terminal_mouse_right_clicks++;
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                terminal_input_draw_requested = 1;
                if (terminal_mouse_reporting_enabled(&buffer)) {
                    size_t top_index = 0u;
                    terminal_visible_row_range(&buffer, &top_index, NULL);
                    size_t total_rows = terminal_total_rows(&buffer);
                    size_t global_row = 0u;
                    size_t column = 0u;
                    int logical_x = 0;
                    int logical_y = 0;
                    if (terminal_window_point_to_framebuffer(event.button.x, event.button.y, &logical_x, &logical_y) == 0 &&
                        terminal_screen_point_to_cell(logical_x,
                                                      logical_y,
                                                      buffer.columns,
                                                      buffer.rows,
                                                      top_index,
                                                      total_rows,
                                                      &global_row,
                                                      &column,
                                                      1) == 0) {
                        size_t row_in_view = global_row >= top_index ? global_row - top_index : 0u;
                        if (row_in_view >= buffer.rows && buffer.rows > 0u) {
                            row_in_view = buffer.rows - 1u;
                        }
                        if (column >= buffer.columns && buffer.columns > 0u) {
                            column = buffer.columns - 1u;
                        }
                        int button_code = -1;
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            button_code = 0;
                        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                            button_code = 1;
                        } else if (event.button.button == SDL_BUTTON_RIGHT) {
                            button_code = 2;
                        }
                        if (button_code >= 0) {
                            terminal_send_mouse_report(&buffer,
                                                       button_code,
                                                       1,
                                                       0,
                                                       column + 1u,
                                                       row_in_view + 1u,
                                                       SDL_GetModState());
                        }
                    }
                    terminal_selection_dragging = 0;
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    terminal_selection_dragging = 0;
                }
            } else if (event.type == SDL_MOUSEMOTION) {
                terminal_cursor_update_position(event.motion.x, event.motion.y);
                if (terminal_mouse_reporting_enabled(&buffer) &&
                    (buffer.mouse_motion_tracking || buffer.mouse_drag_tracking)) {
                    int send_motion = 0;
                    int button_code = 0;
                    if ((event.motion.state & SDL_BUTTON_LMASK) != 0) {
                        button_code = 0;
                        send_motion = 1;
                    } else if ((event.motion.state & SDL_BUTTON_MMASK) != 0) {
                        button_code = 1;
                        send_motion = 1;
                    } else if ((event.motion.state & SDL_BUTTON_RMASK) != 0) {
                        button_code = 2;
                        send_motion = 1;
                    } else if (buffer.mouse_motion_tracking) {
                        button_code = 0;
                        send_motion = 1;
                    }
                    if (send_motion) {
                        size_t top_index = 0u;
                        terminal_visible_row_range(&buffer, &top_index, NULL);
                        size_t total_rows = terminal_total_rows(&buffer);
                        size_t global_row = 0u;
                        size_t column = 0u;
                        int logical_x = 0;
                        int logical_y = 0;
                        if (terminal_window_point_to_framebuffer(event.motion.x, event.motion.y, &logical_x, &logical_y) == 0 &&
                            terminal_screen_point_to_cell(logical_x,
                                                          logical_y,
                                                          buffer.columns,
                                                          buffer.rows,
                                                          top_index,
                                                          total_rows,
                                                          &global_row,
                                                          &column,
                                                          1) == 0) {
                            size_t row_in_view = global_row >= top_index ? global_row - top_index : 0u;
                            if (row_in_view >= buffer.rows && buffer.rows > 0u) {
                                row_in_view = buffer.rows - 1u;
                            }
                            if (column >= buffer.columns && buffer.columns > 0u) {
                                column = buffer.columns - 1u;
                            }
                            terminal_send_mouse_report(&buffer,
                                                       button_code,
                                                       0,
                                                       1,
                                                       column + 1u,
                                                       row_in_view + 1u,
                                                       SDL_GetModState());
                        }
                    }
                } else if (terminal_selection_dragging) {
                    if ((event.motion.state & SDL_BUTTON_LMASK) == 0) {
                        terminal_selection_dragging = 0;
                    } else {
                        size_t top_index = 0u;
                        terminal_visible_row_range(&buffer, &top_index, NULL);
                        size_t total_rows = terminal_total_rows(&buffer);
                        size_t global_row = 0u;
                        size_t column = 0u;
                        int logical_x = 0;
                        int logical_y = 0;
                        if (terminal_window_point_to_framebuffer(event.motion.x, event.motion.y, &logical_x, &logical_y) == 0 &&
                            terminal_screen_point_to_cell(logical_x,
                                                          logical_y,
                                                          buffer.columns,
                                                          buffer.rows,
                                                          top_index,
                                                          total_rows,
                                                          &global_row,
                                                          &column,
                                                          1) == 0) {
                            terminal_selection_update(global_row, column);
                        }
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                terminal_input_draw_requested = 1;
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod mod = terminal_normalize_modifiers(event.key.keysym.mod);
                int handled = 0;
                unsigned char ch = 0u;

                int clipboard_handled = 0;
                if ((mod & KMOD_CTRL) != 0 && (mod & KMOD_ALT) == 0 && (mod & KMOD_GUI) == 0) {
                    if (sym == SDLK_c) {
                        if (terminal_copy_selection_to_clipboard(&buffer)) {
                            clipboard_handled = 1;
                        }
                    }
                    if ((mod & KMOD_SHIFT) != 0 && sym == SDLK_v) {
                        if (terminal_paste_from_clipboard(&buffer, master_fd) == 0) {
                            clipboard_handled = 1;
                        }
                    }
                }
                if ((mod & KMOD_SHIFT) != 0 && sym == SDLK_INSERT) {
                    if (terminal_paste_from_clipboard(&buffer, master_fd) == 0) {
                        clipboard_handled = 1;
                    }
                }
                if (clipboard_handled) {
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
                    continue;
                }

                if ((mod & KMOD_CTRL) != 0) {
                    if (sym >= 0 && sym <= 127) {
                        int ascii = (int)sym;
                        if (ascii >= 'a' && ascii <= 'z') {
                            ascii -= ('a' - 'A');
                        }
                        if (ascii >= '@' && ascii <= '_') {
                            ch = (unsigned char)(ascii - '@');
                            handled = 1;
                        } else if (ascii == ' ') {
                            ch = 0u;
                            handled = 1;
                        } else if (ascii == '/') {
                            ch = 31u;
                            handled = 1;
                        } else if (ascii == '?') {
                            ch = 127u;
                            handled = 1;
                        }
                    }
                }

                if (handled) {
                    terminal_selection_clear();
                    if (terminal_send_bytes(master_fd, &ch, 1u) < 0) {
                        running = 0;
                    }
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
                    continue;
                }

                switch (sym) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER: {
                    unsigned int modifier = terminal_modifier_param(mod);
                    if (sym == SDLK_KP_ENTER && buffer.app_keypad && modifier == 1u) {
                        if (terminal_send_ss3_final(master_fd, mod, 'M') < 0) {
                            running = 0;
                        }
                    } else if (modifier == 1u) {
                        unsigned char cr = '\r';
                        if (terminal_send_bytes(master_fd, &cr, 1u) < 0) {
                            running = 0;
                        }
                    } else if (terminal_send_csi_number(master_fd, mod, 13u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                }
                case SDLK_BACKSPACE: {
                    unsigned int modifier = terminal_modifier_param(mod);
                    if (modifier == 1u) {
                        unsigned char del = 0x7Fu;
                        if (terminal_send_bytes(master_fd, &del, 1u) < 0) {
                            running = 0;
                        }
                    } else if (terminal_send_csi_number(master_fd, mod, 127u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                }
                case SDLK_TAB: {
                    unsigned int modifier = terminal_modifier_param(mod);
                    int has_ctrl_or_alt = (mod & (KMOD_CTRL | KMOD_ALT)) != 0;
                    if (modifier == 1u) {
                        unsigned char tab = '\t';
                        if (terminal_send_bytes(master_fd, &tab, 1u) < 0) {
                            running = 0;
                        }
                        handled = 1;
                    } else if ((mod & KMOD_SHIFT) != 0 && !has_ctrl_or_alt && modifier == 2u) {
                        if (terminal_send_string(master_fd, "\x1b[Z") < 0) {
                            running = 0;
                        }
                        handled = 1;
                    } else if (terminal_send_csi_number(master_fd, mod, 9u) < 0) {
                        running = 0;
                        handled = 1;
                    } else {
                        handled = 1;
                    }
                    break;
                }
                case SDLK_ESCAPE:
                    if (terminal_send_escape_prefix(master_fd) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_UP:
                    if ((buffer.app_cursor != 0) ?
                        (terminal_send_ss3_final(master_fd, mod, 'A') < 0) :
                        (terminal_send_csi_final(master_fd, mod, 'A') < 0)) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_DOWN:
                    if ((buffer.app_cursor != 0) ?
                        (terminal_send_ss3_final(master_fd, mod, 'B') < 0) :
                        (terminal_send_csi_final(master_fd, mod, 'B') < 0)) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_RIGHT:
                    if ((buffer.app_cursor != 0) ?
                        (terminal_send_ss3_final(master_fd, mod, 'C') < 0) :
                        (terminal_send_csi_final(master_fd, mod, 'C') < 0)) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_LEFT:
                    if ((buffer.app_cursor != 0) ?
                        (terminal_send_ss3_final(master_fd, mod, 'D') < 0) :
                        (terminal_send_csi_final(master_fd, mod, 'D') < 0)) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_HOME:
                    if (terminal_send_csi_final(master_fd, mod, 'H') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_END:
                    if (terminal_send_csi_final(master_fd, mod, 'F') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_PAGEUP:
                    if (terminal_send_csi_number(master_fd, mod, 5u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_PAGEDOWN:
                    if (terminal_send_csi_number(master_fd, mod, 6u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_INSERT:
                    if (terminal_send_csi_number(master_fd, mod, 2u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_DELETE:
                    if (terminal_send_csi_number(master_fd, mod, 3u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F1:
                    if (terminal_send_ss3_final(master_fd, mod, 'P') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F2:
                    if (terminal_send_ss3_final(master_fd, mod, 'Q') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F3:
                    if (terminal_send_ss3_final(master_fd, mod, 'R') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F4:
                    if (terminal_send_ss3_final(master_fd, mod, 'S') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F5:
                    if (terminal_send_csi_number(master_fd, mod, 15u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F6:
                    if (terminal_send_csi_number(master_fd, mod, 17u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F7:
                    if (terminal_send_csi_number(master_fd, mod, 18u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F8:
                    if (terminal_send_csi_number(master_fd, mod, 19u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F9:
                    if (terminal_send_csi_number(master_fd, mod, 20u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F10:
                    if (terminal_send_csi_number(master_fd, mod, 21u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F11:
                    if (terminal_send_csi_number(master_fd, mod, 23u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F12:
                    if (terminal_send_csi_number(master_fd, mod, 24u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F13:
                    if (terminal_send_csi_number(master_fd, mod, 25u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F14:
                    if (terminal_send_csi_number(master_fd, mod, 26u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F15:
                    if (terminal_send_csi_number(master_fd, mod, 28u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F16:
                    if (terminal_send_csi_number(master_fd, mod, 29u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F17:
                    if (terminal_send_csi_number(master_fd, mod, 31u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F18:
                    if (terminal_send_csi_number(master_fd, mod, 32u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F19:
                    if (terminal_send_csi_number(master_fd, mod, 33u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F20:
                    if (terminal_send_csi_number(master_fd, mod, 34u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F21:
                    if (terminal_send_csi_number(master_fd, mod, 42u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F22:
                    if (terminal_send_csi_number(master_fd, mod, 43u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F23:
                    if (terminal_send_csi_number(master_fd, mod, 44u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F24:
                    if (terminal_send_csi_number(master_fd, mod, 45u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_KP_0:
                case SDLK_KP_1:
                case SDLK_KP_2:
                case SDLK_KP_3:
                case SDLK_KP_4:
                case SDLK_KP_5:
                case SDLK_KP_6:
                case SDLK_KP_7:
                case SDLK_KP_8:
                case SDLK_KP_9:
                case SDLK_KP_PERIOD:
                case SDLK_KP_PLUS:
                case SDLK_KP_MINUS:
                case SDLK_KP_MULTIPLY:
                case SDLK_KP_DIVIDE:
                    if (buffer.app_keypad) {
                        char final_char = 0;
                        switch (sym) {
                        case SDLK_KP_0: final_char = 'p'; break;
                        case SDLK_KP_1: final_char = 'q'; break;
                        case SDLK_KP_2: final_char = 'r'; break;
                        case SDLK_KP_3: final_char = 's'; break;
                        case SDLK_KP_4: final_char = 't'; break;
                        case SDLK_KP_5: final_char = 'u'; break;
                        case SDLK_KP_6: final_char = 'v'; break;
                        case SDLK_KP_7: final_char = 'w'; break;
                        case SDLK_KP_8: final_char = 'x'; break;
                        case SDLK_KP_9: final_char = 'y'; break;
                        case SDLK_KP_PERIOD: final_char = 'n'; break;
                        case SDLK_KP_PLUS: final_char = 'k'; break;
                        case SDLK_KP_MINUS: final_char = 'm'; break;
                        case SDLK_KP_MULTIPLY: final_char = 'j'; break;
                        case SDLK_KP_DIVIDE: final_char = 'o'; break;
                        default: break;
                        }
                        if (final_char != 0) {
                            if (terminal_send_ss3_final(master_fd, mod, final_char) < 0) {
                                running = 0;
                            }
                            handled = 1;
                        }
                    }
                    break;
                default:
                    break;
                }

                if (handled) {
                    terminal_selection_clear();
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
                    continue;
                }
            } else if (event.type == SDL_TEXTINPUT) {
                terminal_input_draw_requested = 1;
                const char *text = event.text.text;
                size_t len = strlen(text);
                if (len > 0u) {
                    SDL_Keymod raw_mod_state = SDL_GetModState();
                    SDL_Keymod mod_state = terminal_normalize_modifiers(raw_mod_state);
                    int altgr_active = terminal_mod_state_has_altgr(raw_mod_state);
                    terminal_selection_clear();
                    if (!altgr_active && (mod_state & KMOD_ALT) != 0 && (mod_state & KMOD_CTRL) == 0) {
                        if (terminal_send_escape_prefix(master_fd) < 0) {
                            running = 0;
                            continue;
                        }
                    }
                    if (terminal_send_bytes(master_fd, text, len) < 0) {
                        running = 0;
                    }
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
                }
            }
        }

        ssize_t bytes_read;
        do {
            bytes_read = read(master_fd, input_buffer, sizeof(input_buffer));
            if (bytes_read > 0) {
                for (ssize_t i = 0; i < bytes_read; i++) {
                    ansi_parser_feed(&parser, &buffer, input_buffer[i]);
                }
                cursor_phase_visible = 1;
                cursor_last_toggle = SDL_GetTicks();
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                running = 0;
                break;
            }
        } while (bytes_read > 0);

        pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid) {
            child_exited = 1;
        }

        if (terminal_cursor_blink_reset_requested) {
            cursor_phase_visible = 1;
            cursor_last_toggle = SDL_GetTicks();
            terminal_cursor_blink_reset_requested = 0;
        }

        Uint32 now = SDL_GetTicks();
        if (terminal_cursor_blink_enabled &&
            cursor_blink_interval > 0u &&
            (Uint32)(now - cursor_last_toggle) >= cursor_blink_interval) {
            cursor_last_toggle = now;
            cursor_phase_visible = cursor_phase_visible ? 0 : 1;
        }
        size_t clamped_scroll_offset = terminal_clamped_scroll_offset(&buffer);
        size_t top_index = 0u;
        size_t bottom_index = 0u;
        terminal_visible_row_range(&buffer, &top_index, &bottom_index);

        size_t cursor_global_index = buffer.history_rows + buffer.cursor_row;
        int cursor_render_visible = (clamped_scroll_offset == 0u) &&
                                    buffer.cursor_visible &&
                                    cursor_phase_visible &&
                                    terminal_cursor_blink_enabled;

        size_t selection_start = 0u;
        size_t selection_end = 0u;
        int selection_has_range = terminal_selection_linear_range(&buffer, &selection_start, &selection_end);

        uint8_t *framebuffer = terminal_framebuffer_pixels;
        int frame_width = terminal_framebuffer_width;
        int frame_height = terminal_framebuffer_height;
        int frame_pitch = frame_width * 4;
        if (!framebuffer || frame_width <= 0 || frame_height <= 0) {
            fprintf(stderr, "Frame buffer unavailable for rendering.\n");
            running = 0;
            break;
        }

        if (terminal_ensure_render_cache(buffer.columns, buffer.rows) != 0) {
            fprintf(stderr, "Failed to prepare terminal render cache.\n");
            running = 0;
            break;
        }

        int full_redraw = terminal_force_full_redraw;
        terminal_force_full_redraw = 0;
        int frame_dirty = 0;
        int shader_timing_enabled = 0;
        int shader_requires_frame = 0;

        int margin_pixels = terminal_margin_pixels;
        if (margin_pixels < 0) {
            margin_pixels = 0;
        }
        if (margin_pixels * 2 > frame_width) {
            margin_pixels = frame_width / 2;
        }
        if (margin_pixels * 2 > frame_height) {
            margin_pixels = frame_height / 2;
        }

        if (terminal_background_dirty) {
            uint32_t margin_color_value = buffer.default_bg;
            uint32_t margin_pixel = terminal_rgba_from_color(margin_color_value);
            for (int py = 0; py < frame_height; py++) {
                uint32_t *row_ptr = (uint32_t *)(framebuffer + (size_t)py * (size_t)frame_pitch);
                for (int px = 0; px < frame_width; px++) {
                    row_ptr[px] = margin_pixel;
                }
            }
            terminal_background_dirty = 0;
            frame_dirty = 1;
        }

        for (size_t row = 0u; row < buffer.rows; row++) {
            size_t global_index = top_index + row;
            const struct terminal_cell *row_cells = terminal_buffer_row_at(&buffer, global_index);
            if (!row_cells) {
                continue;
            }
            for (size_t col = 0u; col < buffer.columns; col++) {
                const struct terminal_cell *cell = &row_cells[col];
                uint32_t ch = cell->ch;
                uint32_t fg = cell->fg;
                uint32_t bg = cell->bg;
                uint8_t style = cell->style;
                if ((style & TERMINAL_STYLE_REVERSE) != 0u) {
                    uint32_t tmp = fg;
                    fg = bg;
                    bg = tmp;
                }
                if ((style & TERMINAL_STYLE_BOLD) != 0u) {
                    fg = terminal_bold_variant(fg);
                }

                int cell_selected = selection_has_range &&
                    terminal_selection_contains_cell(global_index, col, selection_start, selection_end, buffer.columns);
                if (cell_selected) {
                    fg = buffer.default_bg;
                    bg = buffer.default_fg;
                }

                int is_cursor_cell = cursor_render_visible &&
                                     global_index == cursor_global_index &&
                                     col == buffer.cursor_column;
                uint32_t fill_color = bg;
                uint32_t glyph_color = fg;
                if (is_cursor_cell) {
                    fill_color = buffer.cursor_color;
                    glyph_color = bg;
                }

                int dest_x = margin_pixels + (int)(col * (size_t)glyph_width);
                int dest_y = margin_pixels + (int)(row * (size_t)glyph_height);
                int end_x = dest_x + glyph_width;
                int end_y = dest_y + glyph_height;
                if (dest_x < 0) {
                    dest_x = 0;
                }
                if (dest_y < 0) {
                    dest_y = 0;
                }
                if (end_x > frame_width) {
                    end_x = frame_width;
                }
                if (end_y > frame_height) {
                    end_y = frame_height;
                }
                if (dest_x >= end_x || dest_y >= end_y) {
                    continue;
                }

                size_t cache_index = row * buffer.columns + col;
                if (cache_index >= terminal_render_cache_count) {
                    continue;
                }
                struct terminal_render_cache_entry *cache_entry = &terminal_render_cache[cache_index];
                int needs_redraw = full_redraw;
                if (!needs_redraw) {
                    if (cache_entry->ch != ch ||
                        cache_entry->fg != glyph_color ||
                        cache_entry->bg != fill_color ||
                        cache_entry->style != style ||
                        cache_entry->cursor != (uint8_t)is_cursor_cell ||
                        cache_entry->selected != (uint8_t)cell_selected) {
                        needs_redraw = 1;
                    }
                }
                if (!needs_redraw) {
                    continue;
                }

                cache_entry->ch = ch;
                cache_entry->fg = glyph_color;
                cache_entry->bg = fill_color;
                cache_entry->style = style;
                cache_entry->cursor = (uint8_t)is_cursor_cell;
                cache_entry->selected = (uint8_t)cell_selected;
                frame_dirty = 1;

                int cell_width = end_x - dest_x;
                int cell_height = end_y - dest_y;
                uint32_t fill_pixel = terminal_rgba_from_color(fill_color);
                for (int py = 0; py < cell_height; py++) {
                    uint32_t *dst32 = (uint32_t *)(framebuffer +
                                                    (size_t)(dest_y + py) * (size_t)frame_pitch +
                                                    (size_t)dest_x * 4u);
                    for (int px = 0; px < cell_width; px++) {
                        dst32[px] = fill_pixel;
                    }
                }

                if (ch != 0u) {
                    uint32_t glyph_index = psf_font_resolve_glyph(&terminal_font, ch);
                    if (glyph_index >= terminal_font.glyph_count) {
                        glyph_index = 0u;
                    }
                    const uint8_t *glyph_bitmap = terminal_font.glyphs + glyph_index * terminal_font.glyph_size;
                    uint32_t glyph_pixel_value = terminal_rgba_from_color(glyph_color);
                    int glyph_scale = TERMINAL_FONT_SCALE;
                    if (glyph_scale <= 0) {
                        glyph_scale = 1;
                    }
                    for (int py = 0; py < cell_height; py++) {
                        uint32_t src_y = (uint32_t)(py / glyph_scale);
                        if (src_y >= terminal_font.height) {
                            break;
                        }
                        const uint8_t *glyph_row = glyph_bitmap + (size_t)src_y * terminal_font.stride;
                        uint32_t *dst32 = (uint32_t *)(framebuffer +
                                                        (size_t)(dest_y + py) * (size_t)frame_pitch +
                                                        (size_t)dest_x * 4u);
                        for (uint32_t src_x = 0; src_x < terminal_font.width; src_x++) {
                            uint8_t mask = (uint8_t)(0x80u >> (src_x & 7u));
                            if ((glyph_row[src_x / 8u] & mask) == 0u) {
                                continue;
                            }
                            int start_px = (int)(src_x * (uint32_t)glyph_scale);
                            int end_px = start_px + glyph_scale;
                            if (start_px >= cell_width) {
                                break;
                            }
                            if (end_px > cell_width) {
                                end_px = cell_width;
                            }
                            for (int px = start_px; px < end_px; px++) {
                                dst32[px] = glyph_pixel_value;
                            }
                        }
                    }

                    if ((style & TERMINAL_STYLE_UNDERLINE) != 0u) {
                        int underline_y = end_y - 1;
                        if (underline_y >= dest_y) {
                            uint32_t *dst32 = (uint32_t *)(framebuffer +
                                                            (size_t)underline_y * (size_t)frame_pitch +
                                                            (size_t)dest_x * 4u);
                            for (int px = 0; px < cell_width; px++) {
                                dst32[px] = glyph_pixel_value;
                            }
                        }
                    }
                }
            }
        }

        if (terminal_custom_pixel_count > 0u &&
            (terminal_custom_pixels_dirty ||
             (terminal_custom_pixels_active && frame_dirty))) {
            terminal_custom_pixels_apply(framebuffer, frame_width, frame_height);
            frame_dirty = 1;
            terminal_custom_pixels_dirty = 0;
            terminal_custom_pixels_active = 1;
        } else if (terminal_custom_pixels_dirty) {
            frame_dirty = 1;
            terminal_custom_pixels_dirty = 0;
            terminal_custom_pixels_pending_layers = 0u;
            terminal_custom_pixels_active = 0;
        }

        shader_timing_enabled = (terminal_shaders_active() &&
                                 terminal_shader_frame_interval_ms > 0u);
        if (shader_timing_enabled) {
            Uint32 elapsed = now - terminal_shader_last_frame_tick;
            if (elapsed >= terminal_shader_frame_interval_ms) {
                shader_requires_frame = 1;
            }
        }

        int cursor_requires_draw = terminal_cursor_enabled && terminal_cursor_dirty;
        int need_input_draw = terminal_input_draw_requested && !terminal_shaders_active();
        int need_gpu_draw = frame_dirty || shader_requires_frame || cursor_requires_draw || need_input_draw;
        if (need_gpu_draw && terminal_render_frame_interval_ms > 0u) {
            Uint32 since_last_frame = now - terminal_render_last_frame_tick;
            if (since_last_frame < terminal_render_frame_interval_ms) {
                SDL_Delay(terminal_render_frame_interval_ms - since_last_frame);
                now = SDL_GetTicks();
            }
        }
        if (!need_gpu_draw) {
            Uint32 idle_delay_ms = terminal_render_frame_interval_ms > 0u
                ? terminal_render_frame_interval_ms
                : 50u;
            if (terminal_render_frame_interval_ms > 0u) {
                Uint32 since_last_frame = now - terminal_render_last_frame_tick;
                if (since_last_frame < terminal_render_frame_interval_ms) {
                    Uint32 remaining = terminal_render_frame_interval_ms - since_last_frame;
                    if (remaining < idle_delay_ms) {
                        idle_delay_ms = remaining;
                    }
                }
            }
            if (shader_timing_enabled) {
                Uint32 since_last_shader = now - terminal_shader_last_frame_tick;
                if (since_last_shader < terminal_shader_frame_interval_ms) {
                    Uint32 remaining = terminal_shader_frame_interval_ms - since_last_shader;
                    if (remaining < idle_delay_ms) {
                        idle_delay_ms = remaining;
                    }
                } else {
                    idle_delay_ms = 0u;
                }
            }
            if (terminal_cursor_blink_enabled && cursor_blink_interval > 0u) {
                Uint32 since_cursor_toggle = now - cursor_last_toggle;
                if (since_cursor_toggle < cursor_blink_interval) {
                    Uint32 remaining = cursor_blink_interval - since_cursor_toggle;
                    if (remaining < idle_delay_ms) {
                        idle_delay_ms = remaining;
                    }
                } else {
                    idle_delay_ms = 0u;
                }
            }
            if (idle_delay_ms == 0u) {
                idle_delay_ms = 1u;
            }
            SDL_Delay(idle_delay_ms);
            continue;
        }

        if (frame_dirty) {
            if (terminal_upload_framebuffer(framebuffer, frame_width, frame_height) != 0) {
                fprintf(stderr, "Failed to upload framebuffer to GPU.\n");
                running = 0;
                break;
            }
        }

        glClear(GL_COLOR_BUFFER_BIT);
        GLuint source_texture = terminal_gl_texture;
        GLfloat source_texture_width = (GLfloat)terminal_texture_width;
        GLfloat source_texture_height = (GLfloat)terminal_texture_height;
        GLfloat source_input_width = (GLfloat)frame_width;
        GLfloat source_input_height = (GLfloat)frame_height;
        int cursor_composited_into_shader = 0;
        int cursor_ready_for_composition = terminal_cursor_enabled &&
                                           terminal_cursor_texture != 0 &&
                                           terminal_cursor_position_valid;
        if (terminal_shaders_active() && cursor_ready_for_composition) {
            if (terminal_prepare_intermediate_targets(drawable_width, drawable_height) == 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, terminal_gl_framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       terminal_gl_intermediate_textures[1], 0);
                GLenum composition_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (composition_status == GL_FRAMEBUFFER_COMPLETE) {
                    glViewport(0, 0, drawable_width, drawable_height);
                    glClear(GL_COLOR_BUFFER_BIT);

                    glUseProgram(0);
                    glMatrixMode(GL_PROJECTION);
                    glLoadIdentity();
                    glMatrixMode(GL_MODELVIEW);
                    glLoadIdentity();

                    glActiveTexture(GL_TEXTURE0);
                    terminal_bind_texture(terminal_gl_texture);
                    glEnable(GL_TEXTURE_2D);

                    glBegin(GL_TRIANGLE_STRIP);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex2f(-1.0f, -1.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex2f(1.0f, -1.0f);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex2f(-1.0f, 1.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex2f(1.0f, 1.0f);
                    glEnd();

                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    terminal_cursor_render(frame_width, frame_height, drawable_width, drawable_height);
                    glDisable(GL_BLEND);

                    glDisable(GL_TEXTURE_2D);
                    terminal_bind_texture(0);

                    cursor_composited_into_shader = 1;
                    source_texture = terminal_gl_intermediate_textures[1];
                    source_texture_width = (GLfloat)drawable_width;
                    source_texture_height = (GLfloat)drawable_height;
                    source_input_width = (GLfloat)drawable_width;
                    source_input_height = (GLfloat)drawable_height;
                }

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }
        if (terminal_shaders_active()) {
            static int frame_counter = 0;
            int frame_value = frame_counter++;
            int history_resized = 0;
            if (terminal_history_width != drawable_width || terminal_history_height != drawable_height) {
                terminal_history_width = drawable_width;
                terminal_history_height = drawable_height;
                history_resized = 1;
            }

            int multipass_failed = 0;

            for (size_t shader_index = 0; shader_index < terminal_gl_shader_count; shader_index++) {
                struct terminal_gl_shader *shader = &terminal_gl_shaders[shader_index];
                if (!shader || shader->program == 0) {
                    continue;
                }

                int last_pass = (shader_index + 1u == terminal_gl_shader_count);
                GLuint target_texture = 0;
                int using_intermediate = 0;

                if (!last_pass) {
                    if (terminal_prepare_intermediate_targets(drawable_width, drawable_height) != 0) {
                        fprintf(stderr, "Failed to prepare intermediate render targets; skipping remaining shader passes.\n");
                        multipass_failed = 1;
                        last_pass = 1;
                    } else {
                        target_texture = terminal_gl_intermediate_textures[shader_index % 2u];
                        glBindFramebuffer(GL_FRAMEBUFFER, terminal_gl_framebuffer);
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
                        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                        if (status != GL_FRAMEBUFFER_COMPLETE) {
                            fprintf(stderr, "Framebuffer incomplete (0x%04x); skipping remaining shader passes.\n", (unsigned int)status);
                            glBindFramebuffer(GL_FRAMEBUFFER, 0);
                            multipass_failed = 1;
                            last_pass = 1;
                        } else {
                            using_intermediate = 1;
                            glViewport(0, 0, drawable_width, drawable_height);
                            glClear(GL_COLOR_BUFFER_BIT);
                        }
                    }
                }

                if (last_pass && !using_intermediate) {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, drawable_width, drawable_height);
                }

                glUseProgram(shader->program);

                terminal_shader_set_vec2(shader->uniform_output_size,
                                         shader->cached_output_size,
                                         &shader->has_cached_output_size,
                                         (GLfloat)drawable_width,
                                         (GLfloat)drawable_height);
                if (shader->uniform_frame_count >= 0) {
                    glUniform1i(shader->uniform_frame_count, frame_value);
                }
                terminal_shader_set_vec2(shader->uniform_texture_size,
                                         shader->cached_texture_size,
                                         &shader->has_cached_texture_size,
                                         source_texture_width,
                                         source_texture_height);
                terminal_shader_set_vec2(shader->uniform_input_size,
                                         shader->cached_input_size,
                                         &shader->has_cached_input_size,
                                         source_input_width,
                                         source_input_height);

                if (shader->uniform_prev_sampler >= 0) {
                    GLuint history_texture = 0;
                    if (terminal_prepare_shader_history(shader, drawable_width, drawable_height, history_resized) == 0) {
                        history_texture = shader->history_texture;
                        if (source_texture == terminal_gl_texture && shader->history_texture_flipped != 0) {
                            history_texture = shader->history_texture_flipped;
                        }
                    }
                    glActiveTexture(GL_TEXTURE1);
                    terminal_bind_texture(history_texture);
                    glActiveTexture(GL_TEXTURE0);
                }

                glActiveTexture(GL_TEXTURE0);
                terminal_bind_texture(source_texture);

                GLuint vao = (source_texture == terminal_gl_texture) ? shader->quad_vaos[0] : shader->quad_vaos[1];
                int using_vao = 0;
                if (vao != 0) {
                    glBindVertexArray(vao);
                    using_vao = 1;
                } else {
                    static const GLfloat fallback_quad_vertices[16] = {
                        -1.0f, -1.0f, 0.0f, 1.0f,
                         1.0f, -1.0f, 0.0f, 1.0f,
                        -1.0f,  1.0f, 0.0f, 1.0f,
                         1.0f,  1.0f, 0.0f, 1.0f
                    };
                    static const GLfloat fallback_texcoords_cpu[8] = {
                        0.0f, 1.0f,
                        1.0f, 1.0f,
                        0.0f, 0.0f,
                        1.0f, 0.0f
                    };
                    static const GLfloat fallback_texcoords_fbo[8] = {
                        0.0f, 0.0f,
                        1.0f, 0.0f,
                        0.0f, 1.0f,
                        1.0f, 1.0f
                    };
                    if (shader->attrib_vertex >= 0) {
                        glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
                        glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, 0, fallback_quad_vertices);
                    }
                    if (shader->attrib_texcoord >= 0) {
                        const GLfloat *quad_texcoords = (source_texture == terminal_gl_texture)
                            ? fallback_texcoords_cpu
                            : fallback_texcoords_fbo;
                        glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
                        glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, 0, quad_texcoords);
                    }
                }
                if (shader->attrib_color >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_color);
                    glVertexAttrib4f((GLuint)shader->attrib_color, 1.0f, 1.0f, 1.0f, 1.0f);
                }

                glDrawArrays(GL_TRIANGLE_STRIP, 0, terminal_quad_vertex_count);

                if (shader->uniform_prev_sampler >= 0) {
                    terminal_update_shader_history(shader, drawable_width, drawable_height);
                }

                if (using_vao) {
                    glBindVertexArray(0);
                } else {
                    if (shader->attrib_vertex >= 0) {
                        glDisableVertexAttribArray((GLuint)shader->attrib_vertex);
                    }
                    if (shader->attrib_texcoord >= 0) {
                        glDisableVertexAttribArray((GLuint)shader->attrib_texcoord);
                    }
                }

                if (using_intermediate) {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    source_texture = target_texture;
                    source_texture_width = (GLfloat)drawable_width;
                    source_texture_height = (GLfloat)drawable_height;
                    source_input_width = (GLfloat)drawable_width;
                    source_input_height = (GLfloat)drawable_height;
                }

                if (multipass_failed) {
                    break;
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glActiveTexture(GL_TEXTURE0);
            terminal_bind_texture(terminal_gl_texture);
            glEnable(GL_TEXTURE_2D);

            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f, 1.0f);
            glVertex2f(-1.0f, -1.0f);
            glTexCoord2f(1.0f, 1.0f);
            glVertex2f(1.0f, -1.0f);
            glTexCoord2f(0.0f, 0.0f);
            glVertex2f(-1.0f, 1.0f);
            glTexCoord2f(1.0f, 0.0f);
            glVertex2f(1.0f, 1.0f);
            glEnd();

            glDisable(GL_TEXTURE_2D);
            terminal_bind_texture(0);
        }

        if (!cursor_composited_into_shader) {
            terminal_cursor_render(frame_width, frame_height, drawable_width, drawable_height);
        }

        SDL_GL_SwapWindow(window);

        terminal_input_draw_requested = 0;
        terminal_cursor_dirty = 0;

        terminal_render_last_frame_tick = SDL_GetTicks();

        if (shader_timing_enabled && need_gpu_draw) {
            terminal_shader_last_frame_tick = now;
        }

        if (child_exited) {
            running = 0;
        }
    }

    SDL_StopTextInput();
    SDL_ShowCursor(SDL_ENABLE);

    if (!child_exited) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, &status, 0);
    }

    terminal_buffer_free(&buffer);
    terminal_buffer_free(&alternate_buffer);
    terminal_release_gl_resources();
    if (terminal_gl_context_handle) {
        SDL_GL_DeleteContext(terminal_gl_context_handle);
        terminal_gl_context_handle = NULL;
    }
#if BUDOSTACK_HAVE_SDL2
    terminal_shutdown_audio();
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();

    terminal_free_requested_shaders();
    free_font(&terminal_font);
    close(master_fd);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

#else

int main(void) {
    fprintf(stderr, "BUDOSTACK terminal requires SDL2 development headers to build.\n");
    fprintf(stderr, "Please install SDL2 and rebuild to use this application.\n");
    return EXIT_FAILURE;
}

#endif
