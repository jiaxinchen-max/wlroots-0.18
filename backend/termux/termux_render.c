/*
 * Uses libtermux-render: setScreenConfig, connectToRender, lorieBuffer,
 * serverState, LorieBuffer_lock/unlock, lorie_mutex_lock/unlock, stopEventLoop.
 */
#include "backend/termux.h"
#include <termux/render/render.h>
#include <termux/render/buffer.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

static bool connected;

static void on_render_stop(void) {
	connected = false;
}

bool termux_render_connected(void) {
	return connected;
}

int termux_render_connect(int width, int height, int refresh) {
	if (connected) {
		return 0;
	}
	setScreenConfig(width, height, refresh > 0 ? refresh : 60);
	setExitCallback(on_render_stop);
	if (connectToRender() != 0) {
		wlr_log(WLR_ERROR, "termux: connectToRender failed");
		return -1;
	}
	connected = true;
	return 0;
}

void termux_render_disconnect(void) {
	if (!connected) {
		return;
	}
	stopEventLoop();
	connected = false;
}

int termux_render_get_conn_fd(void) {
	if (!connected) {
		return -1;
	}
	return get_conn_fd();
}

void termux_render_get_size(int *width, int *height) {
	LorieBuffer *buf = get_lorieBuffer();
	if (!width && !height) return;
	if (!buf) {
		if (width) *width = 0;
		if (height) *height = 0;
		return;
	}
	const LorieBuffer_Desc *desc = LorieBuffer_description(buf);
	if (width) *width = desc->width;
	if (height) *height = desc->height;
}

int termux_render_push_frame(const void *data, size_t stride_bytes) {
	LorieBuffer *buf = get_lorieBuffer();
	struct lorie_shared_server_state *state = get_serverState();
	if (!connected || !buf || !state || !data) {
		return -1;
	}
	lorie_mutex_lock(&state->lock, &state->lockingPid);
	void *shared_buffer = NULL;
	if (LorieBuffer_lock(buf, &shared_buffer) != 0) {
		lorie_mutex_unlock(&state->lock, &state->lockingPid);
		return -1;
	}
	const LorieBuffer_Desc *desc = LorieBuffer_description(buf);
	int w = desc->width;
	int h = desc->height;
	int stride = desc->stride > 0 ? desc->stride : w;
	size_t row_src = stride_bytes > 0 ? stride_bytes : (size_t)w * 4;
	size_t row_dst = (size_t)stride * 4;
	if (row_src == row_dst && (size_t)stride * h * 4 <= row_dst * (size_t)h) {
		memcpy(shared_buffer, data, (size_t)stride * h * 4);
	} else {
		for (int y = 0; y < h; y++) {
			memcpy((uint8_t *)shared_buffer + y * row_dst,
				(const uint8_t *)data + y * row_src,
				(size_t)w * 4);
		}
	}
	state->waitForNextFrame = 0;
	state->drawRequested = 1;
	pthread_cond_signal(&state->cond);
	lorie_mutex_unlock(&state->lock, &state->lockingPid);
	LorieBuffer_unlock(buf);
	return 0;
}
