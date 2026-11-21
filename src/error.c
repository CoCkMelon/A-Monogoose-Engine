/* ame-next — typed errors and process-wide logger. */
#include <ame/error.h>
#include "ame/fs.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AME_ERROR_NAME(code, level, text) [code] = #code,
#define AME_ERROR_INFO(code, level, text) [code] = { code, level, #code, text },

static const char *const g_error_names[AME_ERR_COUNT] = {
    AME_ERROR_LIST(AME_ERROR_NAME)
};

static const ame_error_info g_error_info[AME_ERR_COUNT] = {
    AME_ERROR_LIST(AME_ERROR_INFO)
};

#undef AME_ERROR_NAME
#undef AME_ERROR_INFO

typedef struct {
    ame_error_code code;
    char detail[256];
    char rendered[512];
} ame_error_state;

static _Thread_local ame_error_state g_last = { AME_ERR_OK, { 0 }, { 0 } };

/* THE CROSS-THREAD CHANNEL (state-audit.txt #3). A _Thread_local slot is
 * right for "what failed HERE", but the loop policy puts game logic on its
 * own thread (loop.txt), so a failure raised while stepping is invisible to
 * the thread that logs and reports it: the app keeps running a broken
 * simulation and the error slot reads empty. Every error is therefore ALSO
 * published into a small process-wide ring, tagged with its origin thread,
 * which the reporting thread drains with ame_error_drain(). Publishing costs
 * a few stores on the error path only - never on success - and the
 * thread-local slot stays both the fast path and each thread's own truth. */
static bool valid_code(ame_error_code code);

#define AME_ERR_RING 16

typedef struct {
    ame_error_code code;
    unsigned long thread_id;
    char detail[128];
} ame_err_record;

static ame_err_record g_ring[AME_ERR_RING];
static unsigned g_ring_head;  /* next slot to write */
static unsigned g_ring_count; /* published, not yet drained */
static atomic_flag g_ring_lock = ATOMIC_FLAG_INIT;

/* a thread's own address of a thread-local object: unique per thread with
 * no platform thread API, which keeps this file dependency-free (the web and
 * Android builds get the same id semantics) */
static unsigned long thread_tag(void) {
    return (unsigned long)(uintptr_t)&g_last;
}

static void ring_push(ame_error_code code, const char *detail) {
    if (code == AME_ERR_OK || !valid_code(code))
        return;
    while (atomic_flag_test_and_set_explicit(&g_ring_lock,
                                             memory_order_acquire)) {
        /* error path only: contention here means two threads failing at
         * the same instant, which is exactly when serializing is right */
    }
    ame_err_record *r = &g_ring[g_ring_head];
    r->code = code;
    r->thread_id = thread_tag();
    if (detail && detail[0])
        /* Truncation is intended: r->detail is a fixed slot in the error
         * ring. Naming the bound explicitly is what keeps GCC's
         * -Wformat-truncation satisfied at -O3; clang does not implement
         * that warning, so only the GCC builds (MinGW cross, the release
         * package) ever reported it. */
        snprintf(r->detail, sizeof r->detail, "%.*s",
                 (int)sizeof r->detail - 1, detail);
    else
        r->detail[0] = '\0';
    g_ring_head = (g_ring_head + 1u) % AME_ERR_RING;
    if (g_ring_count < AME_ERR_RING)
        g_ring_count++;
    atomic_flag_clear_explicit(&g_ring_lock, memory_order_release);
}

static FILE *g_log_file;
static ame_log_level g_log_level = AME_LOG_DEBUG;
static atomic_flag g_log_lock = ATOMIC_FLAG_INIT;

static bool valid_code(ame_error_code code) {
    return code >= 0 && code < AME_ERR_COUNT;
}

static const char *level_name(ame_log_level level) {
    switch (level) {
    case AME_LOG_DEBUG:    return "DEBUG";
    case AME_LOG_INFO:     return "INFO";
    case AME_LOG_WARN:     return "WARN";
    case AME_LOG_ERROR:    return "ERROR";
    case AME_LOG_CRITICAL: return "CRITICAL";
    default:               return "UNKNOWN";
    }
}

const ame_error_info *ame_error_info_get(ame_error_code code) {
    static const ame_error_info unknown = {
        AME_ERR_INVALID_ARGUMENT, AME_LOG_ERROR,
        "AME_ERR_INVALID_ARGUMENT", "invalid error code"
    };
    return valid_code(code) ? &g_error_info[code] : &unknown;
}

const char *ame_error_name(ame_error_code code) {
    return valid_code(code) ? g_error_names[code] : "AME_ERR_UNKNOWN";
}

const char *ame_error_string(ame_error_code code) {
    return ame_error_info_get(code)->message;
}

ame_log_level ame_error_level(ame_error_code code) {
    return ame_error_info_get(code)->level;
}

void ame_clear_error(void) {
    g_last.code = AME_ERR_OK;
    g_last.detail[0] = '\0';
    g_last.rendered[0] = '\0';
}

void ame_set_error(ame_error_code code, const char *detail) {
    if (!valid_code(code))
        code = AME_ERR_INVALID_ARGUMENT;
    g_last.code = code;
    if (detail && detail[0])
        snprintf(g_last.detail, sizeof g_last.detail, "%.*s",
                 (int)sizeof g_last.detail - 1, detail);
    else
        g_last.detail[0] = '\0';
    g_last.rendered[0] = '\0';
    ring_push(code, NULL); /* publish: another thread may be the reporter */
}

void ame_set_errorf(ame_error_code code, const char *fmt, ...) {
    if (!fmt) {
        ame_set_error(code, NULL);
        return;
    }
    if (!valid_code(code))
        code = AME_ERR_INVALID_ARGUMENT;
    g_last.code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last.detail, sizeof g_last.detail, fmt, ap);
    va_end(ap);
    g_last.rendered[0] = '\0';
    ring_push(code, g_last.detail);
}

ame_error_code ame_last_error_code(void) {
    return g_last.code;
}

const char *ame_last_error_detail(void) {
    return g_last.detail;
}

const char *ame_last_error(void) {
    const ame_error_info *info = ame_error_info_get(g_last.code);
    if (!g_last.detail[0])
        return info->message;
    if (!g_last.rendered[0]) {
        snprintf(g_last.rendered, sizeof g_last.rendered, "%s: %s",
                 info->message, g_last.detail);
    }
    return g_last.rendered;
}

int ame_error_drain(void) {
    const unsigned long me = thread_tag();
    ame_err_record batch[AME_ERR_RING];
    unsigned n = 0;
    while (atomic_flag_test_and_set_explicit(&g_ring_lock,
                                             memory_order_acquire)) {
    }
    for (unsigned i = 0; i < g_ring_count; i++) {
        unsigned idx = (g_ring_head + AME_ERR_RING - g_ring_count + i)
                       % AME_ERR_RING;
        batch[n++] = g_ring[idx];
    }
    g_ring_count = 0; /* drained, whether or not the caller prints them */
    atomic_flag_clear_explicit(&g_ring_lock, memory_order_release);

    int reported = 0;
    for (unsigned i = 0; i < n; i++) {
        if (batch[i].thread_id == me)
            continue; /* this thread's own errors are already in its slot */
        const ame_error_info *info = ame_error_info_get(batch[i].code);
        if (batch[i].detail[0])
            ame_log(info->level, "%s: %s: %s (thread %lu)", info->name,
                    info->message, batch[i].detail, batch[i].thread_id);
        else
            ame_log(info->level, "%s: %s (thread %lu)", info->name,
                    info->message, batch[i].thread_id);
        reported++;
    }
    return reported;
}

static void log_unlock(void) {
    atomic_flag_clear_explicit(&g_log_lock, memory_order_release);
}

static void log_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_log_lock, memory_order_acquire)) {
        /* The logger is deliberately tiny and infrequent. */
    }
}

bool ame_log_open(const char *path) {
    if (!path || !path[0])
        return false;
    FILE *f = ame_fs_fopen(path, "ab");
    if (!f)
        return false;
    log_lock();
    FILE *old = g_log_file;
    g_log_file = f;
    log_unlock();
    if (old)
        fclose(old);
    return true;
}

void ame_log_close(void) {
    log_lock();
    FILE *old = g_log_file;
    g_log_file = NULL;
    log_unlock();
    if (old)
        fclose(old);
}

void ame_log_set_level(ame_log_level level) {
    if (level < AME_LOG_DEBUG)
        level = AME_LOG_DEBUG;
    if (level > AME_LOG_CRITICAL)
        level = AME_LOG_CRITICAL;
    g_log_level = level;
}

void ame_log(ame_log_level level, const char *fmt, ...) {
    if (level < g_log_level || !fmt)
        return;
    char message[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);

    log_lock();
    fprintf(stderr, "[ame][%s] %s\n", level_name(level), message);
    fflush(stderr);
    if (g_log_file) {
        fprintf(g_log_file, "[ame][%s] %s\n", level_name(level), message);
        fflush(g_log_file);
    }
    log_unlock();
}

void ame_log_error(ame_error_code code, const char *detail) {
    const ame_error_info *info = ame_error_info_get(code);
    if (detail && detail[0])
        ame_log(info->level, "%s: %s: %s", info->name, info->message, detail);
    else
        ame_log(info->level, "%s: %s", info->name, info->message);
}

void ame_log_last_error(void) {
    ame_log_error(g_last.code, g_last.detail);
}
