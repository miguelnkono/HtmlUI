#define _POSIX_C_SOURCE 200809L
/**
 * src/paint/paint.c — Display list builder.
 *
 * Walks the layout tree in painter's order and emits DrawCommands.
 */

#include "paint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int colors_eq(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* Collect all visible text content from a node's #text children */
static char *collect_text(const __html_node__ *node) {
  /* Direct text_content on the node itself */
  if (node->text_content && strlen(node->text_content) > 0)
    return strdup(node->text_content);
  /* Otherwise look for #text child nodes */
  for (int i = 0; i < node->child_count; i++) {
    __html_node__ *ch = node->children[i];
    if (strcmp(ch->tag, "#text") == 0 && ch->text_content &&
        strlen(ch->text_content) > 0)
      return strdup(ch->text_content);
  }
  return NULL;
}

static void paint_node(__html_node__ *node, __display_list__ *dl) {
  if (!node || !node->style || !node->layout)
    return;
  if (strcmp(node->tag, "#text") == 0)
    return;
  if (node->style->display == DISPLAY_NONE)
    return;

  __computed_style__ *s = node->style;
  LayoutBox *b = node->layout;

  /* Skip truly invisible nodes (zero-size, fully transparent) */
  if (b->width < 0.5f && b->height < 0.5f)
    return;

  /* Clip push */
  if (s->overflow_hidden) {
    DrawCommand clip = {0};
    clip.type = CMD_CLIP_PUSH;
    clip.x = b->x;
    clip.y = b->y;
    clip.w = b->width;
    clip.h = b->height;
    display_list_push(dl, clip);
  }

  /* Background rect */
  static const Color transparent = {0, 0, 0, 0};
  int has_bg = !colors_eq(s->background_color, transparent);
  int has_border = (s->border_width[0] > 0 || s->border_width[1] > 0 ||
                    s->border_width[2] > 0 || s->border_width[3] > 0);
  int has_radius = (s->border_radius[0] > 0 || s->border_radius[1] > 0 ||
                    s->border_radius[2] > 0 || s->border_radius[3] > 0);

  if (has_bg || has_border || has_radius) {
    DrawCommand rect = {0};
    rect.type = CMD_RECT;
    rect.x = b->x;
    rect.y = b->y;
    rect.w = b->width;
    rect.h = b->height;
    rect.fill_color = s->background_color;
    rect.border_color = s->border_color[0];
    rect.border_width = s->border_width[0];
    /* Use top-left radius as uniform radius (per-corner in future) */
    rect.border_radius = s->border_radius[0];
    rect.z_index = (int)s->z_index;
    display_list_push(dl, rect);
  }

  /* Children (recurse) */
  for (int i = 0; i < node->child_count; i++)
    paint_node(node->children[i], dl);

  /* Text */
  char *text = collect_text(node);
  if (text) {
    DrawCommand cmd = {0};
    cmd.type = CMD_TEXT;
    /* Position text inside the content area */
    cmd.x = b->content_x;
    cmd.y = b->content_y;
    cmd.w = b->content_width;
    cmd.h = b->content_height;
    cmd.text = text; /* ownership transferred to display list */
    cmd.font_size = s->font_size;
    cmd.text_color = s->color;
    cmd.font_family = s->font_family ? strdup(s->font_family) : NULL;
    cmd.text_align = s->text_align;
    cmd.z_index = (int)s->z_index;
    display_list_push(dl, cmd);
  }

  /* Clip pop */
  if (s->overflow_hidden) {
    DrawCommand clip = {0};
    clip.type = CMD_CLIP_POP;
    display_list_push(dl, clip);
  }
}

/* Public API */

void paint_build(__html_node__ *root, __display_list__ *dl) {
  if (!root || !dl)
    return;
  display_list_clear(dl);
  paint_node(root, dl);
}

void paint_dump(const __display_list__ *dl) {
  if (!dl)
    return;
  printf("__display_list__ (%d commands)\n", dl->count);
  printf("==============================\n");
  for (int i = 0; i < dl->count; i++) {
    const DrawCommand *c = &dl->commands[i];
    switch (c->type) {
    case CMD_RECT:
      printf("[%3d] RECT      x=%.0f y=%.0f w=%.0f h=%.0f  "
             "fill=rgba(%d,%d,%d,%d)  border=%.0fpx  radius=%.0f\n",
             i, c->x, c->y, c->w, c->h, c->fill_color.r, c->fill_color.g,
             c->fill_color.b, c->fill_color.a, c->border_width,
             c->border_radius);
      break;
    case CMD_TEXT:
      printf("[%3d] TEXT      x=%.0f y=%.0f w=%.0f  "
             "font=%.0fpx  color=rgba(%d,%d,%d,%d)  \"%s\"\n",
             i, c->x, c->y, c->w, c->font_size, c->text_color.r,
             c->text_color.g, c->text_color.b, c->text_color.a,
             c->text ? c->text : "");
      break;
    case CMD_CLIP_PUSH:
      printf("[%3d] CLIP_PUSH x=%.0f y=%.0f w=%.0f h=%.0f\n", i, c->x, c->y,
             c->w, c->h);
      break;
    case CMD_CLIP_POP:
      printf("[%3d] CLIP_POP\n", i);
      break;
    case CMD_IMAGE:
      printf("[%3d] IMAGE     x=%.0f y=%.0f w=%.0f h=%.0f  id=%u\n", i, c->x,
             c->y, c->w, c->h, c->texture_id);
      break;
    }
  }
}
