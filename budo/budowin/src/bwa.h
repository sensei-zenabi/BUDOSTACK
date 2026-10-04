#ifndef BUDOWIN_BWA_H
#define BUDOWIN_BWA_H

/*
 * BUDOWIN native application ABI.
 *
 * In this native port, .BWA files are POSIX ELF shared libraries exposing
 * bwa_entry. Rebuild DOS/DJGPP modules from source for the target platform.
 * The callbacks and host services retain the upstream BWA 1.8 interface.
 */

#define BWA_ABI_MAJOR 1
#define BWA_ABI_MINOR 8

#define BWA_NAME_LEN 32
#define BWA_ID_LEN 16

#define BWA_FLAG_NONE       0U
#define BWA_FLAG_SINGLETON  1U
#define BWA_FLAG_LAUNCHER   2U

#define BWA_HOST_APP_EXPLORER 1
#define BWA_HOST_APP_EDITOR   2
#define BWA_HOST_APP_TERMINAL 3

/* Stable system color roles.  Applications should query these through
 * BwaHostApi instead of depending on VGA palette indices. */
#define BUDO_SYS_COLOR_DESKTOP        0
#define BUDO_SYS_COLOR_TEXT           1
#define BUDO_SYS_COLOR_SHADOW         2
#define BUDO_SYS_COLOR_MIDGRAY        3
#define BUDO_SYS_COLOR_HIGHLIGHT      4
#define BUDO_SYS_COLOR_FACE           5
#define BUDO_SYS_COLOR_TITLE_ACTIVE   6
#define BUDO_SYS_COLOR_TITLE_TEXT     7
#define BUDO_SYS_COLOR_ACCENT         8
#define BUDO_SYS_COLOR_COUNT          9

/* Stable UI geometry roles. */
#define BUDO_SYS_METRIC_WINDOW_BORDER    0
#define BUDO_SYS_METRIC_TITLE_HEIGHT     1
#define BUDO_SYS_METRIC_TITLE_TEXT_Y     2
#define BUDO_SYS_METRIC_TITLE_BUTTON_W   3
#define BUDO_SYS_METRIC_TITLE_BUTTON_H   4
#define BUDO_SYS_METRIC_MENU_HEIGHT      5
#define BUDO_SYS_METRIC_STATUS_HEIGHT    6
#define BUDO_SYS_METRIC_CHAR_WIDTH       7
#define BUDO_SYS_METRIC_LINE_HEIGHT      8
#define BUDO_SYS_METRIC_COUNT            9

#define BUDO_WINDOW_BUTTON_MINIMIZE 0x01U
#define BUDO_WINDOW_BUTTON_MAXIMIZE 0x02U
#define BUDO_WINDOW_BUTTON_CLOSE    0x04U

#define BUDO_WINDOW_DEFAULT_BUTTONS \
    (BUDO_WINDOW_BUTTON_MINIMIZE | \
     BUDO_WINDOW_BUTTON_MAXIMIZE | \
     BUDO_WINDOW_BUTTON_CLOSE)

typedef struct BwaHostApi {
    unsigned short abi_major;
    unsigned short abi_minor;

    void (*fill_rect)(int x, int y, int w, int h, unsigned char color);
    void (*draw_rect)(int x, int y, int w, int h, unsigned char color);
    void (*draw_text)(int x, int y, const char *text,
                      unsigned char color, int max_chars);
    int (*point_in_rect)(int px, int py, int x, int y, int w, int h);
    int (*launch_host_app)(int app_id);
    int (*get_file_association_count)(void);
    int (*get_file_association)(int index,
                                char *extension,
                                unsigned int extension_size,
                                char *app_id,
                                unsigned int app_id_size);
    int (*set_file_association)(const char *extension,
                                const char *app_id);
    int (*get_file_app_count)(void);
    int (*get_file_app)(int index,
                        char *app_id,
                        unsigned int app_id_size,
                        char *name,
                        unsigned int name_size);
    int (*close_active_app)(void);

    /* ABI 1.4: semantic UI services. */
    unsigned char (*get_system_color)(int role);
    int (*get_system_metric)(int metric);
    void (*draw_standard_window)(int x, int y, int w, int h,
                                 const char *title,
                                 unsigned int button_flags);
    void (*draw_standard_button)(int x, int y, int w, int h,
                                 const char *label, int pressed);
    void (*draw_sunken_panel)(int x, int y, int w, int h,
                              unsigned char fill_color);

    /* ABI 1.5: one host-managed top-level window per native app. */
    int (*window_create)(int x, int y, int w, int h,
                         const char *title,
                         unsigned int button_flags);
    int (*window_get_rect)(int *x, int *y, int *w, int *h);
    int (*window_get_client_rect)(int *x, int *y, int *w, int *h);
    int (*window_is_maximized)(void);
    int (*window_close)(void);

    /* ABI 1.6 managed-window refinements. */
    int (*window_set_min_size)(int min_w, int min_h);
    int (*window_set_title)(const char *title);

    /* ABI 1.7: binary file services safe for DXE modules. */
    int (*file_read_all)(const char *path,
                         unsigned char *buffer,
                         unsigned int capacity,
                         unsigned int *size_out);
    int (*file_write_all)(const char *path,
                          const unsigned char *buffer,
                          unsigned int size);

    /* ABI 1.8: host-owned heap allocation for DXE modules. */
    void *(*memory_alloc)(unsigned int size);
    void (*memory_free)(void *ptr);
} BwaHostApi;

typedef struct BwaAppCallbacks {
    int (*open)(void);
    void (*draw)(void);
    int (*mouse_down)(int x, int y, int buttons);
    int (*key)(int key);
    void (*close)(void);
    void (*draw_icon)(int x, int y);
    int (*open_file)(const char *path);

    /* ABI 1.6: continuous pointer input for interactive applications. */
    int (*mouse_move)(int x, int y, int buttons);
    int (*mouse_up)(int x, int y, int buttons);
} BwaAppCallbacks;

typedef struct BwaAppDefinition {
    int runtime_id;
    const char *app_id;
    const char *name;
    unsigned long flags;
    BwaAppCallbacks callbacks;
} BwaAppDefinition;

typedef int (*BwaEntryPoint)(const BwaHostApi *host,
                             BwaAppDefinition *app);

#endif
