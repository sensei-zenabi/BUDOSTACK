#ifndef BUDOWIN_BWA_H
#define BUDOWIN_BWA_H

/*
 * BUDOWIN native application ABI.
 *
 * In this native port, .BWA files are POSIX ELF shared libraries exposing
 * bwa_entry. Rebuild DOS/DJGPP modules from source for the target platform.
 * Services added after BWA 1.8 are appended to preserve existing field offsets.
 */

#define BWA_ABI_MAJOR 1
#define BWA_ABI_MINOR 15

#define BWA_NAME_LEN 32
#define BWA_ID_LEN 16

#define BWA_FLAG_NONE       0U
/* Legacy flag retained for ABI compatibility; launchers create new instances. */
#define BWA_FLAG_SINGLETON  1U
#define BWA_FLAG_LAUNCHER   2U

#define BWA_HOST_APP_EXPLORER 1
#define BWA_HOST_APP_EDITOR   2
#define BWA_HOST_APP_TERMINAL 3
#define BWA_HOST_APP_READER 4
#define BWA_HOST_APP_HELP 5
#define BWA_HOST_APP_RECYCLE_BIN 6

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
#define BUDO_SYS_COLOR_TITLE_INACTIVE 9
#define BUDO_SYS_COLOR_SURFACE 10
#define BUDO_SYS_COLOR_FRAME 11
#define BUDO_SYS_COLOR_HOVER 12
#define BUDO_SYS_COLOR_PRESSED 13
#define BUDO_SYS_COLOR_DESKTOP_TEXT 14
#define BUDO_SYS_COLOR_TERMINAL_BG 15
#define BUDO_SYS_COLOR_TERMINAL_TEXT 16
#define BUDO_SYS_COLOR_FILE_ICON 17
#define BUDO_SYS_COLOR_FOLDER_ICON 18
#define BUDO_SYS_COLOR_CURSOR_FILL 19
#define BUDO_SYS_COLOR_CURSOR_OUTLINE 20
#define BUDO_SYS_COLOR_STATUS_BG 21
#define BUDO_SYS_COLOR_STATUS_TEXT 22
#define BUDO_SYS_COLOR_SELECTION_BG 23
#define BUDO_SYS_COLOR_SELECTION_TEXT 24
#define BUDO_SYS_COLOR_COUNT 25

#define BUDO_BUTTON_PRESSED 1U
#define BUDO_BUTTON_DISABLED 2U
#define BUDO_BUTTON_FOCUSED 4U
#define BUDO_BUTTON_DEFAULT 8U
#define BUDO_BUTTON_IMMEDIATE 16U
#define BUDO_CURSOR_ARROW 0
#define BUDO_CURSOR_TEXT 1
#define BUDO_CURSOR_RESIZE 2
#define BUDO_CURSOR_CROSSHAIR 3
#define BUDO_CURSOR_BUSY 4
#define BUDO_CURSOR_RESIZE_EW 6
#define BUDO_CURSOR_RESIZE_NS 7
#define BUDO_CURSOR_RESIZE_NESW 8
#define BUDO_CURSOR_OVERLAY 0x100

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

#define BUDO_FILE_OPEN 1
#define BUDO_FILE_SAVE_AS 2
#define BUDO_CONFIRM_SAVE 1
#define BUDO_CONFIRM_OVERWRITE 2
#define BUDO_RESPONSE_CANCEL 0
#define BUDO_RESPONSE_SAVE 1
#define BUDO_RESPONSE_DISCARD 2

/* ABI 1.15: at most eight notes, each 1..1000 ms, total <=2000 ms.
 * Frequencies 0..4000 Hz; zero is a rest. */
typedef struct BwaTone {
    unsigned short frequency_hz;
    unsigned short duration_ms;
} BwaTone;

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

    /* ABI 1.5: one host-managed top-level window per native instance. */
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

    /* ABI 1.9: exact 24-bit RGB, independent of the system palette.
     * rgb is 0x00RRGGBB; the rectangle is clipped to the screen. */
    void (*fill_rect_rgb)(int x, int y, int w, int h, unsigned int rgb);
    /* ABI 1.10: shared asynchronous desktop dialogs. Results are delivered
     * to file_selected / confirm_result on the requesting application. */
    int (*file_dialog)(int mode, const char *initial_path);
    int (*confirm_dialog)(int kind, const char *title, const char *message);
    void (*get_pointer_state)(int *x, int *y, int *buttons);
    /* ABI 1.11: Shift (0x03) and Ctrl (0x04) for shared item selection. */
    unsigned int (*get_keyboard_modifiers)(void);
    /* ABI 1.12: explicit button states and draw-time contextual regions.
     * Immediate buttons (scroll arrows) bypass release-to-activate capture.
     * Regions are rebuilt on each draw; coordinates are screen-relative. */
    void (*draw_button_state)(int x, int y, int w, int h,
                              const char *label, unsigned int state);
    void (*pointer_region)(int x, int y, int w, int h, int cursor,
                            const char *tooltip);
    unsigned long long (*get_time_ms)(void);
    /* ABI 1.13: persistent keyboard layout and association removal.
     * Layout: 0 = ENG, 1 = Finnish/Swedish NORD (default).
     * set_file_association also creates new extensions. */
    int (*get_keyboard_layout)(void);
    int (*set_keyboard_layout)(int nordic);
    int (*delete_file_association)(const char *extension);
    /* ABI 1.14: read-only documents and persistent UI palette roles. */
    int (*open_reader_file)(const char *path);
    int (*set_system_color)(int role, int palette_index);
    int (*reset_system_colors)(void);
    /* Queues asynchronous PC-speaker-style audio through the terminal host.
     * Replaces the current effect; count=0 stops. Zero means unavailable/error. */
    int (*play_tones)(const BwaTone *tones, unsigned int count);
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
    /* ABI 1.10. Zero permits close; request_close returns nonzero to permit
     * it, or zero while the app handles a save confirmation. */
    int (*request_close)(void);
    int (*file_selected)(int mode, const char *path);
    void (*confirm_result)(int kind, int response);
} BwaAppDefinition;

typedef int (*BwaEntryPoint)(const BwaHostApi *host,
                             BwaAppDefinition *app);

#endif
