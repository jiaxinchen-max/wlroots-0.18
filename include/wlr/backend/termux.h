/*
 * Termux/Lorie display backend: expose compositor output to a termux-display
 * server (same protocol as termux-display-client main.c).
 */
#ifndef WLR_BACKEND_TERMUX_H
#define WLR_BACKEND_TERMUX_H

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>

struct wlr_allocator;

struct wlr_backend *wlr_termux_backend_create(struct wl_event_loop *loop,
	const char *socket_path);

/**
 * Add a termux output. Resolution/fps should match what the server expects.
 * The backend must be started after the display server is listening.
 */
struct wlr_output *wlr_termux_add_output(struct wlr_backend *backend,
	unsigned int width, unsigned int height, unsigned int refresh_mhz);

bool wlr_backend_is_termux(struct wlr_backend *backend);
bool wlr_output_is_termux(struct wlr_output *output);

/**
 * Get the allocator for the termux backend.
 * Returns a shared memory allocator suitable for software rendering.
 */
struct wlr_allocator *wlr_termux_backend_get_allocator(struct wlr_backend *backend);

#endif