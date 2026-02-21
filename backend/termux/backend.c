#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "backend/termux.h"

struct wlr_termux_backend *termux_backend_from_backend(struct wlr_backend *b) {
	assert(wlr_backend_is_termux(b));
	return (struct wlr_termux_backend *)b;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_termux_backend *backend = termux_backend_from_backend(wlr_backend);
	wlr_log(WLR_INFO, "Starting termux backend");
	struct wlr_termux_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		wl_signal_emit_mutable(&backend->backend.events.new_output, &output->wlr_output);
	}
	termux_input_create_devices(backend);
	backend->started = true;
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_termux_backend *backend = termux_backend_from_backend(wlr_backend);
	if (!wlr_backend) return;
	termux_input_destroy(backend);
	wlr_backend_finish(wlr_backend);
	struct wlr_termux_output *out, *tmp;
	wl_list_for_each_safe(out, tmp, &backend->outputs, link) {
		wlr_output_destroy(&out->wlr_output);
	}
	wl_list_remove(&backend->event_loop_destroy.link);
	free(backend->socket_path);
	free(backend);
}

static uint32_t get_buffer_caps(struct wlr_backend *wlr_backend) {
	return WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_SHM;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_buffer_caps = get_buffer_caps,
};

static void handle_event_loop_destroy(struct wl_listener *listener, void *data) {
	struct wlr_termux_backend *backend = wl_container_of(listener, backend, event_loop_destroy);
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_termux_backend_create(struct wl_event_loop *loop, const char *socket_path) {
	wlr_log(WLR_INFO, "Creating termux backend");
	struct wlr_termux_backend *backend = calloc(1, sizeof(*backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate termux backend");
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);
	backend->event_loop = loop;
	backend->socket_path = socket_path ? strdup(socket_path) : NULL;
	wl_list_init(&backend->outputs);
	wl_signal_init(&backend->events_unicode);
	backend->event_loop_destroy.notify = handle_event_loop_destroy;
	wl_event_loop_add_destroy_listener(loop, &backend->event_loop_destroy);
	return &backend->backend;
}

bool wlr_backend_is_termux(struct wlr_backend *backend) {
	return backend && backend->impl == &backend_impl;
}