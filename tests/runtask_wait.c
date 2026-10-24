/* Link with the library object files from make, -lm and -pthread. */
#define main runtask_application_main
#define nanosleep test_nanosleep
#include "../apps/runtask.c"
#undef nanosleep
#undef main
#include <assert.h>

static int sleep_calls, interrupt_next, cancel_next, fail_next;
static long long slept_ns;

int test_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    assert(request->tv_sec == 0);
    assert(request->tv_nsec > 0 && request->tv_nsec <= 50000000L);
    long requested = request->tv_nsec;
    ++sleep_calls;
    if (interrupt_next || cancel_next) {
        interrupt_next = 0;
        remaining->tv_sec = 0;
        remaining->tv_nsec = requested / 2;
        slept_ns += requested - remaining->tv_nsec;
        if (cancel_next) stop = 1;
        errno = EINTR;
        return -1;
    }
    if (fail_next) {
        errno = EINVAL;
        return -1;
    }
    slept_ns += requested;
    return 0;
}

static void reset_wait(void)
{
    sleep_calls = interrupt_next = cancel_next = fail_next = 0;
    slept_ns = 0;
    stop = 0;
}

int main(void)
{
    reset_wait();
    delay_ms(INT_MIN);
    delay_ms(-1);
    delay_ms(0);
    assert(sleep_calls == 0);
    const int durations[] = {1, 50, 51, 121};
    for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i) {
        reset_wait();
        delay_ms(durations[i]);
        assert(slept_ns == (long long)durations[i] * 1000000);
        assert(sleep_calls == (durations[i] + 49) / 50);
    }
    reset_wait();
    interrupt_next = 1;
    delay_ms(121);
    assert(slept_ns == 121000000 && sleep_calls == 4);
    reset_wait();
    cancel_next = 1;
    delay_ms(INT_MAX);
    assert(stop && sleep_calls == 1);
    delay_ms(50);
    assert(sleep_calls == 1);
    reset_wait();
    fail_next = 1;
    delay_ms(50);
    assert(sleep_calls == 1);
    puts("PASS: WAIT boundaries, 50 ms slices, EINTR resume, cancellation and error handling");
    return 0;
}
