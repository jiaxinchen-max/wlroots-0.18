/*
 * Termux output: on commit, copy wlr_buffer to shared buffer via libtermux-render.
 * Frame events use Wayland-native automatic refresh (wlr_output_schedule_frame).
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/termux.h"
#include "types/wlr_output.h"

static const uint32_t SUPPORTED_OUTPUT_STATE =
	WLR_OUTPUT_STATE_BUFFER |
	WLR_OUTPUT_STATE_DAMAGE |
	WLR_OUTPUT_STATE_ENABLED |
	WLR_OUTPUT_STATE_MODE;

/* Queue operations for async present */
static void present_queue_init(struct termux_present_queue *queue) {
	wl_list_init(&queue->buffers);
	pthread_mutex_init(&queue->mutex, NULL);
	pthread_cond_init(&queue->cond, NULL);
	queue->length = 0;
}

static void present_queue_destroy(struct termux_present_queue *queue) {
	pthread_mutex_destroy(&queue->mutex);
	pthread_cond_destroy(&queue->cond);
}

static void present_queue_push(struct termux_present_queue *queue, struct termux_present_buffer *buffer) {
	pthread_mutex_lock(&queue->mutex);
	if (queue->length == 0) {
		pthread_cond_signal(&queue->cond);
	}
	queue->length++;
	wl_list_insert(&queue->buffers, &buffer->link);
	pthread_mutex_unlock(&queue->mutex);
}

static struct termux_present_buffer *present_queue_pull(struct termux_present_queue *queue) {
	pthread_mutex_lock(&queue->mutex);
	
	while (queue->length == 0) {
		pthread_cond_wait(&queue->cond, &queue->mutex);
	}
	
	queue->length--;
	struct termux_present_buffer *buffer = wl_container_of(queue->buffers.prev, buffer, link);
	wl_list_remove(&buffer->link);
	
	pthread_mutex_unlock(&queue->mutex);
	return buffer;
}

static struct wlr_termux_output *termux_output_from_output(struct wlr_output *o) {
	assert(wlr_output_is_termux(o));
	return wl_container_of(o, (struct wlr_termux_output *)0, wlr_output);
}

/* Present thread: processes buffers asynchronously */
static void *present_thread_func(void *data) {
	struct wlr_termux_output *output = data;
	
	wlr_log(WLR_INFO, "termux: present thread started");
	
	while (output->present_thread_running) {
		struct termux_present_buffer *present_buffer = present_queue_pull(&output->present_queue);
		
		if (!output->present_thread_running) {
			free(present_buffer);
			break;
		}
		
		/* Copy buffer to termux render */
		if (present_buffer->buffer) {
			struct wlr_buffer *buffer = present_buffer->buffer;
			void *data = NULL;
			uint32_t format = 0;
			size_t stride = 0;
			bool ok = false;
			
			if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
				ok = termux_render_push_frame(data, stride) == 0;
				wlr_buffer_end_data_ptr_access(buffer);
			} else {
				/* Try SHM fallback */
				struct wlr_shm_attributes shm;
				if (wlr_buffer_get_shm(buffer, &shm)) {
					size_t s = (shm.stride > 0 ? (size_t)shm.stride : (size_t)buffer->width * 4)
						* (size_t)shm.height;
					void *ptr = mmap(NULL, s, PROT_READ, MAP_SHARED, shm.fd, shm.offset);
					if (ptr != MAP_FAILED) {
						ok = termux_render_push_frame(ptr, (size_t)(shm.stride > 0 ? shm.stride : buffer->width * 4)) == 0;
						munmap(ptr, s);
					}
				}
			}
			
			wlr_log(WLR_DEBUG, "termux: async buffer copy result: %s", ok ? "success" : "failed");
			wlr_buffer_unlock(buffer);
		}
		
		free(present_buffer);
		
		/* Signal completion */
		eventfd_write(output->present_complete_fd, 1);
	}
	
	wlr_log(WLR_INFO, "termux: present thread stopped");
	return NULL;
}

/* Handle present completion events */
static int present_complete_handler(int fd, uint32_t mask, void *data) {
	struct wlr_termux_output *output = data;
	
	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wlr_log(WLR_ERROR, "termux: present complete event error");
		return 0;
	}
	
	eventfd_t count = 0;
	if (eventfd_read(fd, &count) < 0) {
		return 0;
	}
	
	/* Send frame event, but let scene graph decide if actual rendering is needed */
	wlr_log(WLR_DEBUG, "termux: present completed, sending frame event");
	wlr_output_send_frame(&output->wlr_output);
	
	return 0;
}

static bool output_test(struct wlr_output *wlr_output, const struct wlr_output_state *state) {
	uint32_t unsupported = state->committed & ~SUPPORTED_OUTPUT_STATE;
	if (unsupported != 0) {
		wlr_log(WLR_ERROR, "termux: unsupported output state: 0x%x (supported: 0x%x)", 
			unsupported, SUPPORTED_OUTPUT_STATE);
		return false;
	}
	wlr_log(WLR_DEBUG, "termux: output_test: state committed=0x%x, all supported", state->committed);
	return true;
}


static bool output_commit(struct wlr_output *wlr_output, const struct wlr_output_state *state) {
	struct wlr_termux_output *output = termux_output_from_output(wlr_output);
	static int commit_count = 0;
	commit_count++;
	
	if (!output_test(wlr_output, state)) {
		wlr_log(WLR_ERROR, "termux: output_test failed!");
		return false;
	}
	
	/* Handle buffer commit asynchronously */
	if ((state->committed & WLR_OUTPUT_STATE_BUFFER) && state->buffer) {
		struct termux_present_buffer *present_buffer = calloc(1, sizeof(*present_buffer));
		if (!present_buffer) {
			wlr_log(WLR_ERROR, "termux: failed to allocate present buffer");
			return false;
		}
		
		present_buffer->buffer = state->buffer;
		wlr_buffer_lock(state->buffer);
		
		/* Queue buffer for async processing */
		present_queue_push(&output->present_queue, present_buffer);
		
		return true;
	} else {
		/* No buffer to commit, but still successful */
		return true;
	}
}

static bool output_set_cursor(struct wlr_output *wlr_output, struct wlr_buffer *buffer, int hx, int hy) {
	return true;
}
static bool output_move_cursor(struct wlr_output *wlr_output, int x, int y) {
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_termux_output *output = termux_output_from_output(wlr_output);
	
	/* Stop present thread */
	output->present_thread_running = false;
	pthread_cond_signal(&output->present_queue.cond);
	pthread_join(output->present_thread, NULL);
	
	/* Cleanup async resources */
	wl_event_source_remove(output->present_complete_source);
	close(output->present_complete_fd);
	present_queue_destroy(&output->present_queue);
	
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
	wlr_log(WLR_INFO, "termux: output enabled=%s, size=%dx%d", 
		output->wlr_output.enabled ? "true" : "false",
		output->wlr_output.width, output->wlr_output.height);

	/* Initialize async present mechanism */
	present_queue_init(&output->present_queue);
	
	output->present_complete_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
	if (output->present_complete_fd < 0) {
		wlr_log(WLR_ERROR, "termux: failed to create present complete eventfd");
		present_queue_destroy(&output->present_queue);
		free(output);
		return NULL;
	}
	
	uint32_t events = WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP;
	output->present_complete_source = wl_event_loop_add_fd(termux->event_loop, 
		output->present_complete_fd, events, present_complete_handler, output);
	if (!output->present_complete_source) {
		wlr_log(WLR_ERROR, "termux: failed to create present complete event source");
		close(output->present_complete_fd);
		present_queue_destroy(&output->present_queue);
		free(output);
		return NULL;
	}
	
	/* Start present thread */
	output->present_thread_running = true;
	if (pthread_create(&output->present_thread, NULL, present_thread_func, output) != 0) {
		wlr_log(WLR_ERROR, "termux: failed to create present thread");
		wl_event_source_remove(output->present_complete_source);
		close(output->present_complete_fd);
		present_queue_destroy(&output->present_queue);
		free(output);
		return NULL;
	}
	
	wl_list_insert(&termux->outputs, &output->link);
	if (termux->started) {
		wl_signal_emit_mutable(&termux->backend.events.new_output, &output->wlr_output);
	}
	/* First frame: schedule so compositor draws initial content (Wayland-native). */
	wlr_output_schedule_frame(&output->wlr_output);
	wlr_log(WLR_INFO, "termux: output created, using on-demand frame scheduling");
	return &output->wlr_output;
}