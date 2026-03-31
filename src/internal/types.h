#ifndef HTMLUI_INTERNAL_TYPES_H
#define HTMLUI_INTERNAL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* =========================================================================
 * Color
 * ========================================================================= */
typedef struct {
  uint8_t r, g, b, a;
} Color;

#define COLOR_TRANSPARENT ((Color){0, 0, 0, 0})
#define COLOR_BLACK ((Color){0, 0, 0, 255})
#define COLOR_WHITE ((Color){255, 255, 255, 255})

Color color_parse(const char *str);

/* =========================================================================
 * Attribute list
 * ========================================================================= */
typedef struct {
  char *key;
  char *value;
} __htmlattr__;

typedef struct {
  __htmlattr__ *items;
  int count;
  int capacity;
} __htmlattr_list__;

void htmlattr_list_init(__htmlattr_list__ *list);
void htmlattr_list_set(__htmlattr_list__ *list, const char *key,
                       const char *value);
const char *htmlattr_list_get(const __htmlattr_list__ *list, const char *key);
void htmlattr_list_free(__htmlattr_list__ *list);

/* =========================================================================
 * DOM Node
 * ========================================================================= */
struct html_node_t;
struct computed_style_t;
struct layout_box_t;

typedef struct html_node_t {
  char *tag;
  char *text_content;
  __htmlattr_list__ attrs;

  struct html_node_t *parent;
  struct UI_t *owner_ui;
  struct html_node_t **children;
  int child_count;
  int child_capacity;

  struct computed_style_t *style;
  struct layout_box_t *layout;

  /* Event callbacks — stored as void(*)(void*), cast at call site */
  void (*cb_click)(void *);
  void (*cb_mousedown)(void *);
  void (*cb_mouseup)(void *);
  void (*cb_hover)(void *);
  void (*cb_keydown)(void *);
  void (*cb_keyup)(void *);
  void (*cb_change)(void *);

  /* Per-event user-data (separate slots — not shared) */
  void *cb_userdata_click;
  void *cb_userdata_mousedown; /* FIX: was missing, shared with click */
  void *cb_userdata_mouseup;   /* FIX: was missing, shared with click */
  void *cb_userdata_hover;
  void *cb_userdata_keydown;
  void *cb_userdata_keyup; /* FIX: was missing, shared with keydown */
  void *cb_userdata_change;

  bool is_hovered;
  bool is_focused;
  bool is_dirty;
  bool force_hidden;
  char *saved_display;
} __html_node__;

__html_node__ *htmlnode_create(const char *tag);
void htmlnode_append_child(__html_node__ *parent, __html_node__ *child);
void htmlnode_free(__html_node__ *node);

const char *htmlnode_id(const __html_node__ *node);
const char *htmlnode_class(const __html_node__ *node);

/* =========================================================================
 * CSS Rule
 * ========================================================================= */
typedef struct {
  char *property;
  char *value;
} __css_declaration__;

typedef struct {
  char *selector;
  __css_declaration__ *declarations;
  int decl_count;
  int decl_capacity;
  int source_order;
} __css_rule__;

typedef struct {
  __css_rule__ *rules;
  int count;
  int capacity;
} __css_rule_list__;

void css_rule_list_init(__css_rule_list__ *list);
void css_rule_list_add(__css_rule_list__ *list, __css_rule__ rule);
void css_rule_list_free(__css_rule_list__ *list);

/* =========================================================================
 * Display / flex enums
 * ========================================================================= */
typedef enum {
  DISPLAY_BLOCK,
  DISPLAY_FLEX,
  DISPLAY_INLINE,
  DISPLAY_INLINE_BLOCK,
  DISPLAY_NONE
} __display_type__;

typedef enum {
  FLEX_DIRECTION_ROW,
  FLEX_DIRECTION_ROW_REVERSE,
  FLEX_DIRECTION_COLUMN,
  FLEX_DIRECTION_COLUMN_REVERSE
} __flex_direction__;

typedef enum {
  JUSTIFY_FLEX_START,
  JUSTIFY_FLEX_END,
  JUSTIFY_CENTER,
  JUSTIFY_SPACE_BETWEEN,
  JUSTIFY_SPACE_AROUND,
  JUSTIFY_SPACE_EVENLY
} __justify_content__;

typedef enum {
  ALIGN_FLEX_START,
  ALIGN_FLEX_END,
  ALIGN_CENTER,
  ALIGN_STRETCH,
  ALIGN_BASELINE
} __align_items__;

typedef enum {
  POSITION_STATIC,
  POSITION_RELATIVE,
  POSITION_ABSOLUTE
} __position_type__;

/* =========================================================================
 * Computed style
 *
 * Sentinel values meaning "property was not explicitly set":
 *   float  → NAN  (test with isnan())
 *   Color  → COLOR_TRANSPARENT with a==0  (use color_is_set() helper below)
 *   char*  → NULL
 *   int    → -1   (font_weight, text_align use explicit -1 sentinel)
 *
 * This lets inherit_from_parent() distinguish "set to default" from
 * "never set" — fixing the magic-constant inheritance bug.
 * ========================================================================= */

#define SIDE_TOP 0
#define SIDE_RIGHT 1
#define SIDE_BOTTOM 2
#define SIDE_LEFT 3

/* Sentinel: align_self "not set by author" so parent align-items applies */
#define ALIGN_SELF_AUTO ((int)-1)

typedef struct computed_style_t {
  /* Box model */
  float width, height;
  float min_width, min_height;
  float max_width, max_height;
  float margin[4];
  float padding[4];
  float border_width[4];
  float border_radius[4];

  /* Colors */
  Color background_color;
  Color color;
  Color border_color[4];

  /* Typography — use NAN / NULL / -1 as "not set" sentinels */
  float font_size;   /* NAN = not set */
  char *font_family; /* NULL = not set */
  float line_height; /* NAN = not set */
  int font_weight;   /* -1 = not set  */
  int font_italic;   /* -1 = not set  */
  int text_align;    /* -1 = not set  */

  /* Layout model */
  __display_type__ display;
  __position_type__ position;
  float top, right, bottom, left;
  float z_index;

  /* Flexbox */
  __flex_direction__ flex_direction;
  __justify_content__ justify_content;
  __align_items__ align_items;
  int align_self; /* ALIGN_SELF_AUTO (-1) = not set */
  float flex_grow;
  float flex_shrink;
  float flex_basis;
  bool flex_wrap;
  float gap;

  /* Misc */
  float opacity;
  bool overflow_hidden;
  bool pointer_events;

  /* --- Inheritance sentinel flags ---
   * True means the property was explicitly authored (CSS rule or inline).
   * False means it came from computed_style_defaults() and may be
   * overridden by inheritance.  Only needed for properties whose default
   * value is the same as a valid authored value (e.g. font_weight:400).
   */
  bool _color_set;
  bool _font_size_set;
  bool _font_weight_set;
  bool _font_italic_set;
  bool _line_height_set;
  bool _text_align_set;
} __computed_style__;

__computed_style__ computed_style_defaults(void);

/* FIX: proper destructor that also frees font_family heap string */
void computed_style_free(__computed_style__ *s);

/* =========================================================================
 * LayoutBox
 * ========================================================================= */
typedef struct layout_box_t {
  float x, y;
  float width, height;
  float content_x, content_y;
  float content_width, content_height;
} LayoutBox;

/* =========================================================================
 * DrawCommand / display list
 * ========================================================================= */
typedef enum {
  CMD_RECT,
  CMD_TEXT,
  CMD_IMAGE,
  CMD_CLIP_PUSH,
  CMD_CLIP_POP
} __draw_cmd_type__;

typedef struct {
  __draw_cmd_type__ type;
  float x, y, w, h;

  Color fill_color;
  Color border_color;
  float border_width;
  float border_radius;

  char *text;
  float font_size;
  Color text_color;
  char *font_family;
  int text_align;

  uint32_t texture_id;
  int z_index;
} DrawCommand;

typedef struct {
  DrawCommand *commands;
  int count;
  int capacity;
} __display_list__;

void display_list_init(__display_list__ *dl);
void display_list_push(__display_list__ *dl, DrawCommand cmd);
void display_list_clear(__display_list__ *dl);
void display_list_free(__display_list__ *dl);

/* =========================================================================
 * UI forward declaration
 * ========================================================================= */
typedef struct UI_t __ui__;
typedef struct html_node_t node_t;

#endif /* HTMLUI_INTERNAL_TYPES_H */
