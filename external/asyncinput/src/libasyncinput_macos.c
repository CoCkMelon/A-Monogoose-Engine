// macOS backend using IOHIDManager: keyboard and mouse via HID callbacks.
// Thread model: a worker thread runs a CFRunLoop with IOHIDManager scheduled.
// Emits NI_EV_KEY for key/button events and NI_EV_REL for mouse motion/wheel.

#include "asyncinput.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// Ring buffer for main-thread polling
#define RING_SIZE 1024
struct ringbuf {
	struct ni_event ev[RING_SIZE];
	int head;
	int tail;
	pthread_mutex_t lock;
};

static struct {
	int initialized;
	atomic_int stop;
	pthread_t thread;
	CFRunLoopRef runloop; // worker thread runloop
	IOHIDManagerRef mgr;
	int device_count;
	struct ringbuf queue;
	ni_callback cb;
	void *cb_user;
	int mice_enabled; // currently always enabled; kept for API symmetry
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

// Minimal mapping from HID keyboard usage to NI_KEY_* codes
// Handles A-Z, 0-9, arrows, ESC, TAB, ENTER, SPACE, F1-F12, modifiers, common symbols.
static int
hid_usage_to_ni_key(uint32_t usage)
{
	// Letters A(0x04) .. Z(0x1D)
	if (usage >= 0x04 && usage <= 0x1D) {
		static const int mapAZ[] = {
			NI_KEY_A, NI_KEY_B, NI_KEY_C, NI_KEY_D, NI_KEY_E, NI_KEY_F, NI_KEY_G, NI_KEY_H,
			NI_KEY_I, NI_KEY_J, NI_KEY_K, NI_KEY_L, NI_KEY_M, NI_KEY_N, NI_KEY_O, NI_KEY_P,
			NI_KEY_Q, NI_KEY_R, NI_KEY_S, NI_KEY_T, NI_KEY_U, NI_KEY_V, NI_KEY_W, NI_KEY_X,
			NI_KEY_Y, NI_KEY_Z };
		return mapAZ[usage - 0x04];
	}
	// 1..0: HID 0x1E..0x27
	if (usage >= 0x1E && usage <= 0x27) {
		static const int mapNum[] = {
			NI_KEY_1, NI_KEY_2, NI_KEY_3, NI_KEY_4, NI_KEY_5, NI_KEY_6, NI_KEY_7, NI_KEY_8, NI_KEY_9, NI_KEY_0 };
		return mapNum[usage - 0x1E];
	}
	// Function keys F1..F12: 0x3A..0x45
	if (usage >= 0x3A && usage <= 0x45) {
		static const int mapF[] = {
			NI_KEY_F1, NI_KEY_F2, NI_KEY_F3, NI_KEY_F4, NI_KEY_F5, NI_KEY_F6,
			NI_KEY_F7, NI_KEY_F8, NI_KEY_F9, NI_KEY_F10, NI_KEY_F11, NI_KEY_F12 };
		return mapF[usage - 0x3A];
	}
	// Arrows: 0x4F RIGHT, 0x50 LEFT, 0x51 DOWN, 0x52 UP
	switch (usage) {
		case 0x29: return NI_KEY_ESC;
		case 0x2B: return NI_KEY_TAB;
		case 0x28: return NI_KEY_ENTER;
		case 0x2C: return NI_KEY_SPACE;
		case 0x2D: return NI_KEY_MINUS;
		case 0x2E: return NI_KEY_EQUAL;
		case 0x2F: return NI_KEY_LEFTBRACE;  // '[' on US
		case 0x30: return NI_KEY_RIGHTBRACE; // ']' on US
		case 0x31: return NI_KEY_BACKSLASH;
		case 0x33: return NI_KEY_SEMICOLON;
		case 0x34: return NI_KEY_APOSTROPHE;
		case 0x35: return NI_KEY_GRAVE;
		case 0x36: return NI_KEY_COMMA;
		case 0x37: return NI_KEY_DOT;
		case 0x38: return NI_KEY_SLASH;
		case 0x4F: return NI_KEY_RIGHT;
		case 0x50: return NI_KEY_LEFT;
		case 0x51: return NI_KEY_DOWN;
		case 0x52: return NI_KEY_UP;
		default: break;
	}
	// Modifiers: HID 0xE0..0xE7 (LCTRL, LSHIFT, LALT, LGUI, RCTRL, RSHIFT, RALT, RGUI)
	if (usage >= 0xE0 && usage <= 0xE7) {
		static const int mapMod[] = {
			NI_KEY_LEFTCTRL, NI_KEY_LEFTSHIFT, NI_KEY_LEFTALT, NI_KEY_LEFTMETA,
			NI_KEY_RIGHTCTRL, NI_KEY_RIGHTSHIFT, NI_KEY_RIGHTALT, NI_KEY_RIGHTMETA };
		return mapMod[usage - 0xE0];
	}
	return 0; // unknown
}

static int
hid_button_usage_to_code(uint32_t usage)
{
	// Map Button 1..5 -> NI_BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA
	switch (usage) {
		case 1: return NI_BTN_LEFT;
		case 2: return NI_BTN_RIGHT;
		case 3: return NI_BTN_MIDDLE;
		case 4: return NI_BTN_SIDE;
		case 5: return NI_BTN_EXTRA;
		default: return 0;
	}
}

static void
hid_input_cb(void *context, IOReturn result, void *sender, IOHIDValueRef value)
{
	(void)context; (void)result; (void)sender;
	IOHIDElementRef elem = IOHIDValueGetElement(value);
	if (!elem) return;
	uint32_t page = IOHIDElementGetUsagePage(elem);
	uint32_t usage = IOHIDElementGetUsage(elem);
	long v = IOHIDValueGetIntegerValue(value);
	long long ts = now_ns();

	struct ni_event ev; memset(&ev, 0, sizeof(ev));
	ev.device_id = 0; // we don't provide per-device ids yet
	ev.timestamp_ns = ts;

	// Keyboard
	if (page == kHIDPage_KeyboardOrKeypad) {
		int code = hid_usage_to_ni_key(usage);
		if (code != 0) {
			ev.type = NI_EV_KEY;
			ev.code = code;
			ev.value = (v != 0) ? 1 : 0; // 1 = press, 0 = release
			emit_or_queue(&ev);
		}
		return;
	}
	// Mouse buttons
	if (page == kHIDPage_Button) {
		int code = hid_button_usage_to_code(usage);
		if (code != 0) {
			ev.type = NI_EV_KEY; ev.code = code; ev.value = (v != 0) ? 1 : 0; emit_or_queue(&ev);
		}
		return;
	}
	// Generic Desktop: X/Y movement and wheel
	if (page == kHIDPage_GenericDesktop) {
		if (usage == kHIDUsage_GD_X) { if (v) { ev.type = NI_EV_REL; ev.code = NI_REL_X; ev.value = (int)v; emit_or_queue(&ev);} return; }
		if (usage == kHIDUsage_GD_Y) { if (v) { ev.type = NI_EV_REL; ev.code = NI_REL_Y; ev.value = -(int)v; emit_or_queue(&ev);} return; }
		if (usage == kHIDUsage_GD_Wheel) { if (v) { ev.type = NI_EV_REL; ev.code = NI_REL_WHEEL; ev.value = (int)v; emit_or_queue(&ev);} return; }
	}
}

static CFDictionaryRef
match_dict_create(uint32_t page, uint32_t usage)
{
	CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (!dict) return NULL;
	CFNumberRef p = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
	CFNumberRef u = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
	if (p && u) {
		CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), p);
		CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), u);
	}
	if (p) CFRelease(p);
	if (u) CFRelease(u);
	return dict;
}

static void *
worker_thread(void *arg)
{
	(void)arg;
	g.runloop = CFRunLoopGetCurrent();
	g.mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	if (!g.mgr) return NULL;

	CFDictionaryRef match_kb = match_dict_create(kHIDPage_GenericDesktop, kHIDUsage_GD_Keyboard);
	CFDictionaryRef match_mouse = match_dict_create(kHIDPage_GenericDesktop, kHIDUsage_GD_Mouse);
	const void *matches[2]; CFIndex mcount = 0;
	if (match_kb) matches[mcount++] = match_kb;
	if (match_mouse) matches[mcount++] = match_mouse;
	CFArrayRef match_list = CFArrayCreate(kCFAllocatorDefault, matches, mcount, &kCFTypeArrayCallBacks);
	if (match_list) {
		IOHIDManagerSetDeviceMatchingMultiple(g.mgr, match_list);
		CFRelease(match_list);
	}
	if (match_kb) CFRelease(match_kb);
	if (match_mouse) CFRelease(match_mouse);

	IOHIDManagerRegisterInputValueCallback(g.mgr, hid_input_cb, NULL);
	IOHIDManagerScheduleWithRunLoop(g.mgr, g.runloop, kCFRunLoopDefaultMode);
	IOReturn r = IOHIDManagerOpen(g.mgr, kIOHIDOptionsTypeNone);
	if (r != kIOReturnSuccess) {
		IOHIDManagerUnscheduleFromRunLoop(g.mgr, g.runloop, kCFRunLoopDefaultMode);
		CFRelease(g.mgr); g.mgr = NULL; return NULL;
	}
	g.device_count = 2; // logical kb+mouse

	while (atomic_load(&g.stop) == 0) {
		SInt32 res = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true); // 10ms slices
		(void)res;
	}
	IOHIDManagerUnscheduleFromRunLoop(g.mgr, g.runloop, kCFRunLoopDefaultMode);
	IOHIDManagerClose(g.mgr, kIOHIDOptionsTypeNone);
	CFRelease(g.mgr); g.mgr = NULL;
	return NULL;
}

int
ni_init(int flags)
{
	if (flags != 0) return -1;
	if (g.initialized) return 0;
	memset(&g, 0, sizeof(g));
	ring_init(&g.queue);
	atomic_store(&g.stop, 0);
	g.mice_enabled = 1;
	if (pthread_create(&g.thread, NULL, worker_thread, NULL) != 0) return -1;
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
	return g.device_count;
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
	atomic_store(&g.stop, 1);
	if (g.runloop) CFRunLoopStop(g.runloop);
	pthread_join(g.thread, NULL);
	g.initialized = 0;
	return 0;
}

int ni_register_key_callback(ni_key_callback cb, void *user_data, int flags) { (void)cb; (void)user_data; (void)flags; return -1; }
int ni_poll_key_events(struct ni_key_event *evts, int max_events) { (void)evts; (void)max_events; return -1; }
int ni_enable_xkb(int enabled) { (void)enabled; return -1; }
int ni_set_xkb_names(const char *rules, const char *model, const char *layout, const char *variant, const char *options) { (void)rules; (void)model; (void)layout; (void)variant; (void)options; return -1; }
int ni_enable_mice(int enabled) { g.mice_enabled = enabled ? 1 : 0; return 0; }

#else
// Non-Apple build: provide an empty TU so the file compiles if accidentally selected.
typedef int make_cfile_nonempty_macos;
#endif
