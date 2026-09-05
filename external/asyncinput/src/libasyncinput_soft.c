// Software-input backend: provides programmatic injection of events and normal polling/callback consumption.
#include "asyncinput.h"

#if defined(ASYNCINPUT_HAVE_SOFTINPUT) || (!defined(_WIN32) && !defined(__linux__) && !defined(__HAIKU__))

#include <string.h>
#include <time.h>
#include <pthread.h>

#define RING_SIZE 1024

struct ringbuf {
	struct ni_event ev[RING_SIZE];
	int head;
	int tail;
	pthread_mutex_t lock;
};

static struct {
	int initialized;
	struct ringbuf q;
	ni_callback cb;
	void *cb_user;
	int soft_device_id; /* default device id for injected events */
} g;

static long long
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void
q_init(struct ringbuf *r)
{
	memset(r, 0, sizeof(*r));
	pthread_mutex_init(&r->lock, NULL);
}

static void
q_push(struct ringbuf *r, const struct ni_event *ev)
{
	pthread_mutex_lock(&r->lock);
	int next = (r->head + 1) % RING_SIZE;
	if (next != r->tail) { r->ev[r->head] = *ev; r->head = next; }
	pthread_mutex_unlock(&r->lock);
}

static int
q_pop_many(struct ringbuf *r, struct ni_event *out, int max)
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
	if (ev->timestamp_ns == 0) ev->timestamp_ns = now_ns();
	if (g.cb) g.cb(ev, g.cb_user); else q_push(&g.q, ev);
}

int
ni_init(int flags)
{
	if (flags != 0) return -1;
	if (g.initialized) return 0;
	memset(&g, 0, sizeof(g));
	q_init(&g.q);
	g.soft_device_id = -100;
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
	return 1; /* one logical software device */
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
	return q_pop_many(&g.q, evts, max_events);
}

int
ni_shutdown(void)
{
	if (!g.initialized) return 0;
	g.initialized = 0;
	return 0;
}

/* Optional APIs not supported in software backend */
int ni_register_key_callback(ni_key_callback cb, void *user_data, int flags) { (void)cb; (void)user_data; (void)flags; return -1; }
int ni_poll_key_events(struct ni_key_event *evts, int max_events) { (void)evts; (void)max_events; return -1; }
int ni_enable_xkb(int enabled) { (void)enabled; return -1; }
int ni_set_xkb_names(const char *rules, const char *model, const char *layout, const char *variant, const char *options) { (void)rules; (void)model; (void)layout; (void)variant; (void)options; return -1; }
int ni_enable_mice(int enabled) { (void)enabled; return 0; }

/* Software-input injection API */
int
ni_soft_set_device_id(int device_id)
{
	g.soft_device_id = device_id;
	return 0;
}

int
ni_soft_inject_event(const struct ni_event *ev)
{
	if (!g.initialized || !ev) return -1;
	struct ni_event e = *ev;
	if (e.device_id == 0) e.device_id = g.soft_device_id;
	emit_or_queue(&e);
	return 0;
}

int
ni_soft_key(int code, int down)
{
	if (!g.initialized) return -1;
	struct ni_event ev = {0};
	ev.device_id = g.soft_device_id;
	ev.type = NI_EV_KEY;
	ev.code = code;
	ev.value = down ? 1 : 0;
	ev.timestamp_ns = now_ns();
	emit_or_queue(&ev);
	return 0;
}

int
ni_soft_mouse_move(int dx, int dy)
{
	if (!g.initialized) return -1;
	struct ni_event ev = {0};
	ev.device_id = g.soft_device_id;
	ev.timestamp_ns = now_ns();
	if (dx) { ev.type = NI_EV_REL; ev.code = NI_REL_X; ev.value = dx; emit_or_queue(&ev); }
	if (dy) { ev.type = NI_EV_REL; ev.code = NI_REL_Y; ev.value = dy; emit_or_queue(&ev); }
	return 0;
}

int
ni_soft_mouse_wheel(int dz)
{
	if (!g.initialized) return -1;
	if (!dz) return 0;
	struct ni_event ev = {0};
	ev.device_id = g.soft_device_id;
	ev.type = NI_EV_REL;
	ev.code = NI_REL_WHEEL;
	ev.value = dz;
	ev.timestamp_ns = now_ns();
	emit_or_queue(&ev);
	return 0;
}

int
ni_soft_mouse_button(int button_code, int down)
{
	if (!g.initialized) return -1;
	struct ni_event ev = {0};
	ev.device_id = g.soft_device_id;
	ev.type = NI_EV_KEY;
	ev.code = button_code;
	ev.value = down ? 1 : 0;
	ev.timestamp_ns = now_ns();
	emit_or_queue(&ev);
	return 0;
}

#endif /* !Windows/Linux/Haiku */

