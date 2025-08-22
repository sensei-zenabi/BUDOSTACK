/* share.c - A basic LAN file-sharing and collaboration app with diagnostic messages.
 *
 * Design principles:
 * - Server mode: "share [ -local ] <file_to_share>" monitors the given file for changes using inotify.
 *   When a change is detected, the file is read and its full content is broadcast via UDP.
 *   If -local is specified, the destination is set to 127.0.0.1 (for testing on one machine);
 *   otherwise it uses 255.255.255.255.
 * - Clients:
 *   - Listen-only mode ("share [ -local ] -listen") receives UDP packets and writes the update
 *     into a local file called "shared_file". If the file does not exist, it is created.
 *   - Collaborative mode ("share [ -local ] -collab") both receives updates and monitors its
 *     local "shared_file". If the file does not exist, it is created. When the file is changed
 *     (e.g., by an external editor that autosaves), the client sends its update over UDP.
 *     To avoid echoing, an inotify event following a network update is ignored.
 *
 * Diagnostic messages have been added so that every significant event and modification is printed.
 *
 * All code is written in plain C (compiled with -std=c11) using POSIX functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/inotify.h>

#define SHARE_PORT 12345
#define BUF_SIZE 65536  // maximum UDP packet size

/* Packet structure:
   - seq_num: sequence number (32-bit)
   - timestamp: update timestamp in microseconds (64-bit)
   - data_length: length of file content (32-bit)
   - data: the file content
*/
struct share_packet {
    uint32_t seq_num;
    uint64_t timestamp;
    uint32_t data_length;
    char data[];
};

/* Global flag to indicate local testing mode.
   If local_mode is set to 1, the destination address for UDP is set to 127.0.0.1.
*/
int local_mode = 0;

/* Helper functions to convert 64-bit integers to network byte order and vice versa.
   (Since there is no standard htonll, we build it from htonl.)
*/
uint64_t htonll(uint64_t value) {
    uint32_t high = htonl((uint32_t)(value >> 32));
    uint32_t low = htonl((uint32_t)(value & 0xFFFFFFFF));
    return ((uint64_t)low << 32) | high;
}

uint64_t ntohll(uint64_t value) {
    uint32_t low = ntohl((uint32_t)(value >> 32));
    uint32_t high = ntohl((uint32_t)(value & 0xFFFFFFFF));
    return ((uint64_t)high << 32) | low;
}

/* Return current timestamp in microseconds */
uint64_t get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000000LL) + tv.tv_usec;
}

/* Read entire file into a malloc'ed buffer.
   The file size is returned via size_out.
*/
char* read_file(const char *filename, uint32_t *size_out) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Diagnostic: Failed to open file %s for reading.\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc(size);
    if (!buffer) { 
        fclose(f);
        fprintf(stderr, "Diagnostic: Memory allocation failed for file %s.\n", filename);
        return NULL;
    }
    if (fread(buffer, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Diagnostic: Failed to read file %s.\n", filename);
    }
    fclose(f);
    *size_out = (uint32_t)size;
    fprintf(stdout, "Diagnostic: Read file %s, size=%u bytes.\n", filename, *size_out);
    return buffer;
}

/* Write buffer content into file */
int write_file(const char *filename, const char *buffer, uint32_t size) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Diagnostic: Failed to open file %s for writing.\n", filename);
        return -1;
    }
    if (fwrite(buffer, 1, size, f) != size) {
        fprintf(stderr, "Diagnostic: Failed to write file %s.\n", filename);
        fclose(f);
        return -1;
    }
    fclose(f);
    fprintf(stdout, "Diagnostic: Wrote %u bytes to file %s.\n", size, filename);
    return 0;
}

/* Print usage information */
void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  As server: %s [ -local ] <file_to_share>\n", prog);
    fprintf(stderr, "  As client listen: %s [ -local ] -listen\n", prog);
    fprintf(stderr, "  As client collab: %s [ -local ] -collab\n", prog);
}

/* Global flag for collaborative clients to ignore an inotify event immediately after
   a network update to avoid echoing the received update.
*/
int ignore_next_event = 0;

/* SERVER MODE:
   - Monitors the given file for modifications (using inotify).
   - When the file changes, reads it and broadcasts its full content.
   - Also listens for UDP packets from collab clients; if one is received,
     the file is updated and then the update is re-broadcast.
*/
int server_mode(const char *filename) {
    int seq_num = 0;
    fprintf(stdout, "Diagnostic: Starting server mode for file %s.\n", filename);
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Bind to all interfaces */
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SHARE_PORT);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    fprintf(stdout, "Diagnostic: UDP socket bound to port %d.\n", SHARE_PORT);
    
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("inotify_init");
        return -1;
    }
    int wd = inotify_add_watch(inotify_fd, filename, IN_MODIFY);
    if (wd < 0) {
        perror("inotify_add_watch");
        return -1;
    }
    fprintf(stdout, "Diagnostic: Added inotify watch on file %s (watch descriptor=%d).\n", filename, wd);
    
    /* Set non-blocking mode */
    fcntl(sock, F_SETFL, O_NONBLOCK);
    fcntl(inotify_fd, F_SETFL, O_NONBLOCK);
    
    /* Broadcast destination */
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SHARE_PORT);
    if (local_mode) {
        dest.sin_addr.s_addr = inet_addr("127.0.0.1");
        fprintf(stdout, "Diagnostic: Local mode enabled; using loopback address 127.0.0.1 as destination.\n");
    } else {
        dest.sin_addr.s_addr = inet_addr("255.255.255.255");
        fprintf(stdout, "Diagnostic: Using broadcast address 255.255.255.255 as destination.\n");
    }
    
    fd_set readfds;
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(inotify_fd, &readfds);
        int maxfd = sock > inotify_fd ? sock : inotify_fd;
        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }
        
        /* Process local file change events */
        if (FD_ISSET(inotify_fd, &readfds)) {
            char buf[1024];
            int len = read(inotify_fd, buf, sizeof(buf));
            if (len > 0) {
                fprintf(stdout, "Diagnostic: Detected inotify event on file %s.\n", filename);
                uint32_t file_size;
                char *file_content = read_file(filename, &file_size);
                if (file_content) {
                    uint32_t packet_size = sizeof(struct share_packet) + file_size;
                    char *packet = malloc(packet_size);
                    if (packet) {
                        struct share_packet *p = (struct share_packet *)packet;
                        p->seq_num = htonl(seq_num++);
                        p->timestamp = htonll(get_timestamp());
                        p->data_length = htonl(file_size);
                        memcpy(p->data, file_content, file_size);
                        sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest, sizeof(dest));
                        fprintf(stdout, "Diagnostic: Broadcasted file update (seq=%d, size=%u bytes).\n", seq_num - 1, file_size);
                        free(packet);
                    }
                    free(file_content);
                }
            }
        }
        
        /* Process incoming UDP packets from collab clients */
        if (FD_ISSET(sock, &readfds)) {
            char buf[BUF_SIZE];
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);
            int r = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&sender, &sender_len);
            if (r > 0) {
                fprintf(stdout, "Diagnostic: Received UDP packet (%d bytes) from %s:%d.\n", r,
                        inet_ntoa(sender.sin_addr), ntohs(sender.sin_port));
                if (r < (int)sizeof(struct share_packet))
                    continue;
                struct share_packet *p = (struct share_packet *)buf;
                uint32_t r_seq = ntohl(p->seq_num);
                uint32_t data_len = ntohl(p->data_length);
                if (r != (int)(sizeof(struct share_packet) + data_len))
                    continue;
                if (write_file(filename, p->data, data_len) == 0) {
                    fprintf(stdout, "Diagnostic: Applied collab update (seq=%u, size=%u bytes) from network.\n", r_seq, data_len);
                    /* After applying the update, re-broadcast it */
                    uint32_t packet_size = sizeof(struct share_packet) + data_len;
                    char *packet = malloc(packet_size);
                    if (packet) {
                        struct share_packet *np = (struct share_packet *)packet;
                        np->seq_num = htonl(seq_num++);
                        np->timestamp = htonll(get_timestamp());
                        np->data_length = htonl(data_len);
                        memcpy(np->data, p->data, data_len);
                        sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest, sizeof(dest));
                        fprintf(stdout, "Diagnostic: Re-broadcasted collab update (new seq=%d, size=%u bytes).\n", seq_num - 1, data_len);
                        free(packet);
                    }
                }
            }
        }
    }
    
    close(inotify_fd);
    close(sock);
    return 0;
}

/* CLIENT MODE (used for both -listen and -collab):
   - Listens on the UDP port for updates.
   - Writes the received file content into "shared_file".
   - If the file does not exist, it is created.
   - In collab mode, also monitors "shared_file" locally for modifications
     (using inotify) and sends any local changes over UDP.
*/
int client_mode(int collab) {
    const char *filename = "shared_file";
    fprintf(stdout, "Diagnostic: Starting client mode (%s).\n", collab ? "collaborative" : "listen-only");
    
    /* Ensure the shared file exists */
    FILE *f = fopen(filename, "ab");
    if (f == NULL) {
        perror("fopen");
        return -1;
    }
    fclose(f);
    fprintf(stdout, "Diagnostic: Ensured shared file %s exists.\n", filename);
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SHARE_PORT);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    fprintf(stdout, "Diagnostic: Client UDP socket bound to port %d.\n", SHARE_PORT);
    
    int inotify_fd = -1;
    int wd = -1;
    if (collab) {
        inotify_fd = inotify_init();
        if (inotify_fd < 0) {
            perror("inotify_init");
            return -1;
        }
        wd = inotify_add_watch(inotify_fd, filename, IN_MODIFY);
        if (wd < 0) {
            perror("inotify_add_watch");
            return -1;
        }
        fcntl(inotify_fd, F_SETFL, O_NONBLOCK);
        fprintf(stdout, "Diagnostic: Added inotify watch on shared file %s (watch descriptor=%d).\n", filename, wd);
    }
    
    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SHARE_PORT);
    if (local_mode) {
        dest.sin_addr.s_addr = inet_addr("127.0.0.1");
        fprintf(stdout, "Diagnostic: Client local mode enabled; using 127.0.0.1 as destination for local updates.\n");
    } else {
        dest.sin_addr.s_addr = inet_addr("255.255.255.255");
        fprintf(stdout, "Diagnostic: Client using broadcast address 255.255.255.255 for local updates.\n");
    }
    
    fd_set readfds;
    int seq = 0;
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        int maxfd = sock;
        if (collab && inotify_fd >= 0) {
            FD_SET(inotify_fd, &readfds);
            if (inotify_fd > maxfd)
                maxfd = inotify_fd;
        }
        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }
        
        /* Process incoming UDP updates */
        if (FD_ISSET(sock, &readfds)) {
            char buf[BUF_SIZE];
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);
            int r = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&sender, &sender_len);
            if (r > 0) {
                fprintf(stdout, "Diagnostic: Client received UDP packet (%d bytes) from %s:%d.\n", r,
                        inet_ntoa(sender.sin_addr), ntohs(sender.sin_port));
                if (r < (int)sizeof(struct share_packet))
                    continue;
                struct share_packet *p = (struct share_packet *)buf;
                uint32_t r_seq = ntohl(p->seq_num);
                uint32_t data_len = ntohl(p->data_length);
                if (r != (int)(sizeof(struct share_packet) + data_len))
                    continue;
                if (write_file(filename, p->data, data_len) == 0) {
                    fprintf(stdout, "Diagnostic: Client applied update (seq=%u, size=%u bytes) from network.\n", r_seq, data_len);
                    if (collab)
                        ignore_next_event = 1;
                }
            }
        }
        
        /* In collaborative mode, check for local file changes */
        if (collab && FD_ISSET(inotify_fd, &readfds)) {
            char buf[1024];
            int len = read(inotify_fd, buf, sizeof(buf));
            if (len > 0) {
                fprintf(stdout, "Diagnostic: Detected inotify event on shared file %s.\n", filename);
                if (ignore_next_event) {
                    fprintf(stdout, "Diagnostic: Ignoring inotify event due to recent network update.\n");
                    ignore_next_event = 0;
                } else {
                    uint32_t file_size;
                    char *file_content = read_file(filename, &file_size);
                    if (file_content) {
                        uint32_t packet_size = sizeof(struct share_packet) + file_size;
                        char *packet = malloc(packet_size);
                        if (packet) {
                            struct share_packet *p = (struct share_packet *)packet;
                            p->seq_num = htonl(seq++);
                            p->timestamp = htonll(get_timestamp());
                            p->data_length = htonl(file_size);
                            memcpy(p->data, file_content, file_size);
                            sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest, sizeof(dest));
                            fprintf(stdout, "Diagnostic: Client sent local update (seq=%d, size=%u bytes).\n", seq - 1, file_size);
                            free(packet);
                        }
                        free(file_content);
                    }
                }
            }
        }
    }
    
    if (collab && inotify_fd >= 0) {
        inotify_rm_watch(inotify_fd, wd);
        close(inotify_fd);
    }
    close(sock);
    return 0;
}

/* Main entry point:
   - The optional "-local" flag forces the use of the loopback interface (127.0.0.1)
     for testing on one machine.
   - If the next argument is "-listen", run in listen-only client mode.
   - If "-collab", run in collaborative client mode.
   - Otherwise, assume server mode and treat the argument as the file to share.
*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    
    int arg_index = 1;
    if (strcmp(argv[arg_index], "-local") == 0) {
        local_mode = 1;
        fprintf(stdout, "Diagnostic: Running in local test mode.\n");
        arg_index++;
        if (argc < arg_index + 1) {
            usage(argv[0]);
            return 1;
        }
    }
    
    if (strcmp(argv[arg_index], "-listen") == 0) {
        printf("Starting in listen-only client mode. Shared file will be saved as 'shared_file'.\n");
        return client_mode(0);
    } else if (strcmp(argv[arg_index], "-collab") == 0) {
        printf("Starting in collaborative client mode. Shared file will be saved as 'shared_file'.\n");
        return client_mode(1);
    } else {
        return server_mode(argv[arg_index]);
    }
}
