// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "common/macros.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

#define SSD_INNER_GAP 3
#define SSD_FRAME_SHADE_ALPHA 0.62f

enum inner_corner {
	INNER_CORNER_TOP_LEFT,
	INNER_CORNER_TOP_RIGHT,
	INNER_CORNER_BOTTOM_LEFT,
	INNER_CORNER_BOTTOM_RIGHT,
};

static bool
mask_point_accepts_input(struct wlr_scene_buffer *scene_buffer,
		double *sx, double *sy)
{
	(void)scene_buffer;
	(void)sx;
	(void)sy;
	return false;
}

static struct lab_data_buffer *
create_inner_corner_mask_buffer(int radius, const float color[4],
		enum inner_corner corner)
{
	if (radius <= 0) {
		return NULL;
	}

	struct lab_data_buffer *buffer = buffer_create_cairo(radius, radius, 1.0f);
	if (!buffer || !buffer->surface) {
		return NULL;
	}

	cairo_t *cairo = cairo_create(buffer->surface);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);

	float alpha = color[3];
	float r = alpha > 0.0f ? color[0] / alpha : 0.0f;
	float g = alpha > 0.0f ? color[1] / alpha : 0.0f;
	float b = alpha > 0.0f ? color[2] / alpha : 0.0f;
	cairo_set_source_rgba(cairo, r, g, b, alpha);
	cairo_paint(cairo);

	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	double cx = 0, cy = 0, a1 = 0, a2 = 0;
	const double pi = 3.14159265358979323846;
	switch (corner) {
	case INNER_CORNER_TOP_LEFT:
		cx = radius;
		cy = radius;
		a1 = pi;
		a2 = 1.5 * pi;
		break;
	case INNER_CORNER_TOP_RIGHT:
		cx = 0;
		cy = radius;
		a1 = 1.5 * pi;
		a2 = 2.0 * pi;
		break;
	case INNER_CORNER_BOTTOM_LEFT:
		cx = radius;
		cy = 0;
		a1 = 0.5 * pi;
		a2 = pi;
		break;
	case INNER_CORNER_BOTTOM_RIGHT:
		cx = 0;
		cy = 0;
		a1 = 0;
		a2 = 0.5 * pi;
		break;
	}

	cairo_move_to(cairo, cx, cy);
	cairo_arc(cairo, cx, cy, radius, a1, a2);
	cairo_close_path(cairo);
	cairo_fill(cairo);
	cairo_surface_flush(buffer->surface);
	cairo_destroy(cairo);
	return buffer;
}

static void
create_inner_corner_masks(struct ssd_border_subtree *subtree,
		struct wlr_scene_tree *parent, int radius, const float color[4])
{
	if (radius <= 0) {
		return;
	}

	struct lab_data_buffer *tl = create_inner_corner_mask_buffer(radius, color,
		INNER_CORNER_TOP_LEFT);
	struct lab_data_buffer *tr = create_inner_corner_mask_buffer(radius, color,
		INNER_CORNER_TOP_RIGHT);
	struct lab_data_buffer *bl = create_inner_corner_mask_buffer(radius, color,
		INNER_CORNER_BOTTOM_LEFT);
	struct lab_data_buffer *br = create_inner_corner_mask_buffer(radius, color,
		INNER_CORNER_BOTTOM_RIGHT);

	if (tl) {
		subtree->inner_top_left = wlr_scene_buffer_create(parent, &tl->base);
		subtree->inner_top_left->point_accepts_input = mask_point_accepts_input;
		wlr_buffer_drop(&tl->base);
	}
	if (tr) {
		subtree->inner_top_right = wlr_scene_buffer_create(parent, &tr->base);
		subtree->inner_top_right->point_accepts_input = mask_point_accepts_input;
		wlr_buffer_drop(&tr->base);
	}
	if (bl) {
		subtree->inner_bottom_left = wlr_scene_buffer_create(parent, &bl->base);
		subtree->inner_bottom_left->point_accepts_input = mask_point_accepts_input;
		wlr_buffer_drop(&bl->base);
	}
	if (br) {
		subtree->inner_bottom_right = wlr_scene_buffer_create(parent, &br->base);
		subtree->inner_bottom_right->point_accepts_input = mask_point_accepts_input;
		wlr_buffer_drop(&br->base);
	}
}

void
ssd_border_create(struct ssd *ssd)
{
	assert(ssd);
	assert(!ssd->border.tree);

	struct view *view = ssd->view;
	struct theme *theme = view->server->theme;
	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int corner_width = ssd_get_corner_width();
	int inner_radius = MAX(rc.corner_radius - theme->border_width + SSD_INNER_GAP, 0);
	float frame_color[4] = { 0, 0, 0, SSD_FRAME_SHADE_ALPHA };

	ssd->border.tree = wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->border.tree->node, -theme->border_width, 0);
	ssd->border.overlay_tree = wlr_scene_tree_create(view->scene_tree);
	wlr_scene_node_raise_to_top(&ssd->border.overlay_tree->node);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		subtree->tree = wlr_scene_tree_create(ssd->border.tree);
		subtree->overlay = wlr_scene_tree_create(ssd->border.overlay_tree);
		struct wlr_scene_tree *parent = subtree->tree;
		struct wlr_scene_tree *overlay_parent = subtree->overlay;
		wlr_scene_node_set_enabled(&parent->node, active);
		wlr_scene_node_set_enabled(&overlay_parent->node, active);
		float *color = frame_color;

		subtree->left = wlr_scene_rect_create(parent,
			theme->border_width, height, color);
		wlr_scene_node_set_position(&subtree->left->node, 0, 0);

		subtree->right = wlr_scene_rect_create(parent,
			theme->border_width, height, color);
		wlr_scene_node_set_position(&subtree->right->node,
			theme->border_width + width, 0);

		subtree->bottom = wlr_scene_rect_create(parent,
			full_width, theme->border_width, color);
		wlr_scene_node_set_position(&subtree->bottom->node,
			0, height);

		subtree->top = wlr_scene_rect_create(parent,
			MAX(width - 2 * corner_width, 0), theme->border_width, color);
		wlr_scene_node_set_position(&subtree->top->node,
			theme->border_width + corner_width,
			-(ssd->titlebar.height + theme->border_width));
		subtree->outer_bottom_left = wlr_scene_buffer_create(parent,
			&theme->window[active].corner_bottom_left_normal->base);
		subtree->outer_bottom_right = wlr_scene_buffer_create(parent,
			&theme->window[active].corner_bottom_right_normal->base);
		wlr_scene_buffer_set_opacity(subtree->outer_bottom_left,
			SSD_FRAME_SHADE_ALPHA);
		wlr_scene_buffer_set_opacity(subtree->outer_bottom_right,
			SSD_FRAME_SHADE_ALPHA);

		subtree->inner_top = wlr_scene_rect_create(overlay_parent,
			width, SSD_INNER_GAP, color);
		subtree->inner_bottom = wlr_scene_rect_create(overlay_parent,
			width, SSD_INNER_GAP, color);
		subtree->inner_left = wlr_scene_rect_create(overlay_parent,
			SSD_INNER_GAP, height, color);
		subtree->inner_right = wlr_scene_rect_create(overlay_parent,
			SSD_INNER_GAP, height, color);

		create_inner_corner_masks(subtree, overlay_parent, inner_radius,
			color);
	}

	if (view->current.width > 0 && view->current.height > 0) {
		/*
		 * The SSD is recreated by a Reconfigure request
		 * thus we may need to handle squared corners.
		 */
		ssd_border_update(ssd);
	}
}

void
ssd_border_update(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	struct view *view = ssd->view;
	if (!ssd->border.tree->node.enabled) {
		/* Re-enable if disabled */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, true);
		wlr_scene_node_set_enabled(&ssd->border.overlay_tree->node, true);
		ssd->margin = ssd_thickness(ssd->view);
	}
	wlr_scene_node_raise_to_top(&ssd->border.overlay_tree->node);

	struct theme *theme = view->server->theme;

	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int corner_width = ssd_get_corner_width();
	int inner_radius = MAX(rc.corner_radius - theme->border_width + SSD_INNER_GAP, 0);
	bool show_inner_radius = !ssd->state.was_squared && inner_radius > 0;
	bool show_outer_radius = rc.corner_radius > 0;

	/*
	 * From here on we have to cover the following border scenarios:
	 * Non-tiled (partial border, rounded corners):
	 *    _____________
	 *   o           oox
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled (full border, squared corners):
	 *   _______________
	 *  |o           oox|
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled or non-tiled with zero title height (full boarder, no title):
	 *   _______________
	 *  |_______________|
	 */

	int side_height = ssd->state.was_squared
		? height + ssd->titlebar.height
		: height;
	int side_y = ssd->state.was_squared
		? -ssd->titlebar.height
		: 0;
	int bottom_corner_width = rc.corner_radius > 0 ? corner_width : 0;
	if (bottom_corner_width > 0) {
		side_height = MAX(side_height - bottom_corner_width, 0);
	}
	int bottom_width = MAX(full_width - 2 * bottom_corner_width, 0);
	int bottom_x = bottom_corner_width;
	int bottom_y = height;
	int top_width = ssd->titlebar.height <= 0 || ssd->state.was_squared
		? full_width
		: MAX(width - 2 * corner_width, 0);
	int top_x = ssd->titlebar.height <= 0 || ssd->state.was_squared
		? 0
		: theme->border_width + corner_width;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];

		wlr_scene_rect_set_size(subtree->left,
			theme->border_width, side_height);
		wlr_scene_node_set_position(&subtree->left->node,
			0, side_y);

		wlr_scene_rect_set_size(subtree->right,
			theme->border_width, side_height);
		wlr_scene_node_set_position(&subtree->right->node,
			theme->border_width + width, side_y);

		wlr_scene_rect_set_size(subtree->bottom,
			bottom_width, theme->border_width);
		wlr_scene_node_set_position(&subtree->bottom->node,
			bottom_x, bottom_y);

		wlr_scene_rect_set_size(subtree->top,
			top_width, theme->border_width);
		wlr_scene_node_set_position(&subtree->top->node,
			top_x, -(ssd->titlebar.height + theme->border_width));
		wlr_scene_node_set_enabled(&subtree->outer_bottom_left->node,
			show_outer_radius);
		wlr_scene_node_set_position(&subtree->outer_bottom_left->node,
			0, MAX(height - corner_width, 0));
		wlr_scene_node_set_enabled(&subtree->outer_bottom_right->node,
			show_outer_radius);
		wlr_scene_node_set_position(&subtree->outer_bottom_right->node,
			MAX(width - corner_width + theme->border_width, 0),
			MAX(height - corner_width, 0));

		wlr_scene_rect_set_size(subtree->inner_top, width, SSD_INNER_GAP);
		wlr_scene_node_set_position(&subtree->inner_top->node, 0, 0);
		wlr_scene_rect_set_size(subtree->inner_bottom, width, SSD_INNER_GAP);
		wlr_scene_node_set_position(&subtree->inner_bottom->node, 0,
			MAX(height - SSD_INNER_GAP, 0));
		wlr_scene_rect_set_size(subtree->inner_left, SSD_INNER_GAP, height);
		wlr_scene_node_set_position(&subtree->inner_left->node, 0, 0);
		wlr_scene_rect_set_size(subtree->inner_right, SSD_INNER_GAP, height);
		wlr_scene_node_set_position(&subtree->inner_right->node,
			MAX(width - SSD_INNER_GAP, 0), 0);

		if (subtree->inner_top_left) {
			wlr_scene_node_set_enabled(&subtree->inner_top_left->node,
				show_inner_radius);
			wlr_scene_node_set_position(&subtree->inner_top_left->node,
				SSD_INNER_GAP, SSD_INNER_GAP);
		}
		if (subtree->inner_top_right) {
			wlr_scene_node_set_enabled(&subtree->inner_top_right->node,
				show_inner_radius);
			wlr_scene_node_set_position(&subtree->inner_top_right->node,
				MAX(width - inner_radius - SSD_INNER_GAP, 0), SSD_INNER_GAP);
		}
		if (subtree->inner_bottom_left) {
			wlr_scene_node_set_enabled(&subtree->inner_bottom_left->node,
				show_inner_radius);
			wlr_scene_node_set_position(&subtree->inner_bottom_left->node,
				SSD_INNER_GAP, MAX(height - inner_radius - SSD_INNER_GAP, 0));
		}
		if (subtree->inner_bottom_right) {
			wlr_scene_node_set_enabled(&subtree->inner_bottom_right->node,
				show_inner_radius);
			wlr_scene_node_set_position(&subtree->inner_bottom_right->node,
				MAX(width - inner_radius - SSD_INNER_GAP, 0),
				MAX(height - inner_radius - SSD_INNER_GAP, 0));
		}
	}
}

void
ssd_border_destroy(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	wlr_scene_node_destroy(&ssd->border.tree->node);
	wlr_scene_node_destroy(&ssd->border.overlay_tree->node);
	ssd->border = (struct ssd_border_scene){0};
}
