// KasperskyOS backend: POSIX-compatible worker reading keyboard and PS/2 mouse devices.
// Uses environment overrides:
//   ASYNCINPUT_KBD_DEVICE (default: /dev/kbd, then /dev/console)
//   ASYNCINPUT_MOUSE_DEVICE (default: /dev/mouse, then /dev/input/mice)

#include "asyncinput.h"

#if defined(KASPERSKYOS) || defined(__KASPERSKYOS__) || defined(KASPERSKY_OS)

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define RING_SIZE 1024

struct ringbuf {
	struct ni_event ev[RING_SIZE];
	int head;
	int tail;
	pthread_mutex_t lock;
};

static struct {
	int initialized;
	volatile int stop;
	pthread_t kb_thread;
	pthread_t ms_thread;
	int kb_fd;
	int ms_fd;
	struct ringbuf queue;
	ni_callback cb;
	void *cb_user;
	int mice_enabled;
} g;

static long long
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void
ring_init(struct ringbuf *r)
{
	memset(r, 0, sizeof(*r));
	pthread_mutex_init(&r->lock, NULL);
}

static void
ring_push(struct ringbuf *r, const struct ni_event *ev)
{
	pthread_mutex_lock(&r->lock);
	int next = (r->head + 1) % RING_SIZE;
	if (next != r->tail) { r->ev[r->head] = *ev; r->head = next; }
	pthread_mutex_unlock(&r->lock);
}

static int
ring_pop_many(struct ringbuf *r, struct ni_event *out, int max)
{
	int n = 0;
	pthread_mutex_lock(&r->lock);
	while (n < max && r->tail != r->head) {
		out[n++] = r->ev[r->tail];
		r->tail = (r->tail + 1) % RING_SIZE;
	}
	pthread_mutex_unlock(&r->lock);
	return n;
}

static inline void
emit_or_queue(struct ni_event *ev)
{
	if (g.cb) g.cb(ev, g.cb_user); else ring_push(&g.queue, ev);
}

static int
try_open(const char *path)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	return fd;
}

static void *
kb_worker(void *arg)
{
	(void)arg;
	unsigned char b;
	bool e0 = false;
	for (;;) {
		if (g.stop) break;
		ssize_t r = read(g.kb_fd, &b, 1);
		if (r == 0) { usleep(1000); continue; }
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(1000); continue; }
			break;
		}
		if (b == 0xE0) { e0 = true; continue; }
		int down = 1;
		int sc = b;
		/* PS/2 set1: release has high bit set */
		if (sc & 0x80) { down = 0; sc &= 0x7F; }
		/* Map scancode set1 directly to NI_KEY_* fallback values */
		struct ni_event ev = {0};
		ev.device_id = -10;
		ev.timestamp_ns = now_ns();
		ev.type = NI_EV_KEY;
		ev.code = sc; /* matches NI_KEY_* fallback */
		ev.value = down ? 1 : 0;
		emit_or_queue(&ev);
		e0 = false;
	}
	return NULL;
}

static void *
ms_worker(void *arg)
{
	(void)arg;
	unsigned char pkt[4];
	int have = 0;
	for (;;) {
		if (g.stop || !g.mice_enabled) break;
		unsigned char buf[8];
		ssize_t r = read(g.ms_fd, buf, sizeof(buf));
		if (r <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(1000); continue; }
			break;
		}
		for (ssize_t i = 0; i < r; i++) {
			pkt[have++] = buf[i];
			if (have >= 3) {
				unsigned char flags = pkt[0];
				signed char dx = (signed char)pkt[1];
				signed char dy = (signed char)pkt[2];
				long long ts = now_ns();
				struct ni_event ev = {0}; ev.device_id = -11; ev.timestamp_ns = ts;
				/* buttons */
				ev.type = NI_EV_KEY; ev.code = NI_BTN_LEFT; ev.value = (flags & 0x01) ? 1 : 0; emit_or_queue(&ev);
				ev.code = NI_BTN_RIGHT; ev.value = (flags & 0x02) ? 1 : 0; emit_or_queue(&ev);
				ev.code = NI_BTN_MIDDLE; ev.value = (flags & 0x04) ? 1 : 0; emit_or_queue(&ev);
				/* rel moves: dy inverted to match evdev coords */
				if (dx) { ev.type = NI_EV_REL; ev.code = NI_REL_X; ev.value = (int)dx; emit_or_queue(&ev); }
				if (dy) { ev.type = NI_EV_REL; ev.code = NI_REL_Y; ev.value = -(int)dy; emit_or_queue(&ev); }
				/* Optional 4th byte for wheel */
				if (have >= 4) {
					signed char dz = (signed char)pkt[3];
					if (dz) { ev.type = NI_EV_REL; ev.code = NI_REL_WHEEL; ev.value = (int)dz; emit_or_queue(&ev); }
				}
				have = 0;
			}
		}
	}
	return NULL;
}

int
ni_init(int flags)
{
	if (flags != 0) return -1;
	if (g.initialized) return 0;
	memset(&g, 0, sizeof(g));
	ring_init(&g.queue);
	g.kb_fd = -1; g.ms_fd = -1;

	const char *kbd = getenv("ASYNCINPUT_KBD_DEVICE");
	const char *mse = getenv("ASYNCINPUT_MOUSE_DEVICE");
	if (!kbd) kbd = "/dev/kbd";
	g.kb_fd = try_open(kbd);
	if (g.kb_fd < 0) {
		/* fallback to console (raw) */
		g.kb_fd = try_open("/dev/console");
	}
	if (mse) g.ms_fd = try_open(mse);
	if (g.ms_fd < 0) {
		g.ms_fd = try_open("/dev/mouse");
		if (g.ms_fd < 0) g.ms_fd = try_open("/dev/input/mice");
	}

	g.stop = 0;
	if (g.kb_fd >= 0) {
		if (pthread_create(&g.kb_thread, NULL, kb_worker, NULL) != 0) {
			close(g.kb_fd); g.kb_fd = -1; return -1;
		}
	}
	if (g.ms_fd >= 0 && g.mice_enabled) {
		if (pthread_create(&g.ms_thread, NULL, ms_worker, NULL) != 0) {
			/* mouse optional */
			close(g.ms_fd); g.ms_fd = -1;
		}
	}
	g.initialized = 1;
	return 0;
}

int
ni_set_device_filter(ni_device_filter filter, void *user_data)
{
	(void)filter; (void)user_data; return 0;
}

int
ni_device_count(void)
{
	int n = 0;
	if (g.kb_fd >= 0) n++;
	if (g.ms_fd >= 0) n++;
	return n;
}

int
ni_register_callback(ni_callback cb, void *user_data, int flags)
{
	if (!g.initialized || flags != 0) return -1;
	g.cb = cb; g.cb_user = user_data; return 0;
}

int
ni_poll(struct ni_event *evts, int max_events)
{
	if (!g.initialized || !evts || max_events <= 0) return -1;
	return ring_pop_many(&g.queue, evts, max_events);
}

int
ni_shutdown(void)
{
	if (!g.initialized) return 0;
	g.stop = 1;
	if (g.kb_thread) pthread_join(g.kb_thread, NULL);
	if (g.ms_thread) pthread_join(g.ms_thread, NULL);
	if (g.kb_fd >= 0) close(g.kb_fd);
	if (g.ms_fd >= 0) close(g.ms_fd);
	g.kb_fd = g.ms_fd = -1;
	g.initialized = 0;
	return 0;
}

int
ni_register_key_callback(ni_key_callback cb, void *user_data, int flags)
{
	(void)cb; (void)user_data; (void)flags; return -1;
}

int
ni_poll_key_events(struct ni_key_event *evts, int max_events)
{
	(void)evts; (void)max_events; return -1;
}

int
ni_enable_xkb(int enabled)
{
	(void)enabled; return -1;
}

int
ni_set_xkb_names(const char *rules, const char *model, const char *layout,
		     const char *variant, const char *options)
{
	(void)rules; (void)model; (void)layout; (void)variant; (void)options; return -1;
}

int
ni_enable_mice(int enabled)
{
	g.mice_enabled = enabled ? 1 : 0;
	if (!g.initialized) return 0;
	if (enabled && g.ms_fd >= 0 && !g.ms_thread) {
		if (pthread_create(&g.ms_thread, NULL, ms_worker, NULL) != 0) { g.mice_enabled = 0; return -1; }
	}
	return 0;
}

#else
/* Not KasperskyOS build */
typedef int kasper_nonempty;
#endif

