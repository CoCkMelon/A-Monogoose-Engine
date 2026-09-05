// KolibriOS backend (skeleton): a minimal no-op implementation that compiles under KolibriOS
// and links with libasyncinput. It exposes the same API but does not yet read real devices.
//
// Rationale: KolibriOS does not provide POSIX/pthreads or Linux-style /dev input devices.
// A proper backend should use KolibriOS syscalls or SDK to poll the system event queue
// (keyboard/mouse). This file establishes the structure; real handling can be added once
// the KolibriOS SDK and exact syscall interface are available in this build environment.
//
// If you have the KolibriOS SDK headers (e.g., <kolibrios.h>, <sys/kolibri.h>), we can
// replace the placeholders below with real calls (e.g., wait for event, get key/mouse data)
// on a worker loop and emit ni_event records.

#include "asyncinput.h"

#if defined(KOLIBRIOS) || defined(__KOLIBRIOS__) || defined(__MENUET__)

/* Minimal skeleton: no threads, no syscalls yet. Provides a queue-less, no-op backend
 * that reports zero devices. This keeps linkage happy and allows higher layers to fall
 * back or proceed without crashing. */

/* State */
static struct {
    int initialized;
    ni_callback cb;
    void *cb_user;
    int mice_enabled;
} g;

int ni_init(int flags)
{
    if (flags != 0) return -1;
    if (g.initialized) return 0;
    g.initialized = 1;
    g.cb = 0; g.cb_user = 0; g.mice_enabled = 1;
    return 0;
}

int ni_set_device_filter(ni_device_filter filter, void *user_data)
{
    (void)filter; (void)user_data; return 0;
}

int ni_device_count(void)
{
    return 0; /* not yet enumerating Kolibri devices */
}

int ni_register_callback(ni_callback cb, void *user_data, int flags)
{
    if (!g.initialized || flags != 0) return -1;
    g.cb = cb; g.cb_user = user_data; return 0;
}

int ni_poll(struct ni_event *evts, int max_events)
{
    (void)evts; (void)max_events;
    if (!g.initialized) return -1;
    return 0; /* no events yet */
}

int ni_shutdown(void)
{
    if (!g.initialized) return 0;
    g.initialized = 0;
    g.cb = 0; g.cb_user = 0;
    return 0;
}

/* Optional/high-level APIs: not available in this skeleton */
int ni_register_key_callback(ni_key_callback cb, void *user_data, int flags) { (void)cb; (void)user_data; (void)flags; return -1; }
int ni_poll_key_events(struct ni_key_event *evts, int max_events) { (void)evts; (void)max_events; return -1; }
int ni_enable_xkb(int enabled) { (void)enabled; return -1; }
int ni_set_xkb_names(const char *rules, const char *model, const char *layout, const char *variant, const char *options) { (void)rules; (void)model; (void)layout; (void)variant; (void)options; return -1; }
int ni_enable_mice(int enabled) { g.mice_enabled = enabled ? 1 : 0; return 0; }

#else
/* Not a KolibriOS build: provide an empty TU so accidental inclusion compiles. */
typedef int kolibri_nonempty_t;
#endif
