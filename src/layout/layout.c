#define _POSIX_C_SOURCE 200809L
/**
 * src/layout/layout.c — Block and Flexbox layout engine.
 */

#include "layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define MIN2(a, b) ((a) < (b) ? (a) : (b))

static float h_inset(const __computed_style__ *s) {
  return s->border_width[SIDE_LEFT] + s->padding[SIDE_LEFT] +
         s->border_width[SIDE_RIGHT] + s->padding[SIDE_RIGHT];
}
static float v_inset(const __computed_style__ *s) {
  return s->border_width[SIDE_TOP] + s->padding[SIDE_TOP] +
         s->border_width[SIDE_BOTTOM] + s->padding[SIDE_BOTTOM];
}
static float cx_off(const __computed_style__ *s) {
  return s->border_width[SIDE_LEFT] + s->padding[SIDE_LEFT];
}
static float cy_off(const __computed_style__ *s) {
  return s->border_width[SIDE_TOP] + s->padding[SIDE_TOP];
}

static LayoutBox *get_box(__html_node__ *n) {
  if (!n->layout)
    n->layout = calloc(1, sizeof(LayoutBox));
  return n->layout;
}
static int is_lnode(const __html_node__ *n) {
  if (!n || !n->style)
    return 0;
  if (n->style->display == DISPLAY_NONE)
    return 0;
  if (strcmp(n->tag, "#text") == 0)
    return 0;
  return 1;
}

/* Forward */
static void layout_node(__html_node__ *n, float cw, float ch, float ox,
                        float oy);

/* Intrinsic size (dry-run) */
static float intrinsic_main(__html_node__ *n, float aw, float ah, int is_row) {
  layout_node(n, aw, ah, 0, 0);
  LayoutBox *b = n->layout;
  return b ? (is_row ? b->width : b->height) : 0.0f;
}

/* Block layout */
static void layout_block(__html_node__ *n, float aw, float ah, float ox,
                         float oy) {
  __computed_style__ *s = n->style;
  LayoutBox *b = get_box(n);

  float ow = isnan(s->width) ? aw : s->width;
  if (!isnan(s->max_width))
    ow = MIN2(ow, s->max_width);
  ow = MAX2(ow, s->min_width);
  ow = MAX2(ow, h_inset(s));
  float cw = MAX2(0.0f, ow - h_inset(s));

  float cursor = 0.0f;
  int has_elem_children = 0;

  for (int i = 0; i < n->child_count; i++) {
    __html_node__ *ch = n->children[i];
    if (!is_lnode(ch))
      continue;
    has_elem_children = 1;
    __computed_style__ *cs = ch->style;
    float mt = cs->margin[SIDE_TOP], mb = cs->margin[SIDE_BOTTOM];
    float ml = cs->margin[SIDE_LEFT];
    cursor += mt;
    layout_node(ch, cw, ah, ox + cx_off(s) + ml, oy + cy_off(s) + cursor);
    LayoutBox *cb = ch->layout;
    cursor += (cb ? cb->height : 0.0f) + mb;
  }

  /* If this node has no layout children but has text content, estimate
   * its height from font metrics. This is the fallback for leaf nodes
   * like <h1>, <p>, <button> that contain only #text children.        */
  if (!has_elem_children && cursor == 0.0f) {
    int has_text = 0;
    for (int i = 0; i < n->child_count; i++) {
      __html_node__ *ch = n->children[i];
      if (strcmp(ch->tag, "#text") == 0 && ch->text_content &&
          strlen(ch->text_content) > 0) {
        has_text = 1;
        break;
      }
    }
    /* Also check text_content directly on the node itself */
    if (!has_text && n->text_content && strlen(n->text_content) > 0)
      has_text = 1;

    if (has_text) {
      float fs = (s->font_size > 0) ? s->font_size : 16.0f;
      cursor = fs * s->line_height; /* e.g. 36px * 1.2 = 43.2px */
    }
  }

  float oh = isnan(s->height) ? cursor + v_inset(s) : s->height;
  if (!isnan(s->max_height))
    oh = MIN2(oh, s->max_height);
  oh = MAX2(oh, s->min_height);

  b->x = ox;
  b->y = oy;
  b->width = ow;
  b->height = oh;
  b->content_x = ox + cx_off(s);
  b->content_y = oy + cy_off(s);
  b->content_width = cw;
  b->content_height = MAX2(0.0f, oh - v_inset(s));
}

/* Flex layout */
static void layout_flex(__html_node__ *n, float aw, float ah, float ox,
                        float oy) {
  __computed_style__ *s = n->style;
  LayoutBox *b = get_box(n);

  int is_row = (s->flex_direction == FLEX_DIRECTION_ROW ||
                s->flex_direction == FLEX_DIRECTION_ROW_REVERSE);
  int is_rev = (s->flex_direction == FLEX_DIRECTION_ROW_REVERSE ||
                s->flex_direction == FLEX_DIRECTION_COLUMN_REVERSE);

  /* Outer size on each axis
   * Width:  explicit > max-width clamp > available width (always known).
   * Height: explicit > max-height clamp > available height IF given,
   *         otherwise we must shrink-wrap (computed in step 6).
   * We keep a separate flag so children know whether cross-size is real. */
  float ow = isnan(s->width) ? aw : s->width;
  if (!isnan(s->max_width))
    ow = MIN2(ow, s->max_width);
  ow = MAX2(ow, s->min_width);

  /* Definite height: explicit value or explicitly passed-in ah.
   * We treat ah==0 as "unknown" to avoid the stretch-to-zero problem. */
  int has_definite_height = !isnan(s->height);
  float oh = has_definite_height ? s->height : ah;
  if (!isnan(s->max_height))
    oh = MIN2(oh, s->max_height);
  oh = MAX2(oh, s->min_height);

  float cw = MAX2(0.0f, ow - h_inset(s));
  /* For a column container with no definite height, ch is unknown at
   * this point — we pass 0 to children so they don't stretch on the
   * main axis. Cross-axis (width) is always cw and is always valid. */
  float ch = has_definite_height ? MAX2(0.0f, oh - v_inset(s)) : 0.0f;

  /* Count layout children */
  int nc = 0;
  for (int i = 0; i < n->child_count; i++)
    if (is_lnode(n->children[i]))
      nc++;

  if (nc == 0) {
    /* No layout children — check for text content and size accordingly */
    if (!has_definite_height) {
      int has_text = 0;
      for (int i = 0; i < n->child_count; i++) {
        __html_node__ *ch = n->children[i];
        if (strcmp(ch->tag, "#text") == 0 && ch->text_content &&
            strlen(ch->text_content) > 0) {
          has_text = 1;
          break;
        }
      }
      if (!has_text && n->text_content && strlen(n->text_content) > 0)
        has_text = 1;
      if (has_text) {
        float fs = (s->font_size > 0) ? s->font_size : 16.0f;
        oh = fs * s->line_height + v_inset(s);
        oh = MAX2(oh, s->min_height);
        ch = MAX2(0.0f, oh - v_inset(s));
      }
    }
    b->x = ox;
    b->y = oy;
    b->width = ow;
    b->height = oh;
    b->content_x = ox + cx_off(s);
    b->content_y = oy + cy_off(s);
    b->content_width = cw;
    b->content_height = ch;
    return;
  }

  float *bm = calloc(nc, sizeof(float));
  float *fm = calloc(nc, sizeof(float));
  float *cs_ = calloc(nc, sizeof(float));
  float *gf = calloc(nc, sizeof(float));
  __html_node__ **kids = calloc(nc, sizeof(__html_node__ *));
  if (!bm || !fm || !cs_ || !gf || !kids) {
    free(bm);
    free(fm);
    free(cs_);
    free(gf);
    free(kids);
    return;
  }

  int idx = 0;
  for (int i = 0; i < n->child_count; i++)
    if (is_lnode(n->children[i]))
      kids[idx++] = n->children[i];

  float gap = s->gap;

  /* base main-axis sizes
   * For column containers the main axis is height.
   * We call intrinsic_main with the known cross size (cw) so children
   * can measure themselves against the correct available width.         */
  float total_grow = 0, total_fixed = 0;
  float avail_main = is_row ? cw : ch;

  for (int i = 0; i < nc; i++) {
    __computed_style__ *kcs = kids[i]->style;
    float mg_m = is_row ? (kcs->margin[SIDE_LEFT] + kcs->margin[SIDE_RIGHT])
                        : (kcs->margin[SIDE_TOP] + kcs->margin[SIDE_BOTTOM]);
    gf[i] = kcs->flex_grow;
    total_grow += gf[i];

    if (!isnan(kcs->flex_basis) && kcs->flex_basis >= 0) {
      bm[i] = kcs->flex_basis;
    } else if (is_row && !isnan(kcs->width)) {
      bm[i] = kcs->width;
    } else if (!is_row && !isnan(kcs->height)) {
      bm[i] = kcs->height;
    } else {
      /* Dry-run: pass the known cross size so the child can measure
       * itself correctly on its own cross axis.                     */
      if (is_row) {
        bm[i] = intrinsic_main(kids[i], cw, ch, /*is_row=*/1);
      } else {
        /* Column: child cross = cw (known), main = unknown → 0   */
        bm[i] = intrinsic_main(kids[i], cw, 0.0f, /*is_row=*/0);
      }
    }
    if (gf[i] == 0)
      total_fixed += bm[i] + mg_m;
  }
  total_fixed += gap * (float)(nc - 1);
  float free_space = MAX2(0.0f, avail_main - total_fixed);

  /* flex-grow distribution */
  for (int i = 0; i < nc; i++) {
    __computed_style__ *kcs = kids[i]->style;
    fm[i] = (gf[i] > 0 && total_grow > 0) ? (free_space * gf[i] / total_grow)
                                          : bm[i];
    if (is_row) {
      if (!isnan(kcs->max_width))
        fm[i] = MIN2(fm[i], kcs->max_width);
      fm[i] = MAX2(fm[i], kcs->min_width);
    } else {
      if (!isnan(kcs->max_height))
        fm[i] = MIN2(fm[i], kcs->max_height);
      fm[i] = MAX2(fm[i], kcs->min_height);
    }
  }

  /* cross-axis sizes
   * For row containers: cross = height. Pass fm[i] as child width so
   *   the child can wrap text / measure height properly.
   * For column containers: cross = width. Use cw (always known).       */
  float max_cross = 0;
  for (int i = 0; i < nc; i++) {
    __computed_style__ *kcs = kids[i]->style;
    float kw, kh;
    if (is_row) {
      kw = fm[i];
      /* Cross (height): use explicit value, or stretch to ch if
       * ch is known and align-items is stretch, else size-to-content. */
      if (!isnan(kcs->height)) {
        kh = kcs->height;
      } else if (s->align_items == ALIGN_STRETCH && ch > 0) {
        kh = MAX2(0, ch - kcs->margin[SIDE_TOP] - kcs->margin[SIDE_BOTTOM]);
      } else {
        kh = 0.0f; /* size to content */
      }
    } else {
      kh = fm[i];
      /* Cross (width): stretch to cw (always valid for column).     */
      if (!isnan(kcs->width)) {
        kw = kcs->width;
      } else if (s->align_items == ALIGN_STRETCH) {
        kw = MAX2(0, cw - kcs->margin[SIDE_LEFT] - kcs->margin[SIDE_RIGHT]);
      } else {
        kw = cw; /* default: fill available width */
      }
    }
    layout_node(kids[i], kw, kh, 0, 0);
    LayoutBox *kb = kids[i]->layout;
    cs_[i] = is_row ? (kb ? kb->height : kh) : (kb ? kb->width : kw);
    float mg_c = is_row ? (kcs->margin[SIDE_TOP] + kcs->margin[SIDE_BOTTOM])
                        : (kcs->margin[SIDE_LEFT] + kcs->margin[SIDE_RIGHT]);
    max_cross = MAX2(max_cross, cs_[i] + mg_c);
  }

  /* justify-content spacing */
  float total_occ = gap * (float)(nc - 1);
  for (int i = 0; i < nc; i++) {
    __computed_style__ *kcs = kids[i]->style;
    float mg = is_row ? (kcs->margin[SIDE_LEFT] + kcs->margin[SIDE_RIGHT])
                      : (kcs->margin[SIDE_TOP] + kcs->margin[SIDE_BOTTOM]);
    total_occ += fm[i] + mg;
  }
  float rem = MAX2(0, avail_main - total_occ);
  float start = 0, between = gap;
  switch (s->justify_content) {
  case JUSTIFY_FLEX_START:
    start = 0;
    between = gap;
    break;
  case JUSTIFY_FLEX_END:
    start = rem;
    between = gap;
    break;
  case JUSTIFY_CENTER:
    start = rem / 2.0f;
    between = gap;
    break;
  case JUSTIFY_SPACE_BETWEEN:
    start = 0;
    between = (nc > 1 ? rem / (nc - 1) : 0) + gap;
    break;
  case JUSTIFY_SPACE_AROUND: {
    float p = rem / nc;
    start = p / 2.0f;
    between = p + gap;
  } break;
  case JUSTIFY_SPACE_EVENLY: {
    float p = rem / (nc + 1);
    start = p;
    between = p + gap;
  } break;
  }

  /* final placement */
  float px = ox + cx_off(s);
  float py = oy + cy_off(s);
  float cursor = start;
  if (is_rev)
    cursor = (is_row ? cw : ch) - start;

  for (int ii = 0; ii < nc; ii++) {
    int i = is_rev ? (nc - 1 - ii) : ii;
    __html_node__ *kid = kids[i];
    __computed_style__ *kcs = kid->style;
    float mms = is_row ? kcs->margin[SIDE_LEFT] : kcs->margin[SIDE_TOP];
    float mme = is_row ? kcs->margin[SIDE_RIGHT] : kcs->margin[SIDE_BOTTOM];
    float mcs = is_row ? kcs->margin[SIDE_TOP] : kcs->margin[SIDE_LEFT];
    float mce = is_row ? kcs->margin[SIDE_BOTTOM] : kcs->margin[SIDE_RIGHT];
    (void)mce;

    float cross_avail = is_row ? ch : cw;
    float coff = 0;
    /* FIX: align_self default is ALIGN_SELF_AUTO (-1), not ALIGN_FLEX_START (0).
     * Using ALIGN_FLEX_START as the sentinel made it impossible to explicitly
     * override a parent's align-items:center back to flex-start on a child. */
    __align_items__ al = (kcs->align_self != ALIGN_SELF_AUTO)
                         ? (__align_items__)kcs->align_self
                         : s->align_items;
    switch (al) {
    case ALIGN_FLEX_END:
      coff = cross_avail - cs_[i] - mcs;
      break;
    case ALIGN_CENTER:
      coff = (cross_avail - cs_[i]) / 2.0f;
      break;
    default:
      coff = mcs;
      break;
    }

    float kox, koy, kw, kh;
    if (is_row) {
      float pos = is_rev ? (cursor - fm[i] - mms) : (cursor + mms);
      kox = px + pos;
      koy = py + coff;
      kw = fm[i];
      kh = cs_[i];
    } else {
      float pos = is_rev ? (cursor - fm[i] - mms) : (cursor + mms);
      kox = px + coff;
      koy = py + pos;
      kw = cs_[i];
      kh = fm[i];
    }
    layout_node(kid, kw, kh, kox, koy);
    if (is_rev)
      cursor -= fm[i] + mms + mme + between;
    else
      cursor += fm[i] + mms + mme + between;
  }

  /* finalize container size
   * If height was not definite, shrink-wrap to actual content.          */
  float fow = ow;
  float foh = oh;
  if (is_row && !has_definite_height) {
    foh = max_cross + v_inset(s);
    if (!isnan(s->max_height))
      foh = MIN2(foh, s->max_height);
    foh = MAX2(foh, s->min_height);
  }
  if (!is_row && !has_definite_height) {
    foh = total_occ + v_inset(s);
    if (!isnan(s->max_height))
      foh = MIN2(foh, s->max_height);
    foh = MAX2(foh, s->min_height);
  }
  if (is_row && isnan(s->width)) {
    fow = aw;
  }

  b->x = ox;
  b->y = oy;
  b->width = fow;
  b->height = foh;
  b->content_x = ox + cx_off(s);
  b->content_y = oy + cy_off(s);
  b->content_width = MAX2(0, fow - h_inset(s));
  b->content_height = MAX2(0, foh - v_inset(s));

  free(bm);
  free(fm);
  free(cs_);
  free(gf);
  free(kids);
}

/* Dispatcher */
static void layout_node(__html_node__ *n, float cw, float ch, float ox,
                        float oy) {
  if (!n || !n->style)
    return;
  if (strcmp(n->tag, "#text") == 0)
    return;
  __computed_style__ *s = n->style;
  if (s->display == DISPLAY_NONE) {
    LayoutBox *b = get_box(n);
    if (b) {
      b->x = ox;
      b->y = oy;
      b->width = 0;
      b->height = 0;
      b->content_x = ox;
      b->content_y = oy;
      b->content_width = 0;
      b->content_height = 0;
    }
    return;
  }
  if (s->display == DISPLAY_FLEX)
    layout_flex(n, cw, ch, ox, oy);
  else
    layout_block(n, cw, ch, ox, oy);
}

/* Public API */
void layout_run(__html_node__ *root, float vw, float vh) {
  if (!root)
    return;
  layout_node(root, vw, vh, 0, 0);
}

void layout_dump(const __html_node__ *n, int depth) {
  if (!n || strcmp(n->tag, "#text") == 0)
    return;
  if (!n->style || n->style->display == DISPLAY_NONE)
    return;
  for (int i = 0; i < depth; i++)
    printf("  ");
  const LayoutBox *b = n->layout;
  const char *id = htmlattr_list_get(&n->attrs, "id");
  const char *cls = htmlattr_list_get(&n->attrs, "class");
  printf("<%s%s%s>", n->tag, id ? " #" : "", id ? id : "");
  if (cls)
    printf(".%s", cls);
  if (b)
    printf("  [%.0f,%.0f  %.0fx%.0f]  content(%.0f,%.0f %.0fx%.0f)", b->x, b->y,
           b->width, b->height, b->content_x, b->content_y, b->content_width,
           b->content_height);
  const char *d = n->style->display == DISPLAY_FLEX   ? "[flex]"
                  : n->style->display == DISPLAY_NONE ? "[none]"
                                                      : "[block]";
  printf(" %s\n", d);
  for (int i = 0; i < n->child_count; i++)
    layout_dump(n->children[i], depth + 1);
}
