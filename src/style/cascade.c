#define _POSIX_C_SOURCE 200809L
/**
 * src/style/cascade.c — CSS cascade and style resolution.
 */

#include "cascade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* =========================================================================
 * String helpers
 * ========================================================================= */
static char *str_trim_copy(const char *s)
{
    if (!s)
        return NULL;
    while (*s && isspace((unsigned char)*s))
        s++;
    const char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        end--;
    size_t len = (size_t)(end - s);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}
static int str_ieq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}
static int class_list_contains(const char *list, const char *name)
{
    if (!list || !name)
        return 0;
    size_t nlen = strlen(name);
    const char *p = list;
    while (*p)
    {
        while (*p && isspace((unsigned char)*p))
            p++;
        const char *s = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if ((size_t)(p - s) == nlen && strncmp(s, name, nlen) == 0)
            return 1;
    }
    return 0;
}

/* =========================================================================
 * Selector matching
 * ========================================================================= */
static int match_simple(const char *sel, const __html_node__ *node)
{
    if (!sel || !node)
        return 0;
    if (node->tag && strcmp(node->tag, "#text") == 0)
        return 0;
    char buf[256];
    strncpy(buf, sel, 255);
    buf[255] = '\0';
    char *colon = strchr(buf, ':');
    if (colon)
        *colon = '\0';
    sel = buf;
    if (strcmp(sel, "*") == 0)
        return 1;
    const char *p = sel;
    /* tag part */
    if (*p && *p != '.' && *p != '#')
    {
        const char *ts = p;
        while (*p && *p != '.' && *p != '#')
            p++;
        size_t tlen = (size_t)(p - ts);
        char tag[64] = {0};
        if (tlen < 64)
        {
            memcpy(tag, ts, tlen);
        }
        if (!str_ieq(tag, node->tag ? node->tag : ""))
            return 0;
    }
    while (*p)
    {
        if (*p == '#')
        {
            p++;
            const char *s = p;
            while (*p && *p != '.' && *p != '#')
                p++;
            char id[128] = {0};
            size_t l = (size_t)(p - s);
            if (l < 128)
                memcpy(id, s, l);
            const char *nid = htmlattr_list_get(&node->attrs, "id");
            if (!nid || strcmp(nid, id) != 0)
                return 0;
        }
        else if (*p == '.')
        {
            p++;
            const char *s = p;
            while (*p && *p != '.' && *p != '#')
                p++;
            char cls[128] = {0};
            size_t l = (size_t)(p - s);
            if (l < 128)
                memcpy(cls, s, l);
            const char *nc = htmlattr_list_get(&node->attrs, "class");
            if (!class_list_contains(nc, cls))
                return 0;
        }
        else
        {
            p++;
        }
    }
    return 1;
}

int selector_matches(const char *selector, const __html_node__ *node)
{
    if (!selector || !node)
        return 0;
    if (node->tag && strcmp(node->tag, "#text") == 0)
        return 0;
    char buf[512];
    strncpy(buf, selector, 511);
    buf[511] = '\0';
    char *parts[16];
    int np = 0;
    char *p = buf;
    while (*p && np < 16)
    {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        parts[np++] = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p)
            *p++ = '\0';
    }
    if (np == 0)
        return 0;
    if (!match_simple(parts[np - 1], node))
        return 0;
    if (np == 1)
        return 1;
    int pi = np - 2;
    const __html_node__ *anc = node->parent;
    while (anc && pi >= 0)
    {
        if (match_simple(parts[pi], anc))
            pi--;
        anc = anc->parent;
    }
    return pi < 0 ? 1 : 0;
}

int selector_specificity(const char *selector)
{
    if (!selector)
        return 0;
    const char *last = strrchr(selector, ' ');
    const char *s = last ? last + 1 : selector;
    char buf[256];
    strncpy(buf, s, 255);
    buf[255] = '\0';
    char *c = strchr(buf, ':');
    if (c)
        *c = '\0';
    int ids = 0, cls = 0, tag = 0;
    const char *p = buf;
    if (*p && *p != '.' && *p != '#' && *p != '*')
    {
        while (*p && *p != '.' && *p != '#')
            p++;
        tag++;
    }
    if (*p == '*')
        p++;
    while (*p)
    {
        if (*p == '#')
        {
            ids++;
            while (*++p && *p != '.' && *p != '#')
                ;
        }
        else if (*p == '.')
        {
            cls++;
            while (*++p && *p != '.' && *p != '#')
                ;
        }
        else
            p++;
    }
    return (ids << 16) | (cls << 8) | tag;
}

/* =========================================================================
 * Sorting
 * ========================================================================= */
typedef struct
{
    const __css_rule__ *rule;
    int specificity;
} MatchedRule;
static int cmp_matched(const void *a, const void *b)
{
    const MatchedRule *ra = (const MatchedRule *)a;
    const MatchedRule *rb = (const MatchedRule *)b;
    if (ra->specificity != rb->specificity)
        return ra->specificity - rb->specificity;
    return ra->rule->source_order - rb->rule->source_order;
}

/* =========================================================================
 * Value resolution
 * ========================================================================= */
static float parse_length(const char *v, float parent_px, float fs, float root_fs)
{
    if (!v)
        return 0.0f;
    while (*v && isspace((unsigned char)*v))
        v++;
    if (strcmp(v, "0") == 0)
        return 0.0f;
    if (strcmp(v, "auto") == 0)
        return NAN;
    char *end = NULL;
    float n = strtof(v, &end);
    if (!end || end == v)
        return 0.0f;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (strcmp(end, "px") == 0)
        return n;
    if (strcmp(end, "%") == 0)
        return isnan(parent_px) ? NAN : n * parent_px / 100.0f;
    if (strcmp(end, "em") == 0)
        return n * fs;
    if (strcmp(end, "rem") == 0)
        return n * root_fs;
    if (*end == '\0')
        return n;
    return 0.0f;
}
static void parse_sh4(const char *v, float out[4], float pw, float fs, float rfs)
{
    if (!v)
        return;
    char buf[256];
    strncpy(buf, v, 255);
    buf[255] = '\0';
    char *pp[4] = {NULL, NULL, NULL, NULL};
    int np = 0;
    char *p = buf;
    while (*p && np < 4)
    {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        pp[np++] = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p)
            *p++ = '\0';
    }
    if (!np)
        return;
    float v0 = parse_length(pp[0], pw, fs, rfs);
    if (np == 1)
    {
        out[0] = out[1] = out[2] = out[3] = v0;
    }
    else if (np == 2)
    {
        float v1 = parse_length(pp[1], pw, fs, rfs);
        out[0] = out[2] = v0;
        out[1] = out[3] = v1;
    }
    else if (np == 3)
    {
        float v1 = parse_length(pp[1], pw, fs, rfs), v2 = parse_length(pp[2], pw, fs, rfs);
        out[0] = v0;
        out[1] = out[3] = v1;
        out[2] = v2;
    }
    else
    {
        out[0] = v0;
        out[1] = parse_length(pp[1], pw, fs, rfs);
        out[2] = parse_length(pp[2], pw, fs, rfs);
        out[3] = parse_length(pp[3], pw, fs, rfs);
    }
}

static void apply_decl(__computed_style__ *s, const char *prop, const char *val,
                       float pw, float ph, float pfs, float rfs)
{
    if (!prop || !val)
        return;
    float fs = s->font_size > 0 ? s->font_size : pfs;
#define L(v) parse_length((v), pw, fs, rfs)
#define LH(v) parse_length((v), ph, fs, rfs)
    if (!strcmp(prop, "width"))
    {
        s->width = L(val);
        return;
    }
    if (!strcmp(prop, "height"))
    {
        s->height = LH(val);
        return;
    }
    if (!strcmp(prop, "min-width"))
    {
        s->min_width = L(val);
        return;
    }
    if (!strcmp(prop, "min-height"))
    {
        s->min_height = LH(val);
        return;
    }
    if (!strcmp(prop, "max-width"))
    {
        s->max_width = L(val);
        return;
    }
    if (!strcmp(prop, "max-height"))
    {
        s->max_height = LH(val);
        return;
    }
    if (!strcmp(prop, "margin"))
    {
        parse_sh4(val, s->margin, pw, fs, rfs);
        return;
    }
    if (!strcmp(prop, "margin-top"))
    {
        s->margin[0] = L(val);
        return;
    }
    if (!strcmp(prop, "margin-right"))
    {
        s->margin[1] = L(val);
        return;
    }
    if (!strcmp(prop, "margin-bottom"))
    {
        s->margin[2] = L(val);
        return;
    }
    if (!strcmp(prop, "margin-left"))
    {
        s->margin[3] = L(val);
        return;
    }
    if (!strcmp(prop, "padding"))
    {
        parse_sh4(val, s->padding, pw, fs, rfs);
        return;
    }
    if (!strcmp(prop, "padding-top"))
    {
        s->padding[0] = L(val);
        return;
    }
    if (!strcmp(prop, "padding-right"))
    {
        s->padding[1] = L(val);
        return;
    }
    if (!strcmp(prop, "padding-bottom"))
    {
        s->padding[2] = L(val);
        return;
    }
    if (!strcmp(prop, "padding-left"))
    {
        s->padding[3] = L(val);
        return;
    }
    if (!strcmp(prop, "border-width"))
    {
        parse_sh4(val, s->border_width, pw, fs, rfs);
        return;
    }
    if (!strcmp(prop, "border-top-width"))
    {
        s->border_width[0] = L(val);
        return;
    }
    if (!strcmp(prop, "border-right-width"))
    {
        s->border_width[1] = L(val);
        return;
    }
    if (!strcmp(prop, "border-bottom-width"))
    {
        s->border_width[2] = L(val);
        return;
    }
    if (!strcmp(prop, "border-left-width"))
    {
        s->border_width[3] = L(val);
        return;
    }
    if (!strcmp(prop, "border-radius"))
    {
        float r = L(val);
        s->border_radius[0] = s->border_radius[1] = s->border_radius[2] = s->border_radius[3] = r;
        return;
    }
    if (!strcmp(prop, "background-color") || !strcmp(prop, "background"))
    {
        s->background_color = color_parse(val);
        return;
    }
    if (!strcmp(prop, "color"))
    {
        s->color = color_parse(val);
        return;
    }
    /* border: <width> <style> <color>
     * We parse width and color; 'style' (solid/dashed/…) is accepted but
     * ignored at this layer — border-style is not yet rendered. */
    if (!strcmp(prop, "border"))
    {
        /* Tokenise the value — up to 3 space-separated tokens */
        char tmp[256];
        strncpy(tmp, val, 255);
        tmp[255] = '\0';
        char *sp = NULL;
        char *tok = strtok_r(tmp, " ", &sp);
        while (tok)
        {
            /* If it starts with a digit or '.', treat it as a width */
            if (isdigit((unsigned char)tok[0]) || tok[0] == '.')
            {
                float w = parse_length(tok, pw, fs, rfs);
                s->border_width[0] = s->border_width[1] =
                    s->border_width[2] = s->border_width[3] = w;
            }
            /* If it starts with '#' or 'r'(gb) or a known name, treat as color */
            else if (tok[0] == '#' || strncmp(tok, "rgb", 3) == 0 ||
                     strncmp(tok, "rgba", 4) == 0 ||
                     (!strcmp(tok, "transparent") || !strcmp(tok, "black") ||
                      !strcmp(tok, "white") || !strcmp(tok, "red") ||
                      !strcmp(tok, "green") || !strcmp(tok, "blue") ||
                      !strcmp(tok, "gray") || !strcmp(tok, "grey") ||
                      !strcmp(tok, "yellow") || !strcmp(tok, "orange") ||
                      !strcmp(tok, "purple") || !strcmp(tok, "pink") ||
                      !strcmp(tok, "cyan") || !strcmp(tok, "magenta")))
            {
                Color c = color_parse(tok);
                s->border_color[0] = s->border_color[1] =
                    s->border_color[2] = s->border_color[3] = c;
            }
            /* Otherwise it's the style keyword (solid, dashed, …) — skip */
            tok = strtok_r(NULL, " ", &sp);
        }
        return;
    }
    if (!strcmp(prop, "border-color"))
    {
        Color c = color_parse(val);
        s->border_color[0] = s->border_color[1] = s->border_color[2] = s->border_color[3] = c;
        return;
    }
    if (!strcmp(prop, "font-size"))
    {
        if (!strcmp(val, "small"))
        {
            s->font_size = 13.0f;
            return;
        }
        if (!strcmp(val, "medium"))
        {
            s->font_size = 16.0f;
            return;
        }
        if (!strcmp(val, "large"))
        {
            s->font_size = 18.0f;
            return;
        }
        if (!strcmp(val, "x-large"))
        {
            s->font_size = 24.0f;
            return;
        }
        if (!strcmp(val, "xx-large"))
        {
            s->font_size = 32.0f;
            return;
        }
        s->font_size = parse_length(val, pfs, pfs, rfs);
        return;
    }
    if (!strcmp(prop, "font-family"))
    {
        free(s->font_family);
        s->font_family = strdup(val);
        return;
    }
    if (!strcmp(prop, "font-weight"))
    {
        if (!strcmp(val, "bold"))
        {
            s->font_weight = 700;
            return;
        }
        if (!strcmp(val, "normal"))
        {
            s->font_weight = 400;
            return;
        }
        s->font_weight = (int)strtol(val, NULL, 10);
        return;
    }
    if (!strcmp(prop, "font-style"))
    {
        s->font_italic = (!strcmp(val, "italic") || !strcmp(val, "oblique"));
        return;
    }
    if (!strcmp(prop, "line-height"))
    {
        char *e = NULL;
        float n = strtof(val, &e);
        s->line_height = (e && !strcmp(e, "px")) ? n / fs : n;
        return;
    }
    if (!strcmp(prop, "text-align"))
    {
        s->text_align = (!strcmp(val, "center") ? 1 : !strcmp(val, "right") ? 2
                                                                            : 0);
        return;
    }
    if (!strcmp(prop, "display"))
    {
        if (!strcmp(val, "flex"))
        {
            s->display = DISPLAY_FLEX;
            return;
        }
        if (!strcmp(val, "block"))
        {
            s->display = DISPLAY_BLOCK;
            return;
        }
        if (!strcmp(val, "inline"))
        {
            s->display = DISPLAY_INLINE;
            return;
        }
        if (!strcmp(val, "inline-block"))
        {
            s->display = DISPLAY_INLINE_BLOCK;
            return;
        }
        if (!strcmp(val, "none"))
        {
            s->display = DISPLAY_NONE;
            return;
        }
        return;
    }
    if (!strcmp(prop, "position"))
    {
        if (!strcmp(val, "static"))
        {
            s->position = POSITION_STATIC;
            return;
        }
        if (!strcmp(val, "relative"))
        {
            s->position = POSITION_RELATIVE;
            return;
        }
        if (!strcmp(val, "absolute"))
        {
            s->position = POSITION_ABSOLUTE;
            return;
        }
        return;
    }
    if (!strcmp(prop, "top"))
    {
        s->top = L(val);
        return;
    }
    if (!strcmp(prop, "right"))
    {
        s->right = L(val);
        return;
    }
    if (!strcmp(prop, "bottom"))
    {
        s->bottom = L(val);
        return;
    }
    if (!strcmp(prop, "left"))
    {
        s->left = L(val);
        return;
    }
    if (!strcmp(prop, "z-index"))
    {
        s->z_index = strtof(val, NULL);
        return;
    }
    if (!strcmp(prop, "flex-direction"))
    {
        if (!strcmp(val, "row"))
            s->flex_direction = FLEX_DIRECTION_ROW;
        else if (!strcmp(val, "row-reverse"))
            s->flex_direction = FLEX_DIRECTION_ROW_REVERSE;
        else if (!strcmp(val, "column"))
            s->flex_direction = FLEX_DIRECTION_COLUMN;
        else if (!strcmp(val, "column-reverse"))
            s->flex_direction = FLEX_DIRECTION_COLUMN_REVERSE;
        return;
    }
    if (!strcmp(prop, "justify-content"))
    {
        if (!strcmp(val, "flex-start"))
            s->justify_content = JUSTIFY_FLEX_START;
        else if (!strcmp(val, "flex-end"))
            s->justify_content = JUSTIFY_FLEX_END;
        else if (!strcmp(val, "center"))
            s->justify_content = JUSTIFY_CENTER;
        else if (!strcmp(val, "space-between"))
            s->justify_content = JUSTIFY_SPACE_BETWEEN;
        else if (!strcmp(val, "space-around"))
            s->justify_content = JUSTIFY_SPACE_AROUND;
        else if (!strcmp(val, "space-evenly"))
            s->justify_content = JUSTIFY_SPACE_EVENLY;
        return;
    }
    if (!strcmp(prop, "align-items"))
    {
        if (!strcmp(val, "flex-start"))
            s->align_items = ALIGN_FLEX_START;
        else if (!strcmp(val, "flex-end"))
            s->align_items = ALIGN_FLEX_END;
        else if (!strcmp(val, "center"))
            s->align_items = ALIGN_CENTER;
        else if (!strcmp(val, "stretch"))
            s->align_items = ALIGN_STRETCH;
        else if (!strcmp(val, "baseline"))
            s->align_items = ALIGN_BASELINE;
        return;
    }
    if (!strcmp(prop, "align-self"))
    {
        if (!strcmp(val, "flex-start"))
            s->align_self = ALIGN_FLEX_START;
        else if (!strcmp(val, "flex-end"))
            s->align_self = ALIGN_FLEX_END;
        else if (!strcmp(val, "center"))
            s->align_self = ALIGN_CENTER;
        else if (!strcmp(val, "stretch"))
            s->align_self = ALIGN_STRETCH;
        return;
    }
    if (!strcmp(prop, "flex-grow"))
    {
        s->flex_grow = strtof(val, NULL);
        return;
    }
    if (!strcmp(prop, "flex-shrink"))
    {
        s->flex_shrink = strtof(val, NULL);
        return;
    }
    if (!strcmp(prop, "flex-basis"))
    {
        s->flex_basis = L(val);
        return;
    }
    if (!strcmp(prop, "flex-wrap"))
    {
        s->flex_wrap = (!strcmp(val, "wrap") || !strcmp(val, "wrap-reverse"));
        return;
    }
    if (!strcmp(prop, "flex"))
    {
        char fb[64];
        strncpy(fb, val, 63);
        fb[63] = '\0';
        char *sp = NULL;
        char *t = strtok_r(fb, " ", &sp);
        if (t)
        {
            s->flex_grow = strtof(t, NULL);
            t = strtok_r(NULL, " ", &sp);
        }
        if (t)
        {
            s->flex_shrink = strtof(t, NULL);
            t = strtok_r(NULL, " ", &sp);
        }
        if (t)
        {
            s->flex_basis = L(t);
        }
        return;
    }
    if (!strcmp(prop, "gap") || !strcmp(prop, "row-gap") || !strcmp(prop, "column-gap"))
    {
        s->gap = L(val);
        return;
    }
    if (!strcmp(prop, "opacity"))
    {
        s->opacity = strtof(val, NULL);
        return;
    }
    if (!strcmp(prop, "overflow"))
    {
        s->overflow_hidden = (!strcmp(val, "hidden"));
        return;
    }
    if (!strcmp(prop, "pointer-events"))
    {
        s->pointer_events = (!strcmp(val, "none") ? 0 : 1);
        return;
    }
#undef L
#undef LH
}

static void inherit_from_parent(__computed_style__ *s, const __computed_style__ *p)
{
    if (!p)
        return;
    /* color */
    static const Color INIT = {0, 0, 0, 0};
    if (s->color.r == INIT.r && s->color.g == INIT.g && s->color.b == INIT.b && s->color.a == INIT.a)
        s->color = p->color;
    if (s->font_size == 16.0f)
        s->font_size = p->font_size;
    if (!s->font_family && p->font_family)
        s->font_family = strdup(p->font_family);
    if (s->font_weight == 400)
        s->font_weight = p->font_weight;
    if (!s->font_italic)
        s->font_italic = p->font_italic;
    if (s->line_height == 1.2f)
        s->line_height = p->line_height;
    if (s->text_align == 0)
        s->text_align = p->text_align;
}

static void cascade_node(__html_node__ *node, const __css_rule_list__ *rules,
                         const __computed_style__ *ps,
                         float vpw, float vph, float rfs)
{
    if (!node || strcmp(node->tag, "#text") == 0)
        return;
    float pw = (ps && !isnan(ps->width)) ? ps->width : vpw;
    float ph = (ps && !isnan(ps->height)) ? ps->height : vph;
    float pfs = ps ? ps->font_size : rfs;

    MatchedRule matched[256];
    int n = 0;
    for (int i = 0; i < rules->count && n < 256; i++)
    {
        if (selector_matches(rules->rules[i].selector, node))
        {
            matched[n].rule = &rules->rules[i];
            matched[n].specificity = selector_specificity(rules->rules[i].selector);
            n++;
        }
    }
    qsort(matched, (size_t)n, sizeof(MatchedRule), cmp_matched);

    __computed_style__ *s = malloc(sizeof(__computed_style__));
    if (!s)
        return;
    *s = computed_style_defaults();

    for (int i = 0; i < n; i++)
    {
        const __css_rule__ *r = matched[i].rule;
        for (int j = 0; j < r->decl_count; j++)
            apply_decl(s, r->declarations[j].property, r->declarations[j].value, pw, ph, pfs, rfs);
    }

    /* inline style overrides */
    const char *inl = htmlattr_list_get(&node->attrs, "style");
    if (inl)
    {
        char buf[1024];
        strncpy(buf, inl, 1023);
        buf[1023] = '\0';
        char *sp = NULL;
        char *d = strtok_r(buf, ";", &sp);
        while (d)
        {
            char *colon = strchr(d, ':');
            if (colon)
            {
                *colon = '\0';
                char *prop = str_trim_copy(d);
                char *val = str_trim_copy(colon + 1);
                if (prop && val)
                    apply_decl(s, prop, val, pw, ph, pfs, rfs);
                free(prop);
                free(val);
            }
            d = strtok_r(NULL, ";", &sp);
        }
    }

    inherit_from_parent(s, ps);
    if (node->force_hidden)
        s->display = DISPLAY_NONE;

    free(node->style);
    node->style = s;

    for (int i = 0; i < node->child_count; i++)
        cascade_node(node->children[i], rules, s, vpw, vph, rfs);
}

void cascade_apply(__html_node__ *root, const __css_rule_list__ *rules, float vpw, float vph)
{
    if (!root || !rules)
        return;
    cascade_node(root, rules, NULL, vpw, vph, 16.0f);
}

void computed_style_dump(const __computed_style__ *s, const char *label)
{
    if (!s)
        return;
    printf("__computed_style__ [%s]\n", label ? label : "?");
    printf("  display:   %s\n", s->display == DISPLAY_FLEX ? "flex" : s->display == DISPLAY_NONE ? "none"
                                                                                                 : "block");
    printf("  size:      w=%.1f h=%.1f\n", s->width, s->height);
    printf("  padding:   %.1f %.1f %.1f %.1f\n", s->padding[0], s->padding[1], s->padding[2], s->padding[3]);
    printf("  margin:    %.1f %.1f %.1f %.1f\n", s->margin[0], s->margin[1], s->margin[2], s->margin[3]);
    printf("  bg-color:  rgba(%d,%d,%d,%d)\n", s->background_color.r, s->background_color.g, s->background_color.b, s->background_color.a);
    printf("  color:     rgba(%d,%d,%d,%d)\n", s->color.r, s->color.g, s->color.b, s->color.a);
    printf("  font:      %.1fpx w=%d italic=%s fam=%s\n", s->font_size, s->font_weight, s->font_italic ? "yes" : "no", s->font_family ? s->font_family : "(inherit)");
    if (s->display == DISPLAY_FLEX)
        printf("  flex-dir:  %s  gap=%.1f\n", s->flex_direction == FLEX_DIRECTION_COLUMN ? "column" : "row", s->gap);
    printf("  b-radius:  %.1f %.1f %.1f %.1f\n", s->border_radius[0], s->border_radius[1], s->border_radius[2], s->border_radius[3]);
    printf("  opacity:   %.2f\n", s->opacity);
}
