#define _POSIX_C_SOURCE 200809L
/**
 * internal/types.c
 *
 * Fixes vs original:
 *  1. computed_style_free() frees font_family before the struct itself.
 *  2. computed_style_defaults() uses NAN/-1/NULL sentinels for inheritable
 *     properties so inherit_from_parent() can distinguish "not set" from
 *     "explicitly set to the CSS initial value".
 *  3. htmlnode_free() calls computed_style_free() instead of plain free().
 */

#include "types.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Color
 * ========================================================================= */
static uint8_t hex_nib(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}
static uint8_t hex2(const char *s) {
    return (uint8_t)((hex_nib(s[0]) << 4) | hex_nib(s[1]));
}

Color color_parse(const char *str) {
    if (!str) return COLOR_TRANSPARENT;
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '#') {
        str++;
        size_t len = strlen(str);
        if (len >= 6) return (Color){hex2(str), hex2(str+2), hex2(str+4), 255};
        if (len >= 3) {
            char r2[3]={str[0],str[0],'\0'}, g2[3]={str[1],str[1],'\0'}, b2[3]={str[2],str[2],'\0'};
            return (Color){hex2(r2), hex2(g2), hex2(b2), 255};
        }
    }
    if (strncmp(str,"rgb(",4)==0) {
        int r=0,g=0,b=0; sscanf(str+4,"%d , %d , %d",&r,&g,&b);
        return (Color){(uint8_t)r,(uint8_t)g,(uint8_t)b,255};
    }
    if (strncmp(str,"rgba(",5)==0) {
        int r=0,g=0,b=0; float a=1.f; sscanf(str+5,"%d , %d , %d , %f",&r,&g,&b,&a);
        return (Color){(uint8_t)r,(uint8_t)g,(uint8_t)b,(uint8_t)(a*255)};
    }
    if (!strcmp(str,"transparent")) return COLOR_TRANSPARENT;
    if (!strcmp(str,"black"))   return COLOR_BLACK;
    if (!strcmp(str,"white"))   return COLOR_WHITE;
    if (!strcmp(str,"red"))     return (Color){255,0,0,255};
    if (!strcmp(str,"green"))   return (Color){0,128,0,255};
    if (!strcmp(str,"blue"))    return (Color){0,0,255,255};
    if (!strcmp(str,"gray")||!strcmp(str,"grey")) return (Color){128,128,128,255};
    if (!strcmp(str,"yellow"))  return (Color){255,255,0,255};
    if (!strcmp(str,"orange"))  return (Color){255,165,0,255};
    if (!strcmp(str,"purple"))  return (Color){128,0,128,255};
    if (!strcmp(str,"pink"))    return (Color){255,192,203,255};
    if (!strcmp(str,"cyan"))    return (Color){0,255,255,255};
    if (!strcmp(str,"magenta")) return (Color){255,0,255,255};
    return COLOR_TRANSPARENT;
}

/* =========================================================================
 * __htmlattr_list__
 * ========================================================================= */
void htmlattr_list_init(__htmlattr_list__ *list)
    { list->items=NULL; list->count=0; list->capacity=0; }

void htmlattr_list_set(__htmlattr_list__ *list, const char *key, const char *value) {
    if (!key) return;
    for (int i=0;i<list->count;i++) {
        if (strcmp(list->items[i].key,key)==0) {
            free(list->items[i].value);
            list->items[i].value = value ? strdup(value) : strdup("");
            return;
        }
    }
    if (list->count >= list->capacity) {
        int nc = list->capacity==0 ? 4 : list->capacity*2;
        __htmlattr__ *it = realloc(list->items, sizeof(__htmlattr__)*(size_t)nc);
        if (!it) return;
        list->items=it; list->capacity=nc;
    }
    list->items[list->count].key   = strdup(key);
    list->items[list->count].value = value ? strdup(value) : strdup("");
    list->count++;
}

const char *htmlattr_list_get(const __htmlattr_list__ *list, const char *key) {
    if (!list||!key) return NULL;
    for (int i=0;i<list->count;i++)
        if (strcmp(list->items[i].key,key)==0) return list->items[i].value;
    return NULL;
}

void htmlattr_list_free(__htmlattr_list__ *list) {
    for (int i=0;i<list->count;i++) { free(list->items[i].key); free(list->items[i].value); }
    free(list->items);
    list->items=NULL; list->count=0; list->capacity=0;
}

/* =========================================================================
 * __computed_style__ lifecycle  (FIX: separate destructor)
 * ========================================================================= */
void computed_style_free(__computed_style__ *s) {
    if (!s) return;
    free(s->font_family);   /* heap string — was leaked by bare free(node->style) */
    free(s);
}

__computed_style__ computed_style_defaults(void) {
    __computed_style__ s;
    memset(&s, 0, sizeof(s));

    s.width = s.height = s.max_width = s.max_height = NAN;
    s.min_width = s.min_height = 0.f;

    for (int i=0;i<4;i++) {
        s.margin[i]=s.padding[i]=s.border_width[i]=s.border_radius[i]=0.f;
        s.border_color[i] = COLOR_TRANSPARENT;
    }
    s.background_color = COLOR_TRANSPARENT;

    /* FIX: sentinel values for all inheritable properties.
     * NAN / NULL / -1 mean "not authored — may inherit from parent". */
    s.color          = COLOR_TRANSPARENT; s._color_set      = false;
    s.font_size      = NAN;               s._font_size_set  = false;
    s.font_family    = NULL;
    s.line_height    = NAN;               s._line_height_set= false;
    s.font_weight    = -1;                s._font_weight_set= false;
    s.font_italic    = -1;                s._font_italic_set= false;
    s.text_align     = -1;                s._text_align_set = false;

    s.display         = DISPLAY_BLOCK;
    s.position        = POSITION_STATIC;
    s.top=s.right=s.bottom=s.left = NAN;
    s.z_index         = 0.f;

    s.flex_direction  = FLEX_DIRECTION_ROW;
    s.justify_content = JUSTIFY_FLEX_START;
    s.align_items     = ALIGN_STRETCH;
    s.align_self      = ALIGN_SELF_AUTO;  /* FIX: -1, not 0 (ALIGN_FLEX_START) */
    s.flex_grow       = 0.f;
    s.flex_shrink     = 1.f;
    s.flex_basis      = NAN;
    s.flex_wrap       = false;
    s.gap             = 0.f;

    s.opacity         = 1.f;
    s.overflow_hidden = false;
    s.pointer_events  = true;
    return s;
}

/* =========================================================================
 * __html_node__
 * ========================================================================= */
__html_node__ *htmlnode_create(const char *tag) {
    __html_node__ *node = calloc(1, sizeof(__html_node__));
    if (!node) return NULL;
    node->tag      = strdup(tag ? tag : "div");
    node->is_dirty = true;
    htmlattr_list_init(&node->attrs);
    return node;
}
void htmlnode_append_child(__html_node__ *parent, __html_node__ *child) {
    if (!parent||!child) return;
    if (parent->child_count >= parent->child_capacity) {
        int nc = parent->child_capacity==0 ? 4 : parent->child_capacity*2;
        __html_node__ **ch = realloc(parent->children, sizeof(__html_node__*)*(size_t)nc);
        if (!ch) return;
        parent->children=ch; parent->child_capacity=nc;
    }
    parent->children[parent->child_count++] = child;
    child->parent   = parent;
    child->owner_ui = parent->owner_ui;
}
void htmlnode_free(__html_node__ *node) {
    if (!node) return;
    for (int i=0;i<node->child_count;i++) htmlnode_free(node->children[i]);
    free(node->children);
    free(node->tag);
    free(node->text_content);
    free(node->saved_display);
    htmlattr_list_free(&node->attrs);
    computed_style_free(node->style);  /* FIX: proper destructor, not bare free */
    free(node->layout);
    free(node);
}
const char *htmlnode_id(const __html_node__ *n)    { return htmlattr_list_get(&n->attrs,"id"); }
const char *htmlnode_class(const __html_node__ *n)  { return htmlattr_list_get(&n->attrs,"class"); }

/* =========================================================================
 * __css_rule_list__
 * ========================================================================= */
void css_rule_list_init(__css_rule_list__ *list)
    { list->rules=NULL; list->count=0; list->capacity=0; }
void css_rule_list_add(__css_rule_list__ *list, __css_rule__ rule) {
    if (list->count >= list->capacity) {
        int nc = list->capacity==0 ? 8 : list->capacity*2;
        __css_rule__ *rules = realloc(list->rules, sizeof(__css_rule__)*(size_t)nc);
        if (!rules) return;
        list->rules=rules; list->capacity=nc;
    }
    list->rules[list->count++] = rule;
}
void css_rule_list_free(__css_rule_list__ *list) {
    for (int i=0;i<list->count;i++) {
        free(list->rules[i].selector);
        for (int j=0;j<list->rules[i].decl_count;j++) {
            free(list->rules[i].declarations[j].property);
            free(list->rules[i].declarations[j].value);
        }
        free(list->rules[i].declarations);
    }
    free(list->rules);
    list->rules=NULL; list->count=0; list->capacity=0;
}

/* =========================================================================
 * __display_list__
 * ========================================================================= */
void display_list_init(__display_list__ *dl)
    { dl->commands=NULL; dl->count=0; dl->capacity=0; }
void display_list_push(__display_list__ *dl, DrawCommand cmd) {
    if (dl->count >= dl->capacity) {
        int nc = dl->capacity==0 ? 32 : dl->capacity*2;
        DrawCommand *cmds = realloc(dl->commands, sizeof(DrawCommand)*(size_t)nc);
        if (!cmds) return;
        dl->commands=cmds; dl->capacity=nc;
    }
    dl->commands[dl->count++] = cmd;
}
void display_list_clear(__display_list__ *dl) {
    for (int i=0;i<dl->count;i++) {
        if (dl->commands[i].type==CMD_TEXT) {
            free(dl->commands[i].text);
            free(dl->commands[i].font_family);
            dl->commands[i].text = dl->commands[i].font_family = NULL;
        }
    }
    dl->count = 0;
}
void display_list_free(__display_list__ *dl) {
    display_list_clear(dl);
    free(dl->commands);
    dl->commands=NULL; dl->capacity=0;
}
