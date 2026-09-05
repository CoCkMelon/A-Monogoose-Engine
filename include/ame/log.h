#ifndef AME_LOG_H
#define AME_LOG_H

/*
 * Mongoose logging policy: non-essential logs are DEBUG-only.
 * Release builds compile LOGD away. Critical errors still use fprintf/SDL_Log
 * at the call site.
 */
#ifdef DEBUG
#  include <stdio.h>
#  define LOGD(...) fprintf(stderr, __VA_ARGS__)
#else
#  define LOGD(...) ((void)0)
#endif

#endif
