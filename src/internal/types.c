#define _POSIX_C_SOURCE 200809L
/**
 * internal/types.c
 *
 * Implementations of the shared data structure helpers defined in types.h:
 *  - Color parsing
 *  - __htmlattr_list__
 *  - __html_node__ create / append / free
 *  - __css_rule_list__
 *  - __display_list__
 *  - __computed_style__ defaults
 */

#include "types.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Color
 */
static uint8_t hex2(const char *s)
{
  uint8_t hi = 0, lo = 0;
  char c = s[0];
  hi = (c >= '0' && c <= '9')   ? (c - '0')
       : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
       : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)
                                : 0;
  c = s[1];
  lo = (c >= '0' && c <= '9')   ? (c - '0')
       : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
       : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)
                                : 0;
  return (uint8_t)((hi << 4) | lo);
}

Color color_parse(const char *str)
{
  if (!str)
    return COLOR_TRANSPARENT;

  /* Skip leading whitespace */
  while (*str && isspace((unsigned char)*str))
    str++;

  /* #rrggbb or #rgb */
  if (*str == '#')
  {
    str++;
    size_t len = strlen(str);
    if (len >= 6)
    {
      return (Color){hex2(str), hex2(str + 2), hex2(str + 4), 255};
    }
    else if (len >= 3)
    {
      /* #rgb → #rrggbb */
      uint8_t r = hex2((char[]){str[0], str[0], '\0'});
      uint8_t g = hex2((char[]){str[1], str[1], '\0'});
      uint8_t b = hex2((char[]){str[2], str[2], '\0'});
      return (Color){r, g, b, 255};
    }
  }

  /* rgb(r, g, b) */
  if (strncmp(str, "rgb(", 4) == 0)
  {
    int r = 0, g = 0, b = 0;
    sscanf(str + 4, "%d , %d , %d", &r, &g, &b);
    return (Color){(uint8_t)r, (uint8_t)g, (uint8_t)b, 255};
  }

  /* rgba(r, g, b, a) */
  if (strncmp(str, "rgba(", 5) == 0)
  {
    int r = 0, g = 0, b = 0;
    float a = 1.0f;
    sscanf(str + 5, "%d , %d , %d , %f", &r, &g, &b, &a);
    return (Color){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)(a * 255)};
  }

  /* Named colours (small subset) */
  if (strcmp(str, "transparent") == 0)
    return COLOR_TRANSPARENT;
  if (strcmp(str, "black") == 0)
    return COLOR_BLACK;
  if (strcmp(str, "white") == 0)
    return COLOR_WHITE;
  if (strcmp(str, "red") == 0)
    return (Color){255, 0, 0, 255};
  if (strcmp(str, "green") == 0)
    return (Color){0, 128, 0, 255};
  if (strcmp(str, "blue") == 0)
    return (Color){0, 0, 255, 255};
  if (strcmp(str, "gray") == 0)
    return (Color){128, 128, 128, 255};
  if (strcmp(str, "grey") == 0)
    return (Color){128, 128, 128, 255};
  if (strcmp(str, "yellow") == 0)
    return (Color){255, 255, 0, 255};
  if (strcmp(str, "orange") == 0)
    return (Color){255, 165, 0, 255};
  if (strcmp(str, "purple") == 0)
    return (Color){128, 0, 128, 255};
  if (strcmp(str, "pink") == 0)
    return (Color){255, 192, 203, 255};
  if (strcmp(str, "cyan") == 0)
    return (Color){0, 255, 255, 255};
  if (strcmp(str, "magenta") == 0)
    return (Color){255, 0, 255, 255};

  return COLOR_TRANSPARENT;
}

/* =========================================================================
 * __htmlattr_list__
 * ========================================================================= */

void htmlattr_list_init(__htmlattr_list__ *list)
{
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

void htmlattr_list_set(__htmlattr_list__ *list, const char *key,
                       const char *value)
{
  if (!key)
    return;

  /* Update existing */
  for (int i = 0; i < list->count; i++)
  {
    if (strcmp(list->items[i].key, key) == 0)
    {
      free(list->items[i].value);
      list->items[i].value = value ? strdup(value) : strdup("");
      return;
    }
  }

  /* Append new */
  if (list->count >= list->capacity)
  {
    int new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
    __htmlattr__ *items = realloc(list->items, sizeof(__htmlattr__) * new_cap);
    if (!items)
      return;
    list->items = items;
    list->capacity = new_cap;
  }

  list->items[list->count].key = strdup(key);
  list->items[list->count].value = value ? strdup(value) : strdup("");
  list->count++;
}

const char *htmlattr_list_get(const __htmlattr_list__ *list, const char *key)
{
  if (!list || !key)
    return NULL;
  for (int i = 0; i < list->count; i++)
    if (strcmp(list->items[i].key, key) == 0)
      return list->items[i].value;
  return NULL;
}

void htmlattr_list_free(__htmlattr_list__ *list)
{
  for (int i = 0; i < list->count; i++)
  {
    free(list->items[i].key);
    free(list->items[i].value);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

/* =========================================================================
 * __html_node__
 * ========================================================================= */

__html_node__ *htmlnode_create(const char *tag)
{
  __html_node__ *node = calloc(1, sizeof(__html_node__));
  if (!node)
    return NULL;
  node->tag = strdup(tag ? tag : "div");
  htmlattr_list_init(&node->attrs);
  node->is_dirty = true;
  return node;
}

void htmlnode_append_child(__html_node__ *parent, __html_node__ *child)
{
  if (!parent || !child)
    return;

  if (parent->child_count >= parent->child_capacity)
  {
    int new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
    __html_node__ **children =
        realloc(parent->children, sizeof(__html_node__ *) * new_cap);
    if (!children)
      return;
    parent->children = children;
    parent->child_capacity = new_cap;
  }

  parent->children[parent->child_count++] = child;
  child->parent = parent;
  child->owner_ui = parent->owner_ui;
}

void htmlnode_free(__html_node__ *node)
{
  if (!node)
    return;

  /* Recurse into children */
  for (int i = 0; i < node->child_count; i++)
    htmlnode_free(node->children[i]);

  free(node->children);
  free(node->tag);
  free(node->text_content);
  free(node->saved_display);
  htmlattr_list_free(&node->attrs);

  /* style and layout are freed by their owning modules */
  free(node->style);
  free(node->layout);

  free(node);
}

const char *htmlnode_id(const __html_node__ *node)
{
  return htmlattr_list_get(&node->attrs, "id");
}

const char *htmlnode_class(const __html_node__ *node)
{
  return htmlattr_list_get(&node->attrs, "class");
}

/* =========================================================================
 * __css_rule_list__
 * ========================================================================= */

void css_rule_list_init(__css_rule_list__ *list)
{
  list->rules = NULL;
  list->count = 0;
  list->capacity = 0;
}

void css_rule_list_add(__css_rule_list__ *list, __css_rule__ rule)
{
  if (list->count >= list->capacity)
  {
    int new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
    __css_rule__ *rules = realloc(list->rules, sizeof(__css_rule__) * new_cap);
    if (!rules)
      return;
    list->rules = rules;
    list->capacity = new_cap;
  }
  list->rules[list->count++] = rule;
}

void css_rule_list_free(__css_rule_list__ *list)
{
  for (int i = 0; i < list->count; i++)
  {
    free(list->rules[i].selector);
    for (int j = 0; j < list->rules[i].decl_count; j++)
    {
      free(list->rules[i].declarations[j].property);
      free(list->rules[i].declarations[j].value);
    }
    free(list->rules[i].declarations);
  }
  free(list->rules);
  list->rules = NULL;
  list->count = 0;
  list->capacity = 0;
}

/* =========================================================================
 * __display_list__
 * ========================================================================= */

void display_list_init(__display_list__ *dl)
{
  dl->commands = NULL;
  dl->count = 0;
  dl->capacity = 0;
}

void display_list_push(__display_list__ *dl, DrawCommand cmd)
{
  if (dl->count >= dl->capacity)
  {
    int new_cap = dl->capacity == 0 ? 32 : dl->capacity * 2;
    DrawCommand *cmds = realloc(dl->commands, sizeof(DrawCommand) * new_cap);
    if (!cmds)
      return;
    dl->commands = cmds;
    dl->capacity = new_cap;
  }
  dl->commands[dl->count++] = cmd;
}

void display_list_clear(__display_list__ *dl)
{
  /* Free heap strings in text commands */
  for (int i = 0; i < dl->count; i++)
  {
    if (dl->commands[i].type == CMD_TEXT)
    {
      free(dl->commands[i].text);
      free(dl->commands[i].font_family);
    }
  }
  dl->count = 0;
}

void display_list_free(__display_list__ *dl)
{
  display_list_clear(dl);
  free(dl->commands);
  dl->commands = NULL;
  dl->capacity = 0;
}

/* =========================================================================
 * __computed_style__ defaults  (CSS initial values)
 * ========================================================================= */
__computed_style__ computed_style_defaults(void)
{
  __computed_style__ s;
  memset(&s, 0, sizeof(s));

  s.width = NAN;  /* auto */
  s.height = NAN; /* auto */
  s.min_width = 0.0f;
  s.min_height = 0.0f;
  s.max_width = NAN; /* unconstrained */
  s.max_height = NAN;

  for (int i = 0; i < 4; i++)
  {
    s.margin[i] = 0.0f;
    s.padding[i] = 0.0f;
    s.border_width[i] = 0.0f;
    s.border_radius[i] = 0.0f;
    s.border_color[i] = COLOR_TRANSPARENT;
  }

  s.background_color = COLOR_TRANSPARENT;
  s.color = COLOR_BLACK;

  s.font_size = 16.0f;
  s.font_family = NULL; /* renderer will use its default font */
  s.line_height = 1.2f;
  s.font_weight = 400;
  s.font_italic = false;
  s.text_align = 0; /* left */

  s.display = DISPLAY_BLOCK;
  s.position = POSITION_STATIC;
  s.top = s.right = s.bottom = s.left = NAN;
  s.z_index = 0.0f;

  s.flex_direction = FLEX_DIRECTION_ROW;
  s.justify_content = JUSTIFY_FLEX_START;
  s.align_items = ALIGN_STRETCH;
  s.align_self = ALIGN_FLEX_START;
  s.flex_grow = 0.0f;
  s.flex_shrink = 1.0f;
  s.flex_basis = NAN; /* auto */
  s.flex_wrap = false;
  s.gap = 0.0f;

  s.opacity = 1.0f;
  s.overflow_hidden = false;
  s.pointer_events = true;

  return s;
}
