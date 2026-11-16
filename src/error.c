/* ame-next — typed errors and process-wide logger. */
#include <ame/error.h>
#include "ame/fs.h"

#include <stdarg.h>
#include <stdatomic.h>
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
        snprintf(g_last.detail, sizeof g_last.detail, "%s", detail);
    else
        g_last.detail[0] = '\0';
    g_last.rendered[0] = '\0';
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
