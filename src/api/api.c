#define _POSIX_C_SOURCE 200809L

#include "../../include/htmlui.h"
#include "../layout/layout.h"
#include "../paint/paint.h"
#include "../parser/css.h"
#include "../parser/html.h"
#include "../style/cascade.h"
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal helpers
 * ========================================================================= */
static void set_error(UI *ui, const char *msg) {
  if (ui)
    snprintf(ui->last_error, sizeof(ui->last_error), "%s", msg);
}

static void set_owner_ui(__html_node__ *node, UI *ui) {
  if (!node)
    return;
  node->owner_ui = ui;
  for (int i = 0; i < node->child_count; i++)
    set_owner_ui(node->children[i], ui);
}

static void repaint(UI *ui) {
  paint_build(ui->dom_root, &ui->display_list);
#ifdef HTMLUI_SDL3
  if (ui->sdl3) {
    Color bg = (ui->dom_root && ui->dom_root->style)
                   ? ui->dom_root->style->background_color
                   : (Color){15, 17, 23, 255};
    sdl3_renderer_begin_frame(ui->sdl3, bg);
    sdl3_renderer_draw(ui->sdl3, &ui->display_list);
    sdl3_renderer_end_frame(ui->sdl3);
    return;
  }
#endif
  if (ui->soft) {
    Color bg = {15, 17, 23, 255};
    if (ui->dom_root && ui->dom_root->style)
      bg = ui->dom_root->style->background_color;
    soft_renderer_clear(ui->soft, bg);
    soft_renderer_draw(ui->soft, &ui->display_list);
  }
}

static int run_pipeline(UI *ui) {
  if (!ui->dom_root)
    return -1;
  cascade_apply(ui->dom_root, &ui->css_rules, (float)ui->window_w,
                (float)ui->window_h);
  layout_run(ui->dom_root, (float)ui->window_w, (float)ui->window_h);
  repaint(ui);
  ui->dirty = false;
  return 0;
}

static void update_text_input(UI *ui) {
#ifdef HTMLUI_SDL3
  if (!ui->sdl3)
    return;
  SDL_Window *win = sdl3_renderer_get_window(ui->sdl3);
  Node *focused = ui->events.focused;
  if (focused && strcmp(focused->tag, "input") == 0)
    SDL_StartTextInput(win);
  else
    SDL_StopTextInput(win);
#else
  (void)ui;
#endif
}

extern int selector_matches(const char *selector, const __html_node__ *node);

static __html_node__ *query_one(__html_node__ *node, const char *sel) {
  if (!node)
    return NULL;
  if (strcmp(node->tag, "#text") != 0 && selector_matches(sel, node))
    return node;
  for (int i = 0; i < node->child_count; i++) {
    __html_node__ *found = query_one(node->children[i], sel);
    if (found)
      return found;
  }
  return NULL;
}
static void query_all_r(__html_node__ *node, const char *sel,
                        __html_node__ ***out, int *count, int *cap) {
  if (!node)
    return;
  if (strcmp(node->tag, "#text") != 0 && selector_matches(sel, node)) {
    if (*count >= *cap) {
      *cap = (*cap == 0) ? 8 : *cap * 2;
      *out = realloc(*out, sizeof(__html_node__ *) * (size_t)*cap);
    }
    (*out)[(*count)++] = node;
  }
  for (int i = 0; i < node->child_count; i++)
    query_all_r(node->children[i], sel, out, count, cap);
}

/* =========================================================================
 * Options / error string
 * ========================================================================= */
void htmlui_options_default(HtmluiOptions *opts) {
  if (!opts)
    return;
  opts->window_title = "htmlui";
  opts->window_width = 800;
  opts->window_height = 600;
  opts->resizable = true;
  opts->renderer = HTMLUI_RENDERER_SDL3;
  opts->vsync = true;
  opts->debug_layout = false;
  opts->debug_hover = false;
}
const char *htmlui_result_str(HtmluiResult r) {
  switch (r) {
  case HTMLUI_OK:
    return "OK";
  case HTMLUI_ERR_IO:
    return "IO error";
  case HTMLUI_ERR_PARSE_HTML:
    return "HTML parse error";
  case HTMLUI_ERR_PARSE_CSS:
    return "CSS parse error";
  case HTMLUI_ERR_LAYOUT:
    return "Layout error";
  case HTMLUI_ERR_RENDER_INIT:
    return "Renderer init error";
  case HTMLUI_ERR_NULL:
    return "NULL pointer";
  case HTMLUI_ERR_NOT_FOUND:
    return "Not found";
  case HTMLUI_ERR_OOM:
    return "Out of memory";
  default:
    return "Unknown error";
  }
}

/* =========================================================================
 * ui_load
 * ========================================================================= */
UI *ui_load(const char *html_path, const char *css_path,
            const HtmluiOptions *opts) {
  UI *ui = calloc(1, sizeof(UI));
  if (!ui)
    return NULL;

  htmlui_options_default(&ui->options);
  if (opts)
    ui->options = *opts;

  ui->window_w = ui->options.window_width;
  ui->window_h = ui->options.window_height;
  ui->running = true;
  ui->dirty = true;

  event_state_init(&ui->events);
  display_list_init(&ui->display_list);
  css_rule_list_init(&ui->css_rules);

  ui->html_path = html_path ? strdup(html_path) : NULL;
  ui->css_path = css_path ? strdup(css_path) : NULL;

  if (html_path)
    ui->dom_root =
        html_parse_file(html_path, ui->last_error, sizeof(ui->last_error));
  else
    ui->dom_root = html_parse_string("<html><body></body></html>",
                                     ui->last_error, sizeof(ui->last_error));
  if (!ui->dom_root) {
    ui_destroy(ui);
    return NULL;
  }

  if (css_path) {
    if (css_parse_file(css_path, &ui->css_rules, ui->last_error,
                       sizeof(ui->last_error)) != 0) {
      ui_destroy(ui);
      return NULL;
    }
  }

  ui->soft = soft_renderer_create(ui->window_w, ui->window_h);
  if (!ui->soft) {
    set_error(ui, "soft renderer OOM");
    ui_destroy(ui);
    return NULL;
  }

#ifdef HTMLUI_SDL3
  if (ui->options.renderer == HTMLUI_RENDERER_SDL3) {
    ui->sdl3 = sdl3_renderer_create(ui->options.window_title, ui->window_w,
                                    ui->window_h, ui->options.resizable,
                                    ui->options.vsync);
    if (!ui->sdl3) {
      snprintf(ui->last_error, sizeof(ui->last_error),
               "SDL3 init failed — falling back to software renderer");
      fprintf(stderr, "[htmlui] %s\n", ui->last_error);
    }
  }
#endif

  set_owner_ui(ui->dom_root, ui);
  run_pipeline(ui);
  return ui;
}

/* =========================================================================
 * ui_reload  (FIX: set_owner_ui before run_pipeline)
 * ========================================================================= */
HtmluiResult ui_reload(UI *ui) {
  if (!ui)
    return HTMLUI_ERR_NULL;

  htmlnode_free(ui->dom_root);
  ui->dom_root = NULL;
  css_rule_list_free(&ui->css_rules);
  css_rule_list_init(&ui->css_rules);

  if (ui->html_path) {
    ui->dom_root =
        html_parse_file(ui->html_path, ui->last_error, sizeof(ui->last_error));
    if (!ui->dom_root)
      return HTMLUI_ERR_PARSE_HTML;
  }
  if (ui->css_path) {
    if (css_parse_file(ui->css_path, &ui->css_rules, ui->last_error,
                       sizeof(ui->last_error)) != 0)
      return HTMLUI_ERR_PARSE_CSS;
  }

  /* FIX: set owner before pipeline runs so mark_dirty() can propagate */
  set_owner_ui(ui->dom_root, ui);
  ui->dirty = true;
  run_pipeline(ui);
  return HTMLUI_OK;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
bool ui_is_running(UI *ui) { return ui && ui->running; }

void ui_destroy(UI *ui) {
  if (!ui)
    return;
  htmlnode_free(ui->dom_root);
  css_rule_list_free(&ui->css_rules);
  display_list_free(&ui->display_list);
  soft_renderer_destroy(ui->soft);
  free(ui->html_path);
  free(ui->css_path);
#ifdef HTMLUI_SDL3
  sdl3_renderer_destroy(ui->sdl3);
#endif
  free(ui);
}

const char *ui_last_error(UI *ui) {
  return ui ? ui->last_error : "NULL ui handle";
}

/* =========================================================================
 * Query
 * ========================================================================= */
Node *ui_query(UI *ui, const char *selector) {
  if (!ui || !selector || !ui->dom_root)
    return NULL;
  return query_one(ui->dom_root, selector);
}
Node **ui_query_all(UI *ui, const char *selector, int *out_count) {
  if (out_count)
    *out_count = 0;
  if (!ui || !selector || !ui->dom_root)
    return NULL;
  __html_node__ **result = NULL;
  int count = 0, cap = 0;
  query_all_r(ui->dom_root, selector, &result, &count, &cap);
  if (out_count)
    *out_count = count;
  return result;
}
Node *node_parent(Node *n) { return n ? n->parent : NULL; }
Node *node_child(Node *n, int i) {
  return (n && i >= 0 && i < n->child_count) ? n->children[i] : NULL;
}
int node_child_count(Node *n) { return n ? n->child_count : 0; }
const char *node_tag(Node *n) { return n ? n->tag : NULL; }
const char *node_get_attr(Node *n, const char *a) {
  if (!n || !a)
    return NULL;
  return htmlattr_list_get(&n->attrs, a);
}

/* =========================================================================
 * Mutation helpers
 * ========================================================================= */
static void node_mark_dirty(Node *node) {
  if (!node)
    return;
  node->is_dirty = true;
  if (node->owner_ui)
    node->owner_ui->dirty = true;
}

void node_set_text(Node *node, const char *text) {
  if (!node)
    return;
  free(node->text_content);
  node->text_content = text ? strdup(text) : NULL;
  for (int i = 0; i < node->child_count; i++) {
    if (strcmp(node->children[i]->tag, "#text") == 0) {
      free(node->children[i]->text_content);
      node->children[i]->text_content = text ? strdup(text) : NULL;
      break;
    }
  }
  node_mark_dirty(node);
}

void node_set_attr(Node *node, const char *attr, const char *value) {
  if (!node || !attr)
    return;
  htmlattr_list_set(&node->attrs, attr, value);
  node_mark_dirty(node);
}

/* FIX: node_set_style() replaces an existing property in the inline style
 * string in-place instead of always appending. The old implementation grew
 * the style attribute by N bytes on every call with the same property,
 * eventually hitting the 4096-byte buffer and losing declarations. */
void node_set_style(Node *node, const char *property, const char *value) {
  if (!node || !property)
    return;
  const char *existing = htmlattr_list_get(&node->attrs, "style");

  char out[4096] = {0};
  int found = 0;

  if (existing) {
    /* Walk the existing declarations; replace matching property, keep rest. */
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", existing);
    char *sp = NULL, *d = strtok_r(tmp, ";", &sp);
    while (d) {
      /* Skip leading whitespace */
      while (*d == ' ')
        d++;
      if (*d == '\0') {
        d = strtok_r(NULL, ";", &sp);
        continue;
      }

      char *colon = strchr(d, ':');
      if (colon) {
        *colon = '\0';
        /* Trim the property token */
        char *pk = d;
        while (*pk == ' ')
          pk++;
        char *pe = pk + strlen(pk) - 1;
        while (pe > pk && *pe == ' ')
          *pe-- = '\0';

        if (strcmp(pk, property) == 0) {
          /* Replace this declaration */
          size_t cur = strlen(out);
          snprintf(out + cur, sizeof(out) - cur, "%s%s: %s",
                   cur > 0 ? "; " : "", property, value);
          found = 1;
        } else {
          /* Keep the original declaration */
          size_t cur = strlen(out);
          snprintf(out + cur, sizeof(out) - cur, "%s%s: %s",
                   cur > 0 ? "; " : "", pk, colon + 1);
        }
      }
      d = strtok_r(NULL, ";", &sp);
    }
  }

  if (!found) {
    /* Property was not present — append */
    size_t cur = strlen(out);
    snprintf(out + cur, sizeof(out) - cur, "%s%s: %s", cur > 0 ? "; " : "",
             property, value);
  }

  htmlattr_list_set(&node->attrs, "style", out);
  node_mark_dirty(node);
}

void node_add_class(Node *node, const char *class_name) {
  if (!node || !class_name)
    return;
  const char *existing = htmlattr_list_get(&node->attrs, "class");
  if (!existing) {
    htmlattr_list_set(&node->attrs, "class", class_name);
  } else {
    if (node_has_class(node, class_name))
      return;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s %s", existing, class_name);
    htmlattr_list_set(&node->attrs, "class", buf);
  }
  node_mark_dirty(node);
}
void node_remove_class(Node *node, const char *class_name) {
  if (!node || !class_name)
    return;
  const char *existing = htmlattr_list_get(&node->attrs, "class");
  if (!existing)
    return;
  char buf[1024] = {0}, tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", existing);
  char *sp = NULL, *tok = strtok_r(tmp, " ", &sp);
  int first = 1;
  while (tok) {
    if (strcmp(tok, class_name) != 0) {
      if (!first)
        strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, tok, sizeof(buf) - strlen(buf) - 1);
      first = 0;
    }
    tok = strtok_r(NULL, " ", &sp);
  }
  htmlattr_list_set(&node->attrs, "class", buf);
  node_mark_dirty(node);
}
void node_toggle_class(Node *node, const char *class_name) {
  if (!node || !class_name)
    return;
  if (node_has_class(node, class_name))
    node_remove_class(node, class_name);
  else
    node_add_class(node, class_name);
}
bool node_has_class(Node *node, const char *class_name) {
  if (!node || !class_name)
    return false;
  const char *cls = htmlattr_list_get(&node->attrs, "class");
  if (!cls)
    return false;
  size_t nlen = strlen(class_name);
  const char *p = cls;
  while (*p) {
    while (*p == ' ')
      p++;
    const char *s = p;
    while (*p && *p != ' ')
      p++;
    if ((size_t)(p - s) == nlen && strncmp(s, class_name, nlen) == 0)
      return true;
  }
  return false;
}

void node_set_visible(Node *node, bool visible) {
  if (!node)
    return;
  if (!visible) {
    if (!node->force_hidden) {
      free(node->saved_display);
      node->saved_display = NULL;
      if (node->style) {
        switch (node->style->display) {
        case DISPLAY_FLEX:
          node->saved_display = strdup("flex");
          break;
        case DISPLAY_INLINE:
          node->saved_display = strdup("inline");
          break;
        case DISPLAY_INLINE_BLOCK:
          node->saved_display = strdup("inline-block");
          break;
        default:
          node->saved_display = strdup("block");
          break;
        }
      }
    }
    node->force_hidden = true;
  } else {
    node->force_hidden = false;
    if (node->saved_display) {
      node_set_style(node, "display", node->saved_display);
      free(node->saved_display);
      node->saved_display = NULL;
    }
  }
  node_mark_dirty(node);
}
bool node_is_visible(Node *node) { return node ? !node->force_hidden : false; }

/* =========================================================================
 * Event registration  (FIX: per-event userdata slots)
 * ========================================================================= */
void node_on_click(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  n->cb_click = (void (*)(void *))cb;
  n->cb_userdata_click = ud;
}
void node_on_mousedown(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  /* FIX: was writing ud to cb_userdata_click, clobbering the click handler's
   * data */
  n->cb_mousedown = (void (*)(void *))cb;
  n->cb_userdata_mousedown = ud;
}
void node_on_mouseup(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  /* FIX: same as above */
  n->cb_mouseup = (void (*)(void *))cb;
  n->cb_userdata_mouseup = ud;
}
void node_on_hover(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  n->cb_hover = (void (*)(void *))cb;
  n->cb_userdata_hover = ud;
}
void node_on_keydown(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  n->cb_keydown = (void (*)(void *))cb;
  n->cb_userdata_keydown = ud;
}
void node_on_keyup(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  /* FIX: was writing ud to cb_userdata_keydown */
  n->cb_keyup = (void (*)(void *))cb;
  n->cb_userdata_keyup = ud;
}
void node_on_change(Node *n, HtmluiEventCb cb, void *ud) {
  if (!n)
    return;
  n->cb_change = (void (*)(void *))cb;
  n->cb_userdata_change = ud;
}

void node_focus(Node *node) {
  if (!node)
    return;
  UI *ui = node->owner_ui;
  if (ui) {
    if (ui->events.focused && ui->events.focused != node)
      ui->events.focused->is_focused = false;
    ui->events.focused = node;
  }
  node->is_focused = true;
}
void ui_blur(UI *ui) {
  if (!ui)
    return;
  if (ui->events.focused) {
    ui->events.focused->is_focused = false;
    ui->events.focused = NULL;
  }
}

/* =========================================================================
 * Render loop
 * ========================================================================= */
void ui_poll_events(UI *ui) {
  if (!ui || !ui->running)
    return;
#ifdef HTMLUI_SDL3
  if (ui->sdl3) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_EVENT_QUIT:
        ui->running = false;
        break;
      case SDL_EVENT_KEY_DOWN:
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
          ui->running = false;
        } else if (e.key.scancode == SDL_SCANCODE_BACKSPACE) {
          Node *focused = ui->events.focused;
          if (focused && strcmp(focused->tag, "input") == 0) {
            const char *current = node_get_attr(focused, "value");
            if (current && strlen(current) > 0) {
              char buf[1024];
              snprintf(buf, sizeof(buf), "%s", current);
              size_t len = strlen(buf);
              while (len > 0 && (buf[len - 1] & 0xC0) == 0x80)
                len--;
              if (len > 0)
                len--;
              buf[len] = '\0';
              node_set_attr(focused, "value", buf);
              node_set_text(focused, buf);
              ui->dirty = true;
            }
          }
        } else {
          int flags = event_key_down(&ui->events, (int)e.key.scancode,
                                     (uint32_t)e.key.mod);
          if (flags & EV_DIRTY)
            ui->dirty = true;
        }
        break;
      case SDL_EVENT_KEY_UP: {
        int flags =
            event_key_up(&ui->events, (int)e.key.scancode, (uint32_t)e.key.mod);
        if (flags & EV_DIRTY)
          ui->dirty = true;
        break;
      }
      case SDL_EVENT_MOUSE_MOTION: {
        int flags =
            event_mouse_move(&ui->events, ui->dom_root, e.motion.x, e.motion.y);
        if (flags & EV_DIRTY)
          ui->dirty = true;
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int flags =
            event_mouse_down(&ui->events, ui->dom_root, e.button.button);
        if (flags & EV_DIRTY)
          ui->dirty = true;
        update_text_input(ui);
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        int flags = event_mouse_up(&ui->events, ui->dom_root, e.button.button);
        if (flags & EV_DIRTY)
          ui->dirty = true;
        update_text_input(ui);
        break;
      }
      case SDL_EVENT_TEXT_INPUT: {
        Node *focused = ui->events.focused;
        if (focused && strcmp(focused->tag, "input") == 0) {
          const char *current = node_get_attr(focused, "value");
          char buf[1024] = {0};
          if (current)
            snprintf(buf, sizeof(buf), "%s", current);
          strncat(buf, e.text.text, sizeof(buf) - strlen(buf) - 1);
          node_set_attr(focused, "value", buf);
          node_set_text(focused, buf);
          ui->dirty = true;
        }
        break;
      }
      case SDL_EVENT_WINDOW_RESIZED:
        ui->window_w = e.window.data1;
        ui->window_h = e.window.data2;
        if (ui->soft)
          soft_renderer_resize(ui->soft, ui->window_w, ui->window_h);
        cascade_apply(ui->dom_root, &ui->css_rules, (float)ui->window_w,
                      (float)ui->window_h);
        ui->dirty = true;
        break;
      default:
        break;
      }
    }
    return;
  }
#endif
  (void)ui;
}

void ui_render_frame(UI *ui) {
  if (!ui || !ui->running || !ui->dirty)
    return;
  cascade_apply(ui->dom_root, &ui->css_rules, (float)ui->window_w,
                (float)ui->window_h);
  layout_run(ui->dom_root, (float)ui->window_w, (float)ui->window_h);
  repaint(ui);
  ui->dirty = false;
}
void ui_invalidate(UI *ui) {
  if (ui)
    ui->dirty = true;
}

int ui_window_width(UI *ui) { return ui ? ui->window_w : 0; }
int ui_window_height(UI *ui) { return ui ? ui->window_h : 0; }

void node_get_rect(Node *node, float *ox, float *oy, float *ow, float *oh) {
  if (!node || !node->layout) {
    if (ox)
      *ox = 0;
    if (oy)
      *oy = 0;
    if (ow)
      *ow = 0;
    if (oh)
      *oh = 0;
    return;
  }
  if (ox)
    *ox = node->layout->x;
  if (oy)
    *oy = node->layout->y;
  if (ow)
    *ow = node->layout->width;
  if (oh)
    *oh = node->layout->height;
}
