// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "labwc-ipc.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "labwc.h"
#include "view.h"

#define IPC_SOCKET_PATH "/tmp/labwc-nwjs.sock"
#define IPC_BUFFER_SIZE 4096

static void ipc_client_destroy(struct ipc_client *client);

static void
ipc_send_to_client(struct ipc_client *client, const char *message)
{
	size_t len = strlen(message);
	ssize_t written = write(client->fd, message, len);
	if (written < 0) {
		if (errno != EAGAIN) {
			wlr_log(WLR_DEBUG, "IPC write error: %s", strerror(errno));
			ipc_client_destroy(client);
		}
	} else if ((size_t)written < len) {
		wlr_log(WLR_DEBUG, "IPC partial write: %zd/%zu", written, len);
	}
}

static void
ipc_broadcast(struct ipc_server *ipc_server, const char *message)
{
	struct ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &ipc_server->clients, link) {
		ipc_send_to_client(client, message);
	}
}

static void
handle_ipc_command(struct ipc_client *client, const char *command)
{
	struct server *server = client->ipc_server->server;
	
	/* Parse JSON-like command format: {"cmd":"action","id":"view_id","x":100,"y":100} */
	char cmd[64] = {0};
	unsigned long view_id = 0;
	int x = 0, y = 0, width = 0, height = 0;
	
	/* Simple parser - in production, use proper JSON library */
	if (sscanf(command, "{\"cmd\":\"%63[^\"]\"", cmd) == 1) {
		char *id_str = strstr(command, "\"id\":\"");
		if (id_str) {
			sscanf(id_str, "\"id\":\"%lx\"", &view_id);
		}
		
		sscanf(command, "%*[^\"x\":]\"x\":%d", &x);
		sscanf(command, "%*[^\"y\":]\"y\":%d", &y);
		sscanf(command, "%*[^\"width\":]\"width\":%d", &width);
		sscanf(command, "%*[^\"height\":]\"height\":%d", &height);
		
		/* Find view by ID */
		struct view *view = NULL;
		struct view *v;
		wl_list_for_each(v, &server->views, link) {
			if ((unsigned long)v == view_id) {
				view = v;
				break;
			}
		}
		
		if (!view && strcmp(cmd, "list") != 0 && strcmp(cmd, "enable_decorations") != 0) {
			wlr_log(WLR_DEBUG, "IPC: view not found: %lx", view_id);
			return;
		}
		
		/* Execute command */
		if (!strcmp(cmd, "close") && view) {
			view_close(view);
		} else if (!strcmp(cmd, "minimize") && view) {
			view_minimize(view, !view->minimized);
		} else if (!strcmp(cmd, "maximize") && view) {
			view_toggle_maximize(view, VIEW_AXIS_BOTH);
		} else if (!strcmp(cmd, "move") && view && width > 0 && height > 0) {
			struct wlr_box geo = { .x = x, .y = y, .width = width, .height = height };
			view_move_resize(view, geo);
		} else if (!strcmp(cmd, "focus") && view) {
			desktop_focus_view(view, true);
		} else if (!strcmp(cmd, "always_on_top") && view) {
			view_toggle_always_on_top(view);
		} else if (!strcmp(cmd, "always_on_bottom") && view) {
			view_toggle_always_on_bottom(view);
		} else if (!strcmp(cmd, "list")) {
			ipc_send_window_list(client->ipc_server);
		} else if (!strcmp(cmd, "enable_decorations")) {
			/* Toggle SSD decorations for all views */
			wl_list_for_each(v, &server->views, link) {
				view_set_ssd_mode(v, LAB_SSD_MODE_NONE);
			}
			/* Send confirmation */
			ipc_send_to_client(client, "{\"event\":\"decorations_disabled\"}\n");
		} else {
			wlr_log(WLR_DEBUG, "IPC: unknown command: %s", cmd);
		}
	}
}

static int
ipc_client_handle_readable(int fd, uint32_t mask, void *data)
{
	struct ipc_client *client = data;
	
	if (mask & WL_EVENT_HANGUP) {
		ipc_client_destroy(client);
		return 0;
	}
	
	/* Ensure buffer has space */
	if (client->buffer_used >= client->buffer_size - 1) {
		client->buffer_size *= 2;
		client->buffer = realloc(client->buffer, client->buffer_size);
	}
	
	/* Read data */
	ssize_t len = read(fd, client->buffer + client->buffer_used,
		client->buffer_size - client->buffer_used - 1);
	
	if (len <= 0) {
		if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			wlr_log(WLR_DEBUG, "IPC read error: %s", strerror(errno));
		}
		ipc_client_destroy(client);
		return 0;
	}
	
	client->buffer_used += len;
	client->buffer[client->buffer_used] = '\0';
	
	/* Process complete messages (newline-delimited) */
	char *line_start = client->buffer;
	char *newline;
	while ((newline = strchr(line_start, '\n')) != NULL) {
		*newline = '\0';
		handle_ipc_command(client, line_start);
		line_start = newline + 1;
	}
	
	/* Move remaining incomplete data to buffer start */
	size_t remaining = client->buffer + client->buffer_used - line_start;
	if (remaining > 0 && line_start != client->buffer) {
		memmove(client->buffer, line_start, remaining);
	}
	client->buffer_used = remaining;
	
	return 0;
}

static void
ipc_client_destroy(struct ipc_client *client)
{
	wlr_log(WLR_DEBUG, "IPC client disconnected");
	wl_list_remove(&client->link);
	wl_event_source_remove(client->event_source);
	close(client->fd);
	free(client->buffer);
	free(client);
}

static int
ipc_handle_connection(int fd, uint32_t mask, void *data)
{
	struct ipc_server *ipc_server = data;
	
	int client_fd = accept(fd, NULL, NULL);
	if (client_fd < 0) {
		wlr_log(WLR_ERROR, "IPC accept failed: %s", strerror(errno));
		return 0;
	}
	
	struct ipc_client *client = znew(*client);
	client->fd = client_fd;
	client->ipc_server = ipc_server;
	client->buffer_size = IPC_BUFFER_SIZE;
	client->buffer = malloc(client->buffer_size);
	client->buffer_used = 0;
	
	client->event_source = wl_event_loop_add_fd(
		ipc_server->server->wl_event_loop,
		client_fd,
		WL_EVENT_READABLE,
		ipc_client_handle_readable,
		client);
	
	wl_list_insert(&ipc_server->clients, &client->link);
	
	wlr_log(WLR_DEBUG, "IPC client connected");
	
	/* Send current window list to new client */
	ipc_send_window_list(ipc_server);
	
	return 0;
}

struct ipc_server *
ipc_server_init(struct server *server)
{
	struct ipc_server *ipc_server = znew(*ipc_server);
	ipc_server->server = server;
	wl_list_init(&ipc_server->clients);
	
	/* Remove existing socket */
	unlink(IPC_SOCKET_PATH);
	
	/* Create Unix socket */
	ipc_server->sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (ipc_server->sock_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to create IPC socket: %s", strerror(errno));
		free(ipc_server);
		return NULL;
	}
	
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);
	
	if (bind(ipc_server->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wlr_log(WLR_ERROR, "Failed to bind IPC socket: %s", strerror(errno));
		close(ipc_server->sock_fd);
		free(ipc_server);
		return NULL;
	}
	
	if (listen(ipc_server->sock_fd, 3) < 0) {
		wlr_log(WLR_ERROR, "Failed to listen on IPC socket: %s", strerror(errno));
		close(ipc_server->sock_fd);
		free(ipc_server);
		return NULL;
	}
	
	ipc_server->event_source = wl_event_loop_add_fd(
		server->wl_event_loop,
		ipc_server->sock_fd,
		WL_EVENT_READABLE,
		ipc_handle_connection,
		ipc_server);
	
	wlr_log(WLR_INFO, "IPC server listening on %s", IPC_SOCKET_PATH);
	
	return ipc_server;
}

void
ipc_server_finish(struct ipc_server *ipc_server)
{
	if (!ipc_server) {
		return;
	}
	
	struct ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &ipc_server->clients, link) {
		ipc_client_destroy(client);
	}
	
	wl_event_source_remove(ipc_server->event_source);
	close(ipc_server->sock_fd);
	unlink(IPC_SOCKET_PATH);
	free(ipc_server);
	
	wlr_log(WLR_DEBUG, "IPC server stopped");
}

void
ipc_send_window_event(struct ipc_server *ipc_server, struct view *view, const char *event)
{
	if (!ipc_server || !view || !event) {
		return;
	}
	
	/* Don't send events if no clients are connected */
	if (wl_list_empty(&ipc_server->clients)) {
		return;
	}
	
	/* Safely get title and app_id */
	const char *title = view->title ? view->title : "";
	const char *app_id = view->app_id ? view->app_id : "";
	
	/* Check for server validity */
	if (!view->server) {
		return;
	}
	
	char msg[1024];
	snprintf(msg, sizeof(msg),
		"{\"event\":\"%s\",\"id\":\"%lx\",\"title\":\"%s\",\"app_id\":\"%s\","
		"\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
		"\"minimized\":%s,\"maximized\":%d,\"fullscreen\":%s,\"focused\":%s}\n",
		event,
		(unsigned long)view,
		title,
		app_id,
		view->current.x,
		view->current.y,
		view->current.width,
		view->current.height,
		view->minimized ? "true" : "false",
		view->maximized,
		view->fullscreen ? "true" : "false",
		(view->server->active_view == view) ? "true" : "false");
	
	ipc_broadcast(ipc_server, msg);
}

void
ipc_send_cursor_position(struct ipc_server *ipc_server, double x, double y)
{
	if (!ipc_server) {
		return;
	}
	
	/* Don't send cursor updates if no clients are connected */
	if (wl_list_empty(&ipc_server->clients)) {
		return;
	}
	
	char msg[128];
	snprintf(msg, sizeof(msg), "{\"event\":\"cursor\",\"x\":%.0f,\"y\":%.0f}\n", x, y);
	ipc_broadcast(ipc_server, msg);
}

void
ipc_send_window_list(struct ipc_server *ipc_server)
{
	if (!ipc_server) {
		return;
	}
	
	/* Don't send if no clients connected */
	if (wl_list_empty(&ipc_server->clients)) {
		return;
	}
	
	struct server *server = ipc_server->server;
	if (!server) {
		return;
	}
	
	char msg[8192] = "{\"event\":\"window_list\",\"windows\":[";
	size_t offset = strlen(msg);
	
	bool first = true;
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped) {
			continue;
		}
		
		/* Safety checks */
		const char *title = view->title ? view->title : "";
		const char *app_id = view->app_id ? view->app_id : "";
		
		char window_data[512];
		snprintf(window_data, sizeof(window_data),
			"%s{\"id\":\"%lx\",\"title\":\"%s\",\"app_id\":\"%s\","
			"\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
			"\"minimized\":%s,\"focused\":%s}",
			first ? "" : ",",
			(unsigned long)view,
			title,
			app_id,
			view->current.x,
			view->current.y,
			view->current.width,
			view->current.height,
			view->minimized ? "true" : "false",
			(server->active_view == view) ? "true" : "false");
		
		if (offset + strlen(window_data) + 10 < sizeof(msg)) {
			strcat(msg, window_data);
			offset += strlen(window_data);
			first = false;
		}
	}
	
	strcat(msg, "]}\n");
	ipc_broadcast(ipc_server, msg);
}