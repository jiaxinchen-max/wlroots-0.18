#ifndef BACKEND_TERMUX_H
#define BACKEND_TERMUX_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <wayland-server-core.h>
#include <wlr/backend/termux.h>
#include <wlr/backend/interface.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>

struct wlr_allocator;

/* Simple thread-safe queue for async present */
struct termux_present_queue {
	struct wl_list buffers;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int length;
};

struct wlr_termux_backend {
	struct wlr_backend backend;
	struct wl_event_loop *event_loop;
	struct wl_list outputs;
	struct wl_listener event_loop_destroy;
	bool started;
	char *socket_path;

	struct wl_event_source *input_event;
	struct wlr_termux_pointer *pointer;
	struct wlr_termux_touch *touch;
	struct wlr_termux_keyboard *keyboard;

	/** Emitted when a Unicode codepoint is received (EVENT_UNICODE). data: const uint32_t* codepoint. Compositor may forward via wlr_text_input_v3_send_commit_string. */
	struct wl_signal events_unicode;

	/** Pending resize from EVENT_SCREEN_SIZE; timer triggers reinit (disconnect, sleep, connect, update output, recreate input). */
	struct {
		int width;
		int height;
		int framerate;
		struct wl_event_source *timer;
	} resize_pending;
};

struct wlr_termux_keyboard {
	struct wlr_keyboard wlr_keyboard;
	struct wlr_termux_backend *backend;
};

struct wlr_termux_output {
	struct wlr_output wlr_output;
	struct wlr_termux_backend *backend;
	struct wl_list link;
	
	/* Async present mechanism */
	struct termux_present_queue present_queue;
	pthread_t present_thread;
	bool present_thread_running;
	int present_complete_fd;
	struct wl_event_source *present_complete_source;
};

/* Buffer entry for present queue */
struct termux_present_buffer {
	struct wlr_buffer *buffer;
	struct wl_list link;
};

struct wlr_termux_pointer {
	struct wlr_pointer wlr_pointer;
	struct wlr_termux_backend *backend;
};

struct wlr_termux_touch {
	struct wlr_touch wlr_touch;
	struct wlr_termux_backend *backend;
};

struct wlr_termux_backend *termux_backend_from_backend(struct wlr_backend *wlr_backend);

void termux_input_create_devices(struct wlr_termux_backend *backend);
void termux_input_destroy(struct wlr_termux_backend *backend);

struct wlr_allocator *wlr_termux_backend_get_allocator(struct wlr_backend *backend);

/* libtermux-render wrapper; use <termux/render/render.h> and <termux/render/buffer.h> where you need library types. */
int termux_render_connect(int width, int height, int refresh);
void termux_render_disconnect(void);
int termux_render_push_frame(const void *data, size_t stride_bytes);
void termux_render_get_size(int *width, int *height);
bool termux_render_connected(void);
int termux_render_get_conn_fd(void);

#endif