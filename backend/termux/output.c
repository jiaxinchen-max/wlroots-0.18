/*
 * Termux output: on commit, copy wlr_buffer to shared buffer via libtermux-render.
 * Frame events use Wayland-native automatic refresh (wlr_output_schedule_frame).
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/termux.h"
#include "types/wlr_output.h"

static const uint32_t SUPPORTED_OUTPUT_STATE =
	WLR_OUTPUT_STATE_BUFFER |
	WLR_OUTPUT_STATE_ENABLED |
	WLR_OUTPUT_STATE_MODE;

static struct wlr_termux_output *termux_output_from_output(struct wlr_output *o) {
	assert(wlr_output_is_termux(o));
	return wl_container_of(o, (struct wlr_termux_output *)0, wlr_output);
}

static bool output_test(struct wlr_output *wlr_output, const struct wlr_output_state *state) {
	uint32_t unsupported = state->committed & ~SUPPORTED_OUTPUT_STATE;
	if (unsupported != 0) return false;
	return true;
}

static bool copy_buffer_to_lorie(struct wlr_termux_output *output, const struct wlr_output_state *state) {
	(void)output;
	if (!(state->committed & WLR_OUTPUT_STATE_BUFFER) || !state->buffer) {
		return true;
	}
	if (!termux_render_connected()) {
		return true;
	}
	struct wlr_buffer *buf = state->buffer;
	void *data = NULL;
	uint32_t format = 0;
	size_t stride = 0;
	bool ok = false;
	if (wlr_buffer_begin_data_ptr_access(buf, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
		ok = termux_render_push_frame(data, stride) == 0;
		wlr_buffer_end_data_ptr_access(buf);
	} else {
		struct wlr_shm_attributes shm;
		if (wlr_buffer_get_shm(buf, &shm)) {
			size_t s = (shm.stride > 0 ? (size_t)shm.stride : (size_t)buf->width * 4)
				* (size_t)shm.height;
			void *ptr = mmap(NULL, s, PROT_READ, MAP_SHARED, shm.fd, shm.offset);
			if (ptr != MAP_FAILED) {
				ok = termux_render_push_frame(ptr, (size_t)(shm.stride > 0 ? shm.stride : buf->width * 4)) == 0;
				munmap(ptr, s);
			}
		}
	}
	if (!ok) {
		wlr_log(WLR_DEBUG, "termux: could not read buffer for push_frame");
	}
	return true;
}

static bool output_commit(struct wlr_output *wlr_output, const struct wlr_output_state *state) {
	struct wlr_termux_output *output = termux_output_from_output(wlr_output);
	if (!output_test(wlr_output, state)) return false;
	if (output_pending_enabled(wlr_output, state)) {
		copy_buffer_to_lorie(output, state);
		struct wlr_output_event_present present_event = {
			.commit_seq = wlr_output->commit_seq + 1,
			.presented = true,
		};
		output_defer_present(wlr_output, present_event);
		/* Schedule next frame only when compositor needs it (damage/frame callbacks).
		 * On first enable, schedule one frame so the initial content is drawn. */
		if (!wlr_output->enabled) {
			wlr_output_schedule_frame(wlr_output);
		}
	}
	return true;
}

static bool output_set_cursor(struct wlr_output *wlr_output, struct wlr_buffer *buffer, int hx, int hy) {
	return true;
}
static bool output_move_cursor(struct wlr_output *wlr_output, int x, int y) {
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_termux_output *output = termux_output_from_output(wlr_output);
	wlr_output_finish(wlr_output);
	wl_list_remove(&output->link);
	termux_render_disconnect();
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.test = output_test,
	.commit = output_commit,
	.set_cursor = output_set_cursor,
	.move_cursor = output_move_cursor,
};

bool wlr_output_is_termux(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

struct wlr_output *wlr_termux_add_output(struct wlr_backend *backend,
		unsigned int width, unsigned int height, unsigned int refresh_mhz) {
	struct wlr_termux_backend *termux = termux_backend_from_backend(backend);
	struct wlr_termux_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log(WLR_ERROR, "Failed to allocate termux output");
		return NULL;
	}
	output->backend = termux;
	/* Tell libtermux-render desired size (setScreenConfig); client creates buffer to match. */
	if (termux_render_connect((int)width, (int)height, (int)refresh_mhz) != 0) {
		wlr_log(WLR_ERROR, "termux: failed to connect to display server");
		free(output);
		return NULL;
	}
	/* Use actual buffer size so Wayland output matches shared buffer; avoids scaling/crop/pad. */
	int w = 0, h = 0;
	termux_render_get_size(&w, &h);
	if (w > 0 && h > 0) {
		width = (unsigned int)w;
		height = (unsigned int)h;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_custom_mode(&state, width, height, refresh_mhz > 0 ? refresh_mhz : 60);
	wlr_output_init(&output->wlr_output, &termux->backend, &output_impl, termux->event_loop, &state);
	wlr_output_state_finish(&state);

	output->wlr_output.enabled = true;
	wlr_output_set_name(&output->wlr_output, "TERMUX-1");
	wlr_output_set_description(&output->wlr_output, "Termux display client");

	wl_list_insert(&termux->outputs, &output->link);
	if (termux->started) {
		wl_signal_emit_mutable(&termux->backend.events.new_output, &output->wlr_output);
	}
	/* First frame: schedule so compositor draws initial content (Wayland-native). */
	wlr_output_schedule_frame(&output->wlr_output);
	return &output->wlr_output;
}