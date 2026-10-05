#define _POSIX_C_SOURCE 200809L
#include "../lib/budo_gfx.h"
#include "../budo/DHERO/DATA.H"
#include "../budo/DHERO/PCX.H"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* This sandbox permits socketpair but not socket(AF_UNIX). Replace only
 * endpoint establishment; packets, ancillary FD transfer, poll, mmap and
 * disconnect handling run through the production implementation unchanged.
 */
static int endpoints[2];
static int socket_calls;
static int accepted;

static int test_socketpair_mode(void) {
    const char *value = getenv("BUDO_GFX_TEST_SOCKETPAIR");
    return value && strcmp(value, "1") == 0;
}

static int test_socket(int domain, int type, int protocol) {
    assert(domain == AF_UNIX && type == SOCK_SEQPACKET && protocol == 0);
    if (!test_socketpair_mode()) {
        return socket(domain, type, protocol);
    }
    return dup(endpoints[socket_calls++ == 0 ? 0 : 1]);
}
static int test_bind(int fd, const struct sockaddr *address, socklen_t length) {
    return test_socketpair_mode() ? 0 : bind(fd, address, length);
}
static int test_listen(int fd, int backlog) {
    return test_socketpair_mode() ? 0 : listen(fd, backlog);
}
static int test_connect(int fd, const struct sockaddr *address, socklen_t length) {
    return test_socketpair_mode() ? 0 : connect(fd, address, length);
}
static int test_accept(int fd, struct sockaddr *address, socklen_t *length) {
    if (!test_socketpair_mode()) {
        return accept(fd, address, length);
    }
    if (accepted++) {
        errno = EAGAIN;
        return -1;
    }
    return dup(fd);
}
#define socket test_socket
#define bind test_bind
#define listen test_listen
#define connect test_connect
#define accept test_accept
#include "../lib/budo_gfx.c"
#undef socket
#undef bind
#undef listen
#undef connect
#undef accept

static void test_pause(void) {
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}

static struct budo_gfx_host *test_host(void) {
    socket_calls = 0;
    accepted = 0;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, endpoints) == 0);
    struct budo_gfx_host *host = NULL;
    assert(budo_gfx_host_open(&host) == 0);
    assert(setenv("BUDOSTACK_GFX_SOCKET", budo_gfx_host_path(host), 1) == 0);
    return host;
}


static void send_key(struct budo_gfx_host *host, int key, int scan) {
    struct budo_gfx_event event = {0};
    event.type = BUDO_GFX_KEY_DOWN;
    event.key = key;
    event.scancode = scan;
    budo_gfx_host_event(host, &event);
    event.type = BUDO_GFX_KEY_UP;
    budo_gfx_host_event(host, &event);
}

static void test_game(const char *binary, const char *preload, int editor) {
    struct budo_gfx_host *host = test_host();
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        char fd[32];
        snprintf(fd, sizeof(fd), "%d", endpoints[1]);
        assert(setenv("BUDO_TEST_FD", fd, 1) == 0);
        assert(setenv("LD_PRELOAD", preload, 1) == 0);
        assert(chdir("/") == 0); /* Assets must resolve from the executable. */
        if (editor) execl(binary, binary, "--editor", (char *)NULL);
        else execl(binary, binary, (char *)NULL);
        _exit(127);
    }
    close(endpoints[0]);
    close(endpoints[1]);
    int frames = 0, stage = 0, status = 0;
    uint64_t previous = 0;
    int changes = 0;
    for (int tick = 0; tick < 14000; ++tick) {
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            int w, h, dirty;
            const uint8_t *pixels = budo_gfx_host_pixels(host, &w, &h, &dirty);
            assert(w == 320 && h == 200);
            if (dirty) {
                uint64_t hash = 1469598103934665603ULL;
                int nonblack = 0;
                for (size_t p = 0; p < (size_t)w * h * 4u; ++p) {
                    hash = (hash ^ pixels[p]) * 1099511628211ULL;
                    if (p % 4u != 3u && pixels[p]) ++nonblack;
                }
                if (nonblack == 0) {
                    test_pause();
                    continue;
                }
                if (hash != previous) ++changes;
                previous = hash;
                ++frames;
            }
            if (editor) {
                if (tick > 200 && stage == 0) { send_key(host, 0, 78); ++stage; }
                if (tick > 400 && stage == 1) { send_key(host, 0, 58); ++stage; }
                if (tick > 600 && stage == 2) { send_key(host, 27, 41); ++stage; }
                if (tick > 800 && stage == 3) { send_key(host, 27, 41); ++stage; }
            } else {
                if (tick > 5500 && stage == 0) { send_key(host, 0, 81); ++stage; }
                if (tick > 5700 && stage == 1) { send_key(host, 13, 40); ++stage; }
                if (tick > 5900 && stage == 2) { send_key(host, 'e', 8); ++stage; }
                if (tick > 6100 && stage == 3) { send_key(host, 27, 41); ++stage; }
                if (tick > 6300 && stage == 4) { send_key(host, 27, 41); ++stage; }
                if (tick > 6500 && stage == 5) { send_key(host, 0, 82); ++stage; }
                if (tick > 6700 && stage == 6) { send_key(host, 13, 40); ++stage; }
                if (tick > 7100 && stage == 7) { send_key(host, 32, 44); ++stage; }
                if (tick > 7300 && stage == 8) { send_key(host, 27, 41); ++stage; }
                if (tick > 7500 && stage == 9) { send_key(host, 27, 41); ++stage; }
                if (tick > 9500 && stage == 10) { send_key(host, 27, 41); ++stage; }
            }
        }
        pid_t result = waitpid(child, &status, WNOHANG);
        assert(result >= 0);
        if (result == child) {
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            assert(stage == (editor ? 4 : 11));
            assert(frames >= (editor ? 4 : 10) && changes >= (editor ? 4 : 7));
            budo_gfx_host_close(host);
            printf("Dungeon Hero %s: %d frames, %d visual changes passed.\n",
                   editor ? "editor" : "menu/options/editor/campaign/inventory/exit", frames, changes);
            return;
        }
        test_pause();
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    assert(!"Dungeon Hero test timeout");
}


/* Asset checks use the production parsers; rendering is exercised by the child. */
void dhero_mode(int mode) { (void)mode; }
void dhero_dac(int port, int value) { (void)port; (void)value; }
void dhero_pixels_write(const void *source, size_t size, unsigned long address) {
    (void)source; (void)size; (void)address;
}
void dhero_pixels_read(unsigned long address, size_t size, void *dest) {
    (void)address;
    memset(dest, 0, size);
}

static void test_assets(void) {
    EnemyDefs enemies;
    ItemDefs items;
    SheetSpec tiles, characters;
    PCXImage image;
    assert(data_load_enemy_defs("budo/DHERO/DATA/GAME/ENEMIES.DAT", &enemies));
    assert(data_load_item_defs("budo/DHERO/DATA/GAME/ITEMS.DAT", &items));
    assert(data_load_sheet("budo/DHERO/DATA/GFX/TILES/TILE000.DAT", &tiles));
    assert(data_load_sheet("budo/DHERO/DATA/GFX/SPRITES/CHAR000.DAT", &characters));
    assert(tiles.cell_width == 16 && tiles.cell_height == 16);
    assert(characters.cell_width == 16 && characters.cell_height == 16);
    assert(pcx_load_image("budo/DHERO/DATA/GFX/FULL/FULL000.pcx", &image));
    assert(image.width == 320 && image.height == 200);
    pcx_free_image(&image);
    assert(pcx_load_image("budo/DHERO/DATA/GFX/TILES/TILE000.pcx", &image));
    for (int i = 0; i < tiles.entry_count; ++i) {
        assert((tiles.entries[i].column + 1) * 16 <= image.width);
        assert((tiles.entries[i].row + 1) * 16 <= image.height);
    }
    pcx_free_image(&image);
    assert(pcx_load_image("budo/DHERO/DATA/GFX/SPRITES/CHAR000.PCX", &image));
    for (int i = 0; i < characters.entry_count; ++i) {
        assert((characters.entries[i].column + 1) * 16 <= image.width);
        assert((characters.entries[i].row + 1) * 16 <= image.height);
    }
    pcx_free_image(&image);
    for (int number = 1; number <= 10; ++number) {
        char path[128];
        LevelData level;
        snprintf(path, sizeof(path), "budo/DHERO/DATA/GFX/MAP/LEVEL%d.DAT", number);
        assert(data_load_level(path, &level));
        assert(data_level_is_walkable(&level, level.start_x, level.start_y));
        assert(data_find_sheet_entry(&characters, "HERO") >= 0);
        assert(level.exit_count > 0);
        for (int i = 0; i < level.legend_count; ++i)
            assert(data_find_sheet_entry(&tiles, level.legend[i].tile) >= 0);
        for (int i = 0; i < level.enemy_count; ++i) {
            assert(data_find_enemy_def(&enemies, level.enemies[i].type) >= 0);
            assert(data_find_sheet_entry(&characters, level.enemies[i].type) >= 0);
        }
        for (int i = 0; i < level.item_count; ++i)
            assert(data_find_item_def(&items, level.items[i].item) >= 0);
        for (int i = 0; i < level.chest_count; ++i)
            assert(strcmp(level.chests[i].item, "NONE") == 0 ||
                   data_find_item_def(&items, level.chests[i].item) >= 0);
    }
    puts("Dungeon Hero: all PCX images, sheets, definitions and ten levels passed.");
}

int main(int argc, char **argv) {
    assert(argc == 3);
    test_assets();
    test_game(argv[1], argv[2], 1);
    test_game(argv[1], argv[2], 0);
    return 0;
}
