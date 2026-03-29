#define _POSIX_C_SOURCE 200809L
/**
 * src/events/events.c — Hit testing and event dispatch.
 */

#include "events.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Helpers to call registered callbacks
 * ========================================================================= */

static void fire(__html_node__* node, void (*cb_ptr)(void*), void* ud_ptr,
                  float mx, float my, int key, uint32_t mods) {
    if (!cb_ptr) return;
    HtmluiEvent ev;
    ev.target    = node;
    ev.mouse_x   = mx;
    ev.mouse_y   = my;
    ev.key_code  = key;
    ev.modifiers = mods;
    ev.user_data = ud_ptr;
    /* Cast: HtmluiEventCb is void(*)(HtmluiEvent*), we store as void(*)(void*).
     * Calling convention is identical on all supported platforms. */
    ((HtmluiEventCb)cb_ptr)(&ev);
}

/* Mark a node and all ancestors dirty */
static void mark_dirty(__html_node__* node) {
    while (node) {
        node->is_dirty = true;
        node = node->parent;
    }
}

/* =========================================================================
 * Init
 * ========================================================================= */

void event_state_init(EventState* es) {
    memset(es, 0, sizeof(*es));
}

/* =========================================================================
 * Hit testing
 *
 * Walk the tree depth-first. Check children LAST (front-to-back reversal:
 * children are rendered on top of parents, so we test children first).
 * ========================================================================= */

__html_node__* event_hit_test(__html_node__* root, float x, float y) {
    if (!root || !root->style || !root->layout) return NULL;
    if (strcmp(root->tag, "#text") == 0) return NULL;
    if (root->style->display == DISPLAY_NONE) return NULL;
    if (!root->style->pointer_events) return NULL;

    LayoutBox* b = root->layout;
    /* Broad check — is point inside this node's box? */
    if (x < b->x || x >= b->x + b->width) return NULL;
    if (y < b->y || y >= b->y + b->height) return NULL;

    /* Test children front-to-back (last child is on top) */
    for (int i = root->child_count - 1; i >= 0; i--) {
        __html_node__* hit = event_hit_test(root->children[i], x, y);
        if (hit) return hit;
    }

    /* No child matched — this node is the hit target */
    return root;
}

/* =========================================================================
 * Mouse move
 * ========================================================================= */

int event_mouse_move(EventState* es, __html_node__* root, float x, float y) {
    es->mouse_x = x;
    es->mouse_y = y;

    __html_node__* now = event_hit_test(root, x, y);
    int flags = EV_NONE;

    if (now != es->hovered) {
        /* Leave old node */
        if (es->hovered) {
            es->hovered->is_hovered = false;
            mark_dirty(es->hovered);
            /* key_code == 0 → leave */
            fire(es->hovered, es->hovered->cb_hover,
                 es->hovered->cb_userdata_hover,
                 x, y, 0, 0);
            flags |= EV_DIRTY;
        }
        /* Enter new node */
        if (now) {
            now->is_hovered = true;
            mark_dirty(now);
            /* key_code == 1 → enter */
            fire(now, now->cb_hover, now->cb_userdata_hover,
                 x, y, 1, 0);
            flags |= EV_DIRTY | EV_CONSUMED;
        }
        es->hovered = now;
    }
    return flags;
}

/* =========================================================================
 * Mouse down
 * ========================================================================= */

int event_mouse_down(EventState* es, __html_node__* root, int button) {
    (void)button;
    __html_node__* node = event_hit_test(root, es->mouse_x, es->mouse_y);
    es->pressed = node;

    int flags = EV_NONE;
    if (node) {
        /* Update focus */
        if (es->focused && es->focused != node) {
            es->focused->is_focused = false;
            mark_dirty(es->focused);
            flags |= EV_DIRTY;
        }
        node->is_focused = true;
        es->focused = node;
        mark_dirty(node);

        fire(node, node->cb_mousedown, node->cb_userdata_click,
             es->mouse_x, es->mouse_y, 0, 0);
        flags |= EV_DIRTY | EV_CONSUMED;
    }
    return flags;
}

/* =========================================================================
 * Mouse up — fire click if same node
 * ========================================================================= */

int event_mouse_up(EventState* es, __html_node__* root, int button) {
    (void)button;
    __html_node__* node = event_hit_test(root, es->mouse_x, es->mouse_y);
    int flags = EV_NONE;

    if (node) {
        fire(node, node->cb_mouseup, node->cb_userdata_click,
             es->mouse_x, es->mouse_y, 0, 0);
        flags |= EV_CONSUMED;

        /* Click = down and up on the same node */
        if (node == es->pressed) {
            fire(node, node->cb_click, node->cb_userdata_click,
                 es->mouse_x, es->mouse_y, 0, 0);
            mark_dirty(node);
            flags |= EV_DIRTY;
        }
    }
    es->pressed = NULL;
    return flags;
}

/* =========================================================================
 * Keyboard
 * ========================================================================= */

int event_key_down(EventState* es, int key_code, uint32_t modifiers) {
    if (!es->focused) return EV_NONE;
    fire(es->focused, es->focused->cb_keydown,
         es->focused->cb_userdata_keydown,
         es->mouse_x, es->mouse_y, key_code, modifiers);
    mark_dirty(es->focused);
    return EV_DIRTY | EV_CONSUMED;
}

int event_key_up(EventState* es, int key_code, uint32_t modifiers) {
    if (!es->focused) return EV_NONE;
    fire(es->focused, es->focused->cb_keydown,
         es->focused->cb_userdata_keydown,
         es->mouse_x, es->mouse_y, key_code, modifiers);
    return EV_CONSUMED;
}

int event_quit(EventState* es) {
    (void)es;
    return EV_QUIT;
}
