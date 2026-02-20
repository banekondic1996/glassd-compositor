// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/macros.h"
#include "config/rcxml.h"
#include "common/list.h"
#include "common/mem.h"
#include "node.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"

#define SSD_BUTTON_OPACITY_IDLE 0.6f

/* Internal API */

struct ssd_button *
attach_ssd_button(struct wl_list *button_parts, enum lab_node_type type,
		struct wlr_scene_tree *parent,
		struct lab_img *imgs[LAB_BS_ALL + 1],
		int x, int y, struct view *view)
{
	struct wlr_scene_tree *root = wlr_scene_tree_create(parent);
	wlr_scene_node_set_position(&root->node, x, y);

	assert(node_type_contains(LAB_NODE_BUTTON, type));
	struct ssd_button *button = znew(*button);
	button->node = &root->node;
	button->type = type;
	node_descriptor_create(&root->node, type, view, button);
	wl_list_append(button_parts, &button->link);

	/* Hitbox */
	float invisible[4] = { 0, 0, 0, 0 };
	wlr_scene_rect_create(root, rc.theme->window_button_width,
		rc.theme->window_button_height, invisible);

	/* Icons */
	int button_width = rc.theme->window_button_width;
	int button_height = rc.theme->window_button_height;
	int target_height = MAX((rc.theme->titlebar_height * 8) / 10, 1);
	int render_height = MIN(button_height, target_height);
	int render_width = MAX(1,
		(button_width * render_height) / MAX(button_height, 1));
	int x_offset = (button_width - render_width) / 2;
	int y_offset = (button_height - render_height) / 2;
	/*
	 * Ensure a small amount of horizontal padding within the button
	 * area (2px on each side with the default 26px button width).
	 * A new theme setting could be added to configure this. Using
	 * an existing setting (padding.width or window.button.spacing)
	 * was considered, but these settings have distinct purposes
	 * already and are zero by default.
	 */
	int icon_padding = render_width / 16;
	struct wlr_scene_tree *content_root = wlr_scene_tree_create(root);
	wlr_scene_node_set_position(&content_root->node, x_offset, y_offset);

	if (type == LAB_NODE_BUTTON_WINDOW_ICON) {
		struct scaled_icon_buffer *icon_buffer =
			scaled_icon_buffer_create(content_root, view->server,
				MAX(render_width - 2 * icon_padding, 1), render_height);
		assert(icon_buffer);
		struct wlr_scene_node *icon_node = &icon_buffer->scene_buffer->node;
		scaled_icon_buffer_set_view(icon_buffer, view);
		wlr_scene_node_set_position(icon_node, icon_padding, 0);
		wlr_scene_buffer_set_opacity(icon_buffer->scene_buffer,
			SSD_BUTTON_OPACITY_IDLE);
		button->window_icon = icon_buffer;
	} else {
		for (uint8_t state_set = LAB_BS_DEFAULT;
				state_set <= LAB_BS_ALL; state_set++) {
			if (!imgs[state_set]) {
				continue;
			}
			struct scaled_img_buffer *img_buffer = scaled_img_buffer_create(
				content_root, imgs[state_set], render_width,
				render_height);
			assert(img_buffer);
			struct wlr_scene_node *icon_node = &img_buffer->scene_buffer->node;
			wlr_scene_node_set_enabled(icon_node, false);
			wlr_scene_buffer_set_opacity(img_buffer->scene_buffer,
				SSD_BUTTON_OPACITY_IDLE);
			button->img_buffers[state_set] = img_buffer;
		}
		/* Initially show non-hover, non-toggled, unrounded variant */
		wlr_scene_node_set_enabled(
			&button->img_buffers[LAB_BS_DEFAULT]->scene_buffer->node, true);
	}

	return button;
}

/* called from node descriptor destroy */
void ssd_button_free(struct ssd_button *button)
{
	wl_list_remove(&button->link);
	free(button);
}
