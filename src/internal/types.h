/**
 * internal/types.h
 *
 * Internal data structures shared across all the modules.
 * Not included in the public API.
 *
 * Pipeline data flow:
 *
 *  __html_node__ (DOM tree)          < produced by parser/html.c
 *  CssRule  (rule list)         < produced by parser/css.c
 *  __computed_style__ (per node)     < produced by style/cascade.c
 *  LayoutBox     (per node)     < produced by layout/layout.c
 *  DrawCommand   (display list) < produced by paint/display_list.c
 */

#ifndef HTMLUI_INTERNAL_TYPES_H
#define HTMLUI_INTERNAL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// color type
// r > red
// g > green
// b > blue
// a > alpha, which is the opacity
typedef struct
{
  uint8_t r, g, b, a;
} Color;

#define COLOR_TRANSPARENT ((Color){0, 0, 0, 0})
#define COLOR_BLACK ((Color){0, 0, 0, 255})
#define COLOR_WHITE ((Color){255, 255, 255, 255})

/* Parse "#rrggbb" or "#rgb" or "rgb(r,g,b)" into a Color. */
Color color_parse(const char *str);

/**
 * Attribute list  (key → value string pairs)
 */
typedef struct
{
  char *key;
  char *value;
} __htmlattr__;

typedef struct
{
  __htmlattr__ *items;
  int count;
  int capacity;
} __htmlattr_list__;

void htmlattr_list_init(__htmlattr_list__ *list);
void htmlattr_list_set(__htmlattr_list__ *list, const char *key,
                       const char *value);
const char *htmlattr_list_get(const __htmlattr_list__ *list, const char *key);
void htmlattr_list_free(__htmlattr_list__ *list);

/**
 * DOM Node
 *
 * Represents one element in the HTML document tree.
 * Text nodes (raw text content between tags) are stored as the
 * text_content field of their parent element node.
 */
/* Forward declarations */
struct html_node_t;
struct computed_style_t;
struct layout_box_t;

typedef struct html_node_t
{
  /* Identity */
  char *tag;
  char *text_content;
  __htmlattr_list__ attrs;

  /* Tree structure */
  struct html_node_t *parent;
  struct UI_t *owner_ui; /* back-pointer to the owning UI context */
  struct html_node_t **children;
  int child_count;
  int child_capacity;

  /* populated in later passes */
  struct computed_style_t *style;
  struct layout_box_t *layout;

  /* Event callbacks — set by the user via node_on_*() */
  /* We use a generic function pointer type to avoid including htmlui.h here.
   * Stored and cast through HtmluiEventCb in api.c. */
  void (*cb_click)(void *);
  void (*cb_mousedown)(void *);
  void (*cb_mouseup)(void *);
  void (*cb_hover)(void *);
  void (*cb_keydown)(void *);
  void (*cb_keyup)(void *);
  void (*cb_change)(void *);
  void *cb_userdata_click;
  void *cb_userdata_hover;
  void *cb_userdata_keydown;
  void *cb_userdata_change;

  /* State */
  bool is_hovered;
  bool is_focused;
  bool is_dirty;       /* true → needs re-layout + repaint             */
  bool force_hidden;   /* set by node_set_visible(node, false)         */
  char *saved_display; /* stores display value before hide             */
} __html_node__;

__html_node__ *htmlnode_create(const char *tag);
void htmlnode_append_child(__html_node__ *parent, __html_node__ *child);
void htmlnode_free(__html_node__ *node); /* recursive, frees entire subtree */

/* Convenience: get attribute shortcuts */
const char *htmlnode_id(const __html_node__ *node);
const char *htmlnode_class(const __html_node__ *node);

/* =========================================================================
 * CSS Rule
 *
 * Represents one parsed CSS rule: a selector and its declarations.
 * ========================================================================= */

typedef struct
{
  char *property;
  char *value;
} __css_declaration__;

typedef struct
{
  char *selector;
  __css_declaration__ *declarations;
  int decl_count;
  int decl_capacity;
  int source_order; /* Index in file — used for cascade      */
} __css_rule__;

typedef struct
{
  __css_rule__ *rules;
  int count;
  int capacity;
} __css_rule_list__;

void css_rule_list_init(__css_rule_list__ *list);
void css_rule_list_add(__css_rule_list__ *list, __css_rule__ rule);
void css_rule_list_free(__css_rule_list__ *list);

/* =========================================================================
 * Display type + flex enums (resolved from "display", "flex-direction", …)
 * ========================================================================= */
typedef enum
{
  DISPLAY_BLOCK,
  DISPLAY_FLEX,
  DISPLAY_INLINE,
  DISPLAY_INLINE_BLOCK,
  DISPLAY_NONE,
} __display_type__;

typedef enum
{
  FLEX_DIRECTION_ROW,
  FLEX_DIRECTION_ROW_REVERSE,
  FLEX_DIRECTION_COLUMN,
  FLEX_DIRECTION_COLUMN_REVERSE,
} __flex_direction__;

typedef enum
{
  JUSTIFY_FLEX_START,
  JUSTIFY_FLEX_END,
  JUSTIFY_CENTER,
  JUSTIFY_SPACE_BETWEEN,
  JUSTIFY_SPACE_AROUND,
  JUSTIFY_SPACE_EVENLY,
} __justify_content__;

typedef enum
{
  ALIGN_FLEX_START,
  ALIGN_FLEX_END,
  ALIGN_CENTER,
  ALIGN_STRETCH,
  ALIGN_BASELINE,
} __align_items__;

typedef enum
{
  POSITION_STATIC,
  POSITION_RELATIVE,
  POSITION_ABSOLUTE,
} __position_type__;

/* =========================================================================
 * __computed_style__
 *
 * The fully-resolved style for one node after the CSS cascade.
 * All values are absolute (px). No percentages, no "auto" keywords.
 * Populated by style/cascade.c.
 * ========================================================================= */

#define SIDE_TOP 0
#define SIDE_RIGHT 1
#define SIDE_BOTTOM 2
#define SIDE_LEFT 3

typedef struct computed_style_t
{
  /* Box model */
  float width;
  float height;
  float min_width;
  float min_height;
  float max_width;
  float max_height;
  float margin[4];
  float padding[4];
  float border_width[4];
  float border_radius[4];

  /* Colors */
  Color background_color;
  Color color;
  Color border_color[4];

  /* Typography */
  float font_size;
  char *font_family;
  float line_height;
  int font_weight;
  bool font_italic;
  int text_align;

  /* Layout model */
  __display_type__ display;
  __position_type__ position;
  float top, right, bottom, left;
  float z_index;

  /* Flexbox */
  __flex_direction__ flex_direction;
  __justify_content__ justify_content;
  __align_items__ align_items;
  __align_items__ align_self;
  float flex_grow;
  float flex_shrink;
  float flex_basis;
  bool flex_wrap;
  float gap;

  /* Misc */
  float opacity;
  bool overflow_hidden;
  bool pointer_events;
} __computed_style__;

/* Return a __computed_style__ filled with CSS initial values. */
__computed_style__ computed_style_defaults(void);

/**
 * LayoutBox
 *
 * Pixel-exact position and size for one node, after layout.
 * Populated by layout/layout.c.
 */
typedef struct layout_box_t
{
  float x, y;          /* Top-left corner, in window-space pixels      */
  float width, height; /* Outer size (includes padding + border)       */

  /* Content area (inside padding) */
  float content_x, content_y;
  float content_width, content_height;
} LayoutBox;

/**
 * DrawCommand  (display list entry)
 *
 * A single, self-contained paint instruction consumed by the renderer.
 * Produced by paint/display_list.c.
 */
typedef enum
{
  CMD_RECT,      /* Filled rectangle, optional border + rounded corners */
  CMD_TEXT,      /* Text string at a position                           */
  CMD_IMAGE,     /* Blit a pre-loaded texture                           */
  CMD_CLIP_PUSH, /* Push a clipping rectangle                           */
  CMD_CLIP_POP,  /* Pop the last clipping rectangle                     */
} __draw_cmd_type__;

typedef struct
{
  __draw_cmd_type__ type;

  /* Position and size (all commands) */
  float x, y, w, h;

  /* CMD_RECT */
  Color fill_color;
  Color border_color;
  float border_width;
  float border_radius;

  /* CMD_TEXT */
  char *text;
  float font_size;
  Color text_color;
  char *font_family;
  int text_align;

  /* CMD_IMAGE */
  uint32_t texture_id;

  /* Z-order hint (painter's algorithm — lower = drawn first) */
  int z_index;
} DrawCommand;

typedef struct
{
  DrawCommand *commands;
  int count;
  int capacity;
} __display_list__;

void display_list_init(__display_list__ *dl);
void display_list_push(__display_list__ *dl, DrawCommand cmd);
void display_list_clear(__display_list__ *dl);
void display_list_free(__display_list__ *dl);

/**
 * UI  (opaque forward declaration)
 *
 * The full struct is defined in src/api/ui_internal.h which includes
 * the public htmlui.h (for HtmluiOptions). Internal pipeline modules
 * only need this forward declaration.
 */
typedef struct UI_t __ui__;

/* Node is the public-facing alias for __html_node__.
 * The public header (htmlui.h) uses 'Node'; internals use '__html_node__'.
 * They are the same struct. */
typedef struct html_node_t node_t;

#endif /* HTMLUI_INTERNAL_TYPES_H */
