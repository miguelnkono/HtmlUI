#define _POSIX_C_SOURCE 200809L
/**
 * tests/test_render.c
 *
 * Milestone 4 test harness.
 *
 * Tests:
 *  1. Display list builder — verifies correct command types and values
 *  2. Software renderer    — renders to pixel buffer, saves as PPM
 *
 * Usage:
 *   ./test_render                          # unit tests only
 *   ./test_render ../examples/hello/views/main.html \
 *                  ../examples/hello/styles/app.css  \
 *                  output.ppm              # full pipeline → image
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/internal/types.h"
#include "../src/layout/layout.h"
#include "../src/paint/paint.h"
#include "../src/parser/css.h"
#include "../src/parser/html.h"
#include "../src/render/soft.h"
#include "../src/style/cascade.h"

/* ── test framework ───────────────────────────────────────────────────── */
static int passed = 0, failed = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (cond) {                                                                \
      printf("  [PASS] %s\n", msg);                                            \
      passed++;                                                                \
    } else {                                                                   \
      printf("  [FAIL] %s\n", msg);                                            \
      failed++;                                                                \
    }                                                                          \
  } while (0)
#define CHECK_F(a, b, msg)                                                     \
  do {                                                                         \
    float _a = (float)(a), _b = (float)(b);                                    \
    if (fabsf(_a - _b) < 2.0f) {                                               \
      printf("  [PASS] %s (%.0f)\n", msg, _a);                                 \
      passed++;                                                                \
    } else {                                                                   \
      printf("  [FAIL] %s  got=%.1f expected=%.1f\n", msg, _a, _b);            \
      failed++;                                                                \
    }                                                                          \
  } while (0)

/* Full pipeline: parse → cascade → layout → paint → return __display_list__ */
static __html_node__ *run_pipeline(const char *html, const char *css, float vw,
                                   float vh, __display_list__ *dl) {
  char err[256];
  __html_node__ *root = html_parse_string(html, err, sizeof(err));
  if (!root) {
    printf("  HTML err: %s\n", err);
    return NULL;
  }
  __css_rule_list__ rules;
  css_rule_list_init(&rules);
  css_parse_string(css, &rules, err, sizeof(err));
  cascade_apply(root, &rules, vw, vh);
  css_rule_list_free(&rules);
  layout_run(root, vw, vh);
  display_list_init(dl);
  paint_build(root, dl);
  return root;
}

static __html_node__ *find_id(__html_node__ *n, const char *id) {
  if (!n)
    return NULL;
  const char *nid = htmlattr_list_get(&n->attrs, "id");
  if (nid && !strcmp(nid, id))
    return n;
  for (int i = 0; i < n->child_count; i++) {
    __html_node__ *f = find_id(n->children[i], id);
    if (f)
      return f;
  }
  return NULL;
}

/* Count display list commands of a given type */
static int count_cmds(const __display_list__ *dl, __draw_cmd_type__ type) {
  int n = 0;
  for (int i = 0; i < dl->count; i++)
    if (dl->commands[i].type == type)
      n++;
  return n;
}

/* Find first CMD_RECT whose position is within tolerance of (x,y) */
static const DrawCommand *find_rect_near(const __display_list__ *dl, float x,
                                         float y) {
  for (int i = 0; i < dl->count; i++) {
    const DrawCommand *c = &dl->commands[i];
    if (c->type == CMD_RECT && fabsf(c->x - x) < 2.0f && fabsf(c->y - y) < 2.0f)
      return c;
  }
  return NULL;
}

/* Find first CMD_TEXT containing substring */
static const DrawCommand *find_text(const __display_list__ *dl,
                                    const char *sub) {
  for (int i = 0; i < dl->count; i++) {
    const DrawCommand *c = &dl->commands[i];
    if (c->type == CMD_TEXT && c->text && strstr(c->text, sub))
      return c;
  }
  return NULL;
}

/* =========================================================================
 * Test: display list builder
 * ========================================================================= */
static void test_display_list(void) {
  printf("\n=== Display List Builder Tests ===\n");

  /* Single rect: background color → one CMD_RECT */
  {
    __display_list__ dl;
    __html_node__ *root = run_pipeline(
        "<div id=\"box\"></div>",
        "#box { width:200px; height:100px; background-color:#1d4ed8; }", 800,
        600, &dl);
    CHECK(dl.count >= 1, "at least 1 command for colored box");
    CHECK(count_cmds(&dl, CMD_RECT) >= 1, "at least 1 CMD_RECT");
    const DrawCommand *r = find_rect_near(&dl, 0, 0);
    CHECK(r != NULL, "rect found at origin");
    if (r) {
      CHECK_F(r->w, 200, "rect width=200");
      CHECK_F(r->h, 100, "rect height=100");
      CHECK(r->fill_color.r == 0x1d && r->fill_color.g == 0x4e &&
                r->fill_color.b == 0xd8,
            "rect fill color=#1d4ed8");
    }
    display_list_free(&dl);
    htmlnode_free(root);
  }

  /* Border radius stored in command */
  {
    __display_list__ dl;
    __html_node__ *root =
        run_pipeline("<div id=\"box\"></div>",
                     "#box { width:100px; height:50px; background-color:#fff; "
                     "border-radius:8px; }",
                     800, 600, &dl);
    const DrawCommand *r = find_rect_near(&dl, 0, 0);
    if (r)
      CHECK_F(r->border_radius, 8, "border-radius=8px in CMD_RECT");
    display_list_free(&dl);
    htmlnode_free(root);
  }

  /* Text node produces CMD_TEXT */
  {
    __display_list__ dl;
    __html_node__ *root =
        run_pipeline("<div id=\"box\"><h1>Hello World</h1></div>",
                     "#box { width:400px; height:200px; }"
                     "h1 { color:#ffffff; font-size:24px; }",
                     800, 600, &dl);
    CHECK(count_cmds(&dl, CMD_TEXT) >= 1, "text content → CMD_TEXT");
    const DrawCommand *t = find_text(&dl, "Hello World");
    CHECK(t != NULL, "CMD_TEXT contains 'Hello World'");
    if (t) {
      CHECK_F(t->font_size, 24, "text font-size=24px");
      CHECK(t->text_color.r == 255 && t->text_color.g == 255 &&
                t->text_color.b == 255,
            "text color=#ffffff");
    }
    display_list_free(&dl);
    htmlnode_free(root);
  }

  /* display:none → no commands */
  {
    __display_list__ dl;
    __html_node__ *root = run_pipeline(
        "<div id=\"a\"></div><div id=\"b\"></div>",
        "#a { width:100px; height:50px; background-color:#f00; display:none; }"
        "#b { width:100px; height:50px; background-color:#0f0; }",
        800, 600, &dl);
    /* Only #b should produce a rect */
    int rects = count_cmds(&dl, CMD_RECT);
    CHECK(rects == 1, "display:none produces no CMD_RECT (1 rect total)");
    display_list_free(&dl);
    htmlnode_free(root);
  }

  /* Multiple children → correct command order (painter's order) */
  {
    __display_list__ dl;
    __html_node__ *root =
        run_pipeline("<div id=\"parent\">"
                     "  <div id=\"a\"></div>"
                     "  <div id=\"b\"></div>"
                     "</div>",
                     "#parent { width:400px; background-color:#111; }"
                     "#a { height:50px; background-color:#f00; }"
                     "#b { height:50px; background-color:#00f; }",
                     800, 600, &dl);
    CHECK(count_cmds(&dl, CMD_RECT) >= 3, "parent + 2 children → 3 rects");
    /* Parent rect should come before children (painter's order) */
    int parent_idx = -1, a_idx = -1;
    for (int i = 0; i < dl.count; i++) {
      const DrawCommand *c = &dl.commands[i];
      if (c->type != CMD_RECT)
        continue;
      if (c->fill_color.r == 0x11 && c->fill_color.g == 0x11)
        parent_idx = i;
      if (c->fill_color.r == 0xff && c->fill_color.g == 0x00)
        a_idx = i;
    }
    CHECK(parent_idx >= 0 && a_idx >= 0 && parent_idx < a_idx,
          "parent CMD_RECT emitted before child CMD_RECT");
    display_list_free(&dl);
    htmlnode_free(root);
  }

  /* Flex container with gap → children positioned correctly */
  {
    __display_list__ dl;
    __html_node__ *root = run_pipeline(
        "<div id=\"row\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "</div>",
        "#row { display:flex; flex-direction:row; width:400px; gap:20px; }"
        "#a   { width:100px; height:40px; background-color:#f00; }"
        "#b   { width:100px; height:40px; background-color:#00f; }",
        800, 600, &dl);
    const DrawCommand *a_r = NULL, *b_r = NULL;
    for (int i = 0; i < dl.count; i++) {
      const DrawCommand *c = &dl.commands[i];
      if (c->type != CMD_RECT)
        continue;
      if (c->fill_color.r == 0xff && c->fill_color.g == 0x00)
        a_r = c;
      if (c->fill_color.b == 0xff && c->fill_color.r == 0x00)
        b_r = c;
    }
    if (a_r && b_r) {
      CHECK_F(a_r->x, 0, "flex a.x=0 in display list");
      CHECK_F(b_r->x, 120, "flex b.x=120 (100+20gap) in display list");
    }
    display_list_free(&dl);
    htmlnode_free(root);
  }
}

/* =========================================================================
 * Test: software renderer pixel output
 * ========================================================================= */
static void test_soft_renderer(void) {
  printf("\n=== Software Renderer Tests ===\n");

  /* Create renderer */
  SoftRenderer *sr = soft_renderer_create(200, 100);
  CHECK(sr != NULL, "soft_renderer_create succeeds");
  if (!sr)
    return;

  /* Clear to red */
  soft_renderer_clear(sr, (Color){255, 0, 0, 255});
  const uint8_t *px = soft_renderer_pixels(sr);
  CHECK(px != NULL, "pixel buffer is accessible");
  if (px) {
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0,
          "clear to red → first pixel is red");
  }

  /* Draw a blue rect, check center pixel */
  __display_list__ dl;
  display_list_init(&dl);
  DrawCommand rect = {0};
  rect.type = CMD_RECT;
  rect.x = 50;
  rect.y = 25;
  rect.w = 100;
  rect.h = 50;
  rect.fill_color = (Color){0, 0, 255, 255};
  display_list_push(&dl, rect);
  soft_renderer_draw(sr, &dl);
  display_list_free(&dl);

  /* Center of rect is at (100, 50) */
  px = soft_renderer_pixels(sr);
  if (px) {
    const uint8_t *p = px + (50 * 200 + 100) * 4;
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255,
          "blue rect center pixel is blue");
  }

  /* Rounded corner — corner pixel should be red (background) not blue */
  {
    SoftRenderer *sr2 = soft_renderer_create(100, 100);
    soft_renderer_clear(sr2, (Color){255, 0, 0, 255});
    __display_list__ dl2;
    display_list_init(&dl2);
    DrawCommand rc = {0};
    rc.type = CMD_RECT;
    rc.x = 0;
    rc.y = 0;
    rc.w = 100;
    rc.h = 100;
    rc.fill_color = (Color){0, 0, 255, 255};
    rc.border_radius = 20;
    display_list_push(&dl2, rc);
    soft_renderer_draw(sr2, &dl2);
    display_list_free(&dl2);
    const uint8_t *px2 = soft_renderer_pixels(sr2);
    /* Top-left corner pixel (1,1) should be mostly red (outside radius) */
    if (px2) {
      const uint8_t *corner = px2 + (1 * 100 + 1) * 4;
      /* We just check it's not solidly blue */
      CHECK(corner[2] < 200, "rounded corner pixel is not solid blue");
    }
    soft_renderer_destroy(sr2);
  }

  /* Save PPM */
  {
    int r = soft_renderer_save_ppm(sr, "/tmp/test_soft.ppm");
    CHECK(r == 0, "soft_renderer_save_ppm returns 0");
    FILE *f = fopen("/tmp/test_soft.ppm", "rb");
    CHECK(f != NULL, "PPM file exists on disk");
    if (f) {
      char magic[3] = {0};
      fread(magic, 1, 2, f);
      CHECK(magic[0] == 'P' && magic[1] == '6', "PPM file starts with P6");
      fclose(f);
    }
  }

  soft_renderer_destroy(sr);
}

/* =========================================================================
 * Full pipeline → PPM image
 * ========================================================================= */
static void test_full_pipeline(const char *html_path, const char *css_path,
                               const char *out_ppm) {
  printf("\n=== Full Pipeline Render Test (800x600) ===\n");
  char err[512];

  __html_node__ *root = html_parse_file(html_path, err, sizeof(err));
  if (!root) {
    printf("  [FAIL] HTML: %s\n", err);
    failed++;
    return;
  }

  __css_rule_list__ rules;
  css_rule_list_init(&rules);
  if (css_parse_file(css_path, &rules, err, sizeof(err)) != 0) {
    printf("  [FAIL] CSS: %s\n", err);
    failed++;
    htmlnode_free(root);
    css_rule_list_free(&rules);
    return;
  }

  cascade_apply(root, &rules, 800, 600);
  layout_run(root, 800, 600);

  __display_list__ dl;
  display_list_init(&dl);
  paint_build(root, &dl);

  printf("  [INFO] Display list: %d commands (%d rects, %d texts)\n", dl.count,
         count_cmds(&dl, CMD_RECT), count_cmds(&dl, CMD_TEXT));

  CHECK(dl.count > 0, "display list is non-empty");
  CHECK(count_cmds(&dl, CMD_RECT) > 0, "at least one CMD_RECT");
  CHECK(count_cmds(&dl, CMD_TEXT) > 0, "at least one CMD_TEXT");

  printf("\n  [Display List Dump]\n");
  paint_dump(&dl);

  /* Render to software buffer */
  SoftRenderer *sr = soft_renderer_create(800, 600);
  CHECK(sr != NULL, "soft renderer created for 800x600");
  if (sr) {
    /* Clear to app background color */
    soft_renderer_clear(sr, (Color){15, 17, 23, 255}); /* #0f1117 */
    soft_renderer_draw(sr, &dl);

    int ret = soft_renderer_save_ppm(sr, out_ppm);
    if (ret == 0) {
      printf("  [PASS] Rendered image saved: %s\n", out_ppm);
      passed++;
    } else {
      printf("  [FAIL] Could not write %s\n", out_ppm);
      failed++;
    }
    soft_renderer_destroy(sr);
  }

  display_list_free(&dl);
  css_rule_list_free(&rules);
  htmlnode_free(root);
}

/* =========================================================================
 * Entry point
 * =========================================================================*/
int main(int argc, char *argv[]) {
  printf("htmlui — Milestone 4: Display List + Renderer Test\n");
  printf("===================================================\n");

  test_display_list();
  test_soft_renderer();

  if (argc >= 4) {
    test_full_pipeline(argv[1], argv[2], argv[3]);
  } else if (argc >= 3) {
    test_full_pipeline(argv[1], argv[2], "output.ppm");
  }

  printf("\n===================================================\n");
  printf("Results: %d passed, %d failed\n", passed, failed);
  printf("===================================================\n");
  return failed > 0 ? 1 : 0;
}
