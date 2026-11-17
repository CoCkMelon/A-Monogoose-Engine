/* ame-next — typed errors and logging.
 *
 * Error values are stable, inspectable codes.  Low-level modules set the
 * thread-local last error and return their existing status value; the caller
 * decides whether to retry, add context, or log it.  The X-macro list keeps
 * the enum, names, messages, and default severity in one place without
 * making SDL part of the public API.
 */
#ifndef AME_ERROR_H
#define AME_ERROR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AME_LOG_DEBUG = 0,
    AME_LOG_INFO,
    AME_LOG_WARN,
    AME_LOG_ERROR,
    AME_LOG_CRITICAL
} ame_log_level;

#define AME_ERROR_LIST(X) \
    X(AME_ERR_OK,                 AME_LOG_INFO,     "success") \
    X(AME_ERR_INVALID_ARGUMENT,   AME_LOG_ERROR,    "invalid argument") \
    X(AME_ERR_NOT_INITIALIZED,    AME_LOG_ERROR,    "module is not initialized") \
    X(AME_ERR_ALREADY_INITIALIZED,AME_LOG_ERROR,    "module is already initialized") \
    X(AME_ERR_OUT_OF_MEMORY,      AME_LOG_CRITICAL, "out of memory") \
    X(AME_ERR_FILE_OPEN,          AME_LOG_ERROR,    "could not open file") \
    X(AME_ERR_FILE_READ,          AME_LOG_ERROR,    "could not read file") \
    X(AME_ERR_TEXTURE_LIMIT,      AME_LOG_ERROR,    "texture registry is full") \
    X(AME_ERR_TEXTURE_DECODE,     AME_LOG_ERROR,    "could not decode texture") \
    X(AME_ERR_TEXTURE_FORMAT,     AME_LOG_ERROR,    "unsupported texture format") \
    X(AME_ERR_PASS_LIMIT,         AME_LOG_ERROR,    "render pass limit reached") \
    X(AME_ERR_PASS_KIND,          AME_LOG_ERROR,    "invalid render pass") \
    X(AME_ERR_SHADER_SOURCE,      AME_LOG_ERROR,    "shader source is missing") \
    X(AME_ERR_SHADER_COMPILE,     AME_LOG_ERROR,    "shader compilation failed") \
    X(AME_ERR_SHADER_LINK,        AME_LOG_ERROR,    "shader link failed") \
    X(AME_ERR_GL_LOADER,          AME_LOG_CRITICAL, "OpenGL entry points are unavailable") \
    X(AME_ERR_GL_FRAMEBUFFER,     AME_LOG_ERROR,    "framebuffer is incomplete") \
    X(AME_ERR_TEXT_ATLAS,         AME_LOG_ERROR,    "text atlas could not be loaded") \
    X(AME_ERR_PLATFORM,           AME_LOG_ERROR,    "platform operation failed") \
    X(AME_ERR_INPUT,              AME_LOG_ERROR,    "input operation failed") \
    /* Append new codes; existing numeric values remain stable. */ \
    X(AME_ERR_BATCH_FULL,         AME_LOG_ERROR,    "render batch is full") \
    X(AME_ERR_MESH_FORMAT,        AME_LOG_ERROR,    "mesh data is malformed or empty")

typedef enum {
#define AME_ERROR_ENUM(code, level, text) code,
    AME_ERROR_LIST(AME_ERROR_ENUM)
#undef AME_ERROR_ENUM
    AME_ERR_COUNT
} ame_error_code;

typedef struct {
    ame_error_code code;
    ame_log_level level;
    const char *name;
    const char *message;
} ame_error_info;

const ame_error_info *ame_error_info_get(ame_error_code code);
const char *ame_error_name(ame_error_code code);
const char *ame_error_string(ame_error_code code);
ame_log_level ame_error_level(ame_error_code code);

/* Every thread has an independent diagnostic slot.  `detail` is optional;
 * ame_last_error() returns the stable base message plus that detail. */
void ame_clear_error(void);
void ame_set_error(ame_error_code code, const char *detail);
void ame_set_errorf(ame_error_code code, const char *fmt, ...);
ame_error_code ame_last_error_code(void);
const char *ame_last_error(void);
const char *ame_last_error_detail(void);

/* THE CROSS-THREAD INBOX (state-audit.txt #3). The slot above is per thread,
 * which is right for "what failed here" - but logic runs on its own thread
 * (loop.txt), so a failure raised there would never reach the thread that
 * reports. Every error raised anywhere is therefore also published into a
 * small process-wide ring; this drains it, LOGS the entries that came from
 * OTHER threads, and returns how many it reported. Call it from the
 * reporting thread (once per frame, or before showing an error). Errors
 * raised by the CALLING thread are not re-reported - they are already in its
 * own slot - and a full ring drops its oldest entry, never blocking. */
int ame_error_drain(void);

/* Small process-wide logger.  It always writes stderr; ame_log_open also
 * mirrors records to an append-only file.  Logging is serialized so engine
 * and logic-thread diagnostics cannot interleave. */
bool ame_log_open(const char *path);
void ame_log_close(void);
void ame_log_set_level(ame_log_level level);
void ame_log(ame_log_level level, const char *fmt, ...);
void ame_log_error(ame_error_code code, const char *detail);
void ame_log_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_ERROR_H */
