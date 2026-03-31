#define _POSIX_C_SOURCE 200809L

#include "events.h"
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */
static void fire(__html_node__ *node, void (*cb)(void *), void *userdata,
                 float mx, float my, int key, uint32_t mods) {
  if (!cb)
    return;
  HtmluiEvent ev;
  ev.target = node;
  ev.mouse_x = mx;
  ev.mouse_y = my;
  ev.key_code = key;
  ev.modifiers = mods;
  ev.user_data = userdata;
  ((HtmluiEventCb)cb)(&ev);
}

static void mark_dirty(__html_node__ *node) {
  while (node) {
    node->is_dirty = true;
    node = node->parent;
  }
}

/* =========================================================================
 * Init
 * ========================================================================= */
void event_state_init(EventState *es) { memset(es, 0, sizeof(*es)); }

/* =========================================================================
 * Hit test
 * ========================================================================= */
__html_node__ *event_hit_test(__html_node__ *root, float x, float y) {
  if (!root || !root->style || !root->layout)
    return NULL;
  if (strcmp(root->tag, "#text") == 0)
    return NULL;
  if (root->style->display == DISPLAY_NONE)
    return NULL;
  if (!root->style->pointer_events)
    return NULL;

  LayoutBox *b = root->layout;
  if (x < b->x || x >= b->x + b->width)
    return NULL;
  if (y < b->y || y >= b->y + b->height)
    return NULL;

  for (int i = root->child_count - 1; i >= 0; i--) {
    __html_node__ *hit = event_hit_test(root->children[i], x, y);
    if (hit)
      return hit;
  }
  return root;
}

/* =========================================================================
 * Mouse move
 * ========================================================================= */
int event_mouse_move(EventState *es, __html_node__ *root, float x, float y) {
  es->mouse_x = x;
  es->mouse_y = y;
  __html_node__ *now = event_hit_test(root, x, y);
  int flags = EV_NONE;

  if (now != es->hovered) {
    if (es->hovered) {
      es->hovered->is_hovered = false;
      mark_dirty(es->hovered);
      fire(es->hovered, es->hovered->cb_hover, es->hovered->cb_userdata_hover,
           x, y, 0, 0);
      flags |= EV_DIRTY;
    }
    if (now) {
      now->is_hovered = true;
      mark_dirty(now);
      fire(now, now->cb_hover, now->cb_userdata_hover, x, y, 1, 0);
      flags |= EV_DIRTY | EV_CONSUMED;
    }
    es->hovered = now;
  }
  return flags;
}

/* =========================================================================
 * Mouse down
 * ========================================================================= */
int event_mouse_down(EventState *es, __html_node__ *root, int button) {
  (void)button;
  __html_node__ *node = event_hit_test(root, es->mouse_x, es->mouse_y);
  es->pressed = node;
  int flags = EV_NONE;
  if (node) {
    if (es->focused && es->focused != node) {
      es->focused->is_focused = false;
      mark_dirty(es->focused);
      flags |= EV_DIRTY;
    }
    node->is_focused = true;
    es->focused = node;
    mark_dirty(node);
    /* FIX: use cb_userdata_mousedown, not cb_userdata_click */
    fire(node, node->cb_mousedown, node->cb_userdata_mousedown, es->mouse_x,
         es->mouse_y, 0, 0);
    flags |= EV_DIRTY | EV_CONSUMED;
  }
  return flags;
}

/* =========================================================================
 * Mouse up
 * ========================================================================= */
int event_mouse_up(EventState *es, __html_node__ *root, int button) {
  (void)button;
  __html_node__ *node = event_hit_test(root, es->mouse_x, es->mouse_y);
  int flags = EV_NONE;
  if (node) {
    /* FIX: use cb_userdata_mouseup, not cb_userdata_click */
    fire(node, node->cb_mouseup, node->cb_userdata_mouseup, es->mouse_x,
         es->mouse_y, 0, 0);
    flags |= EV_CONSUMED;
    if (node == es->pressed) {
      /* click = down + up on same node — click uses cb_userdata_click */
      fire(node, node->cb_click, node->cb_userdata_click, es->mouse_x,
           es->mouse_y, 0, 0);
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
int event_key_down(EventState *es, int key_code, uint32_t modifiers) {
  if (!es->focused)
    return EV_NONE;
  fire(es->focused, es->focused->cb_keydown, es->focused->cb_userdata_keydown,
       es->mouse_x, es->mouse_y, key_code, modifiers);
  mark_dirty(es->focused);
  return EV_DIRTY | EV_CONSUMED;
}

/* FIX: was calling cb_keydown instead of cb_keyup, and using
 * cb_userdata_keydown instead of cb_userdata_keyup. */
int event_key_up(EventState *es, int key_code, uint32_t modifiers) {
  if (!es->focused)
    return EV_NONE;
  fire(es->focused, es->focused->cb_keyup,
       es->focused->cb_userdata_keyup, /* FIX: correct slot */
       es->mouse_x, es->mouse_y, key_code, modifiers);
  return EV_CONSUMED;
}

int event_quit(EventState *es) {
  (void)es;
  return EV_QUIT;
}
