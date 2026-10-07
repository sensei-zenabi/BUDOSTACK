#ifndef BUDOWIN_DOCUMENT_IO_H
#define BUDOWIN_DOCUMENT_IO_H
#include <sys/mman.h>
#include <stdatomic.h>
#define BW_DOCUMENT_LIMIT (16u * 1024u * 1024u)

typedef struct DocumentIoState {
    _Atomic int done, cancel;
    _Atomic unsigned int bytes;
    int ok, error;
    unsigned int size;
    unsigned char data[];
} DocumentIoState;
static int document_io_active, document_runtime_ready;
static unsigned int document_io_bytes, document_io_total;
static DocumentIoState *document_io_shared;
static char document_io_name[MAX_NAME];
static void document_io_pump(void);
static void document_io_reset_capture(void);
static int document_io_run(const char *path, const unsigned char *data, unsigned int size,
                            unsigned char **loaded, unsigned int *loaded_size);
static void document_io_draw(void);

typedef struct BufferedDocument {
    FILE *stream;
    char *buffer;
    size_t length;
} BufferedDocument;
static BufferedDocument buffered_documents[8];
static unsigned char *document_read_buffers[8];

static FILE *document_read_open(const char *path)
{
    if (!document_runtime_ready) return fopen(path, "rb");
    unsigned char *data = NULL;
    unsigned int size;
    if (!document_io_run(path, NULL, 0, &data, &size)) return NULL;
    for (int i = 0; i < 8; ++i) {
        if (document_read_buffers[i]) continue;
        FILE *file = fmemopen(data, size, "rb");
        if (!file) { free(data); return NULL; }
        document_read_buffers[i] = data;
        return file;
    }
    free(data); errno = EMFILE; return NULL;
}

static void document_reads_release(void)
{
    for (int i = 0; i < 8; ++i) { free(document_read_buffers[i]); document_read_buffers[i] = NULL; }
}

static FILE *document_write_open(char *temporary)
{
    for (int i = 0; i < 8; ++i) {
        if (buffered_documents[i].stream) continue;
        BufferedDocument *entry = &buffered_documents[i];
        free(entry->buffer);
        entry->buffer = NULL; entry->length = 0;
        entry->stream = open_memstream(&entry->buffer, &entry->length);
        if (entry->stream) temporary[0] = 0;
        return entry->stream;
    }
    errno = EMFILE; return NULL;
}

/* All serializers write into a memory stream. Only the worker touches the
 * destination, using fsync and atomic replacement after successful writes. */
static int document_write_finish(FILE *file, const char *path)
{
    for (int i = 0; i < 8; ++i) {
        BufferedDocument *entry = &buffered_documents[i];
        if (entry->stream != file) continue;
        int ok = !ferror(file) && fflush(file) == 0;
        if (fclose(file) != 0) ok = 0;
        entry->stream = NULL;
        if (entry->length > BW_DOCUMENT_LIMIT) { ok = 0; errno = EFBIG; }
        if (ok) ok = document_io_run(path, (const unsigned char *)entry->buffer, (unsigned int)entry->length, NULL, NULL);
        free(entry->buffer); entry->buffer = NULL; entry->length = 0;
        return ok;
    }
    errno = EINVAL; return 0;
}

static void document_io_worker(DocumentIoState *shared, const char *path,
                                const unsigned char *data, unsigned int size)
{
    char temporary[MAX_PATH] = "";
    int fd = -1, ok = 1;
    struct stat info;
    if (data) {
        if (stat(path, &info) == 0 && !S_ISREG(info.st_mode)) { errno = EINVAL; ok = 0; }
        if (ok) {
            int length = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
            if (length < 0 || (size_t)length >= sizeof(temporary)) { errno = ENAMETOOLONG; ok = 0; }
        }
        if (ok) fd = mkstemp(temporary);
        if (fd < 0) ok = 0;
        if (ok && stat(path, &info) == 0 && fchmod(fd, info.st_mode & 0777) != 0) ok = 0;
        unsigned int offset = 0;
        while (ok && offset < size && !atomic_load(&shared->cancel)) {
            unsigned int count = size - offset;
            if (count > 65536) count = 65536;
            ssize_t written = write(fd, data + offset, count);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = 0; break; }
            offset += (unsigned int)written;
            atomic_store(&shared->bytes, offset);
        }
        if (ok && atomic_load(&shared->cancel)) { errno = ECANCELED; ok = 0; }
        if (ok && fsync(fd) != 0) ok = 0;
        int error = errno;
        if (fd >= 0 && close(fd) != 0 && ok) { ok = 0; error = errno; }
        if (atomic_load(&shared->cancel)) { ok = 0; error = ECANCELED; }
        if (ok && rename(temporary, path) != 0) { ok = 0; error = errno; }
        if (!ok && temporary[0]) unlink(temporary);
        shared->error = error;
    } else {
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) ok = 0;
        if (ok && (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode))) { errno = EINVAL; ok = 0; }
        if (ok && info.st_size > BW_DOCUMENT_LIMIT) { errno = EFBIG; ok = 0; }
        unsigned int offset = 0;
        while (ok && !atomic_load(&shared->cancel)) {
            if (offset == BW_DOCUMENT_LIMIT) {
                unsigned char extra;
                ssize_t count = read(fd, &extra, 1);
                if (count != 0) { errno = EFBIG; ok = 0; }
                break;
            }
            unsigned int chunk = BW_DOCUMENT_LIMIT - offset;
            if (chunk > 65536) chunk = 65536;
            ssize_t count = read(fd, shared->data + offset, chunk);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) { ok = 0; break; }
            if (!count) break;
            offset += (unsigned int)count;
            atomic_store(&shared->bytes, offset);
        }
        if (atomic_load(&shared->cancel)) { errno = ECANCELED; ok = 0; }
        shared->size = offset;
        shared->error = errno;
        if (fd >= 0) close(fd);
    }
    shared->ok = ok;
    atomic_store(&shared->done, 1);
    _exit(ok ? 0 : 1);
}

static int document_io_run(const char *path, const unsigned char *data, unsigned int size,
                            unsigned char **loaded, unsigned int *loaded_size)
{
    if (document_io_active) { errno = EBUSY; return 0; }
    size_t capacity = sizeof(DocumentIoState) + (data ? 0 : BW_DOCUMENT_LIMIT);
    /* /dev/zero keeps shared mappings portable without MAP_ANONYMOUS macros. */
    int zero = open("/dev/zero", O_RDWR);
    if (zero < 0) return 0;
    DocumentIoState *shared = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, zero, 0);
    close(zero);
    if (shared == MAP_FAILED) return 0;
    atomic_init(&shared->done, 0); atomic_init(&shared->cancel, 0); atomic_init(&shared->bytes, 0);
    shared->ok = shared->error = 0; shared->size = 0;
    pid_t child = fork();
    if (child < 0) { munmap(shared, capacity); return 0; }
    if (!child) document_io_worker(shared, path, data, size);
    document_io_active = 1; document_io_shared = shared;
    document_io_reset_capture();
    document_io_total = data ? size : 0;
    const char *name = strrchr(path, '/');
    snprintf(document_io_name, sizeof(document_io_name), "%s", name ? name + 1 : path);
    int status = 0, reaped = 0;
    while (!atomic_load(&shared->done)) {
        document_io_bytes = atomic_load(&shared->bytes);
        if (document_runtime_ready) document_io_pump();
        else { struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL); }
        if (waitpid(child, &status, WNOHANG) == child) { reaped = 1; break; }
    }
    if (!reaped) while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    int ok = atomic_load(&shared->done) && shared->ok;
    int error = shared->error;
    if (ok && loaded) {
        *loaded = malloc((size_t)shared->size + 1);
        if (!*loaded) { ok = 0; error = ENOMEM; }
        else {
            memcpy(*loaded, shared->data, shared->size);
            (*loaded)[shared->size] = 0;
            *loaded_size = shared->size;
        }
    }
    document_io_active = 0; document_io_shared = NULL;
    munmap(shared, capacity);
    errno = error;
    return ok;
}
#endif
