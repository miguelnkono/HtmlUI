#define _POSIX_C_SOURCE 200809L
/**
 * tests/test_layout.c
 *
 * Milestone 3 test harness — layout engine.
 *
 * Usage:
 *   ./test_layout
 *   ./test_layout ../examples/hello/views/main.html \
 *                  ../examples/hello/styles/app.css
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/internal/types.h"
#include "../src/parser/html.h"
#include "../src/parser/css.h"
#include "../src/style/cascade.h"
#include "../src/layout/layout.h"

/* ── test framework ───────────────────────────────────────────────────── */
static int passed=0, failed=0;
#define CHECK(cond,msg) \
    do{if(cond){printf("  [PASS] %s\n",msg);passed++;}else{printf("  [FAIL] %s\n",msg);failed++;}}while(0)
#define CHECK_F(a,b,msg) \
    do{float _a=(float)(a),_b=(float)(b);\
       if(fabsf(_a-_b)<1.0f){printf("  [PASS] %s (%.0f)\n",msg,_a);passed++;}\
       else{printf("  [FAIL] %s  got=%.1f  expected=%.1f\n",msg,_a,_b);failed++;}}while(0)
#define CHECK_GE(a,b,msg) \
    do{float _a=(float)(a),_b=(float)(b);\
       if(_a>=_b){printf("  [PASS] %s (%.0f >= %.0f)\n",msg,_a,_b);passed++;}\
       else{printf("  [FAIL] %s  got=%.1f  expected>=%.1f\n",msg,_a,_b);failed++;}}while(0)

/* Build, cascade and layout from inline HTML+CSS */
static __html_node__* build(const char* html, const char* css,
                        float vw, float vh) {
    char err[256];
    __html_node__* root = html_parse_string(html, err, sizeof(err));
    if (!root) { printf("  HTML err: %s\n", err); return NULL; }
    __css_rule_list__ rules; css_rule_list_init(&rules);
    css_parse_string(css, &rules, err, sizeof(err));
    cascade_apply(root, &rules, vw, vh);
    css_rule_list_free(&rules);
    layout_run(root, vw, vh);
    return root;
}

static __html_node__* find_id(__html_node__* n, const char* id) {
    if (!n) return NULL;
    const char* nid = htmlattr_list_get(&n->attrs, "id");
    if (nid && strcmp(nid,id)==0) return n;
    for (int i=0;i<n->child_count;i++) {
        __html_node__* f = find_id(n->children[i],id);
        if (f) return f;
    }
    return NULL;
}
static __html_node__* find_tag(__html_node__* n, const char* tag) {
    if (!n) return NULL;
    if (strcmp(n->tag,tag)==0) return n;
    for (int i=0;i<n->child_count;i++) {
        __html_node__* f = find_tag(n->children[i],tag);
        if (f) return f;
    }
    return NULL;
}

/* =========================================================================
 * Test: block layout basics
 * ========================================================================= */
static void test_block_basic(void) {
    printf("\n=== Block Layout Tests ===\n");

    /* Explicit width/height */
    {
        __html_node__* root = build(
            "<div id=\"box\"></div>",
            "#box { display:block; width:200px; height:100px; }", 800, 600);
        __html_node__* box = find_id(root,"box");
        CHECK(box && box->layout, "explicit-size box has LayoutBox");
        if (box && box->layout) {
            CHECK_F(box->layout->width,  200, "block width=200px");
            CHECK_F(box->layout->height, 100, "block height=100px");
        }
        htmlnode_free(root);
    }

    /* Takes full available width when width is auto */
    {
        __html_node__* root = build(
            "<div id=\"box\"></div>",
            "#box { display:block; }", 800, 600);
        __html_node__* box = find_id(root,"box");
        if (box && box->layout)
            CHECK_F(box->layout->width, 800, "auto width fills container (800px)");
        htmlnode_free(root);
    }

    /* Position: root at (0,0) */
    {
        __html_node__* root = build(
            "<div id=\"box\"></div>",
            "#box { width:100px; height:50px; }", 800, 600);
        __html_node__* box = find_id(root,"box");
        if (box && box->layout) {
            CHECK_F(box->layout->x, 0, "root-level node x=0");
            CHECK_F(box->layout->y, 0, "root-level node y=0");
        }
        htmlnode_free(root);
    }

    /* Padding shrinks content area */
    {
        __html_node__* root = build(
            "<div id=\"box\"></div>",
            "#box { width:200px; height:100px; padding:20px; }", 800, 600);
        __html_node__* box = find_id(root,"box");
        if (box && box->layout) {
            CHECK_F(box->layout->content_width,  160, "padding: content_w=160");
            CHECK_F(box->layout->content_height,  60, "padding: content_h=60");
            CHECK_F(box->layout->content_x,       20, "padding: content_x=20");
            CHECK_F(box->layout->content_y,       20, "padding: content_y=20");
        }
        htmlnode_free(root);
    }
}

/* =========================================================================
 * Test: block stacking — children stack vertically
 * ========================================================================= */
static void test_block_stacking(void) {
    printf("\n=== Block Stacking Tests ===\n");

    __html_node__* root = build(
        "<div id=\"parent\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "  <div id=\"c\"></div>"
        "</div>",
        "#parent { width:400px; }"
        "#a { height:50px; }"
        "#b { height:80px; }"
        "#c { height:30px; }",
        800, 600);

    __html_node__* a = find_id(root,"a");
    __html_node__* b = find_id(root,"b");
    __html_node__* c = find_id(root,"c");

    CHECK(a&&a->layout && b&&b->layout && c&&c->layout, "all children have layout");

    if (a&&a->layout && b&&b->layout && c&&c->layout) {
        CHECK_F(a->layout->y,  0,  "a starts at y=0");
        CHECK_F(b->layout->y, 50,  "b starts at y=50");
        CHECK_F(c->layout->y,130,  "c starts at y=130 (50+80)");
    }

    /* Parent height grows to contain children */
    __html_node__* parent = find_id(root,"parent");
    if (parent && parent->layout)
        CHECK_GE(parent->layout->height, 160, "parent height >= 160 (50+80+30)");

    htmlnode_free(root);
}

/* =========================================================================
 * Test: margin
 * ========================================================================= */
static void test_margins(void) {
    printf("\n=== Margin Tests ===\n");

    __html_node__* root = build(
        "<div id=\"parent\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "</div>",
        "#parent { width:400px; }"
        "#a { height:40px; margin-bottom:20px; }"
        "#b { height:40px; margin-top:10px; }",
        800, 600);

    __html_node__* a = find_id(root,"a");
    __html_node__* b = find_id(root,"b");

    if (a&&a->layout && b&&b->layout) {
        CHECK_F(a->layout->y, 0,  "a y=0");
        CHECK_F(b->layout->y, 70, "b y=70 (40 + margin-bottom:20 + margin-top:10)");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: display:none
 * ========================================================================= */
static void test_display_none(void) {
    printf("\n=== display:none Tests ===\n");

    __html_node__* root = build(
        "<div id=\"parent\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"hidden\"></div>"
        "  <div id=\"b\"></div>"
        "</div>",
        "#parent { width:400px; }"
        "#a      { height:50px; }"
        "#hidden { display:none; height:999px; }"
        "#b      { height:50px; }",
        800, 600);

    __html_node__* hidden = find_id(root,"hidden");
    __html_node__* b      = find_id(root,"b");

    if (hidden && hidden->layout) {
        CHECK_F(hidden->layout->width,  0, "display:none → width=0");
        CHECK_F(hidden->layout->height, 0, "display:none → height=0");
    }
    if (b && b->layout)
        CHECK_F(b->layout->y, 50, "b follows a (hidden takes no space)");

    htmlnode_free(root);
}

/* =========================================================================
 * Test: flex row — children side by side
 * ========================================================================= */
static void test_flex_row(void) {
    printf("\n=== Flex Row Tests ===\n");

    __html_node__* root = build(
        "<div id=\"row\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "  <div id=\"c\"></div>"
        "</div>",
        "#row { display:flex; flex-direction:row; width:300px; height:60px; }"
        "#a   { width:80px; height:60px; }"
        "#b   { width:80px; height:60px; }"
        "#c   { width:80px; height:60px; }",
        800, 600);

    __html_node__* a = find_id(root,"a");
    __html_node__* b = find_id(root,"b");
    __html_node__* c = find_id(root,"c");

    CHECK(a&&a->layout && b&&b->layout && c&&c->layout, "all flex children have layout");
    if (a&&a->layout && b&&b->layout && c&&c->layout) {
        CHECK_F(a->layout->x,   0, "flex-row: a.x=0");
        CHECK_F(b->layout->x,  80, "flex-row: b.x=80");
        CHECK_F(c->layout->x, 160, "flex-row: c.x=160");
        /* All same y */
        CHECK_F(a->layout->y, b->layout->y, "flex-row: all same y");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: flex column
 * ========================================================================= */
static void test_flex_column(void) {
    printf("\n=== Flex Column Tests ===\n");

    __html_node__* root = build(
        "<div id=\"col\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "</div>",
        "#col { display:flex; flex-direction:column; width:200px; }"
        "#a   { height:40px; }"
        "#b   { height:60px; }",
        800, 600);

    __html_node__* a = find_id(root,"a");
    __html_node__* b = find_id(root,"b");

    if (a&&a->layout && b&&b->layout) {
        CHECK_F(a->layout->y,  0, "flex-col: a.y=0");
        CHECK_F(b->layout->y, 40, "flex-col: b.y=40");
        CHECK_F(a->layout->x,  b->layout->x, "flex-col: same x");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: flex gap
 * ========================================================================= */
static void test_flex_gap(void) {
    printf("\n=== Flex Gap Tests ===\n");

    __html_node__* root = build(
        "<div id=\"row\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "</div>",
        "#row { display:flex; flex-direction:row; width:500px; gap:20px; }"
        "#a   { width:100px; height:40px; }"
        "#b   { width:100px; height:40px; }",
        800, 600);

    __html_node__* a = find_id(root,"a");
    __html_node__* b = find_id(root,"b");

    if (a&&a->layout && b&&b->layout) {
        CHECK_F(a->layout->x,   0, "gap: a.x=0");
        CHECK_F(b->layout->x, 120, "gap: b.x=120 (100 + 20 gap)");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: flex-grow distributes space
 * ========================================================================= */
static void test_flex_grow(void) {
    printf("\n=== Flex-grow Tests ===\n");

    /* Two children, one flex-grow:1, one fixed 100px.
     * Container is 400px. Fixed child takes 100, grown child gets 300. */
    __html_node__* root = build(
        "<div id=\"row\">"
        "  <div id=\"fixed\"></div>"
        "  <div id=\"grown\"></div>"
        "</div>",
        "#row   { display:flex; flex-direction:row; width:400px; height:50px; }"
        "#fixed { width:100px; }"
        "#grown { flex-grow:1; }",
        800, 600);

    __html_node__* fixed = find_id(root,"fixed");
    __html_node__* grown = find_id(root,"grown");

    if (fixed&&fixed->layout && grown&&grown->layout) {
        CHECK_F(fixed->layout->width, 100, "fixed child stays 100px");
        CHECK_F(grown->layout->width, 300, "flex-grow:1 gets remaining 300px");
        CHECK_F(grown->layout->x,     100, "grown child starts at x=100");
    }
    htmlnode_free(root);

    /* Two equal flex-grow children split evenly */
    root = build(
        "<div id=\"row\">"
        "  <div id=\"a\"></div>"
        "  <div id=\"b\"></div>"
        "</div>",
        "#row { display:flex; flex-direction:row; width:400px; height:50px; }"
        "#a   { flex-grow:1; }"
        "#b   { flex-grow:1; }",
        800, 600);

    __html_node__* a = find_id(root,"a");
    __html_node__* b = find_id(root,"b");
    if (a&&a->layout && b&&b->layout) {
        CHECK_F(a->layout->width, 200, "equal grow: a gets 200px");
        CHECK_F(b->layout->width, 200, "equal grow: b gets 200px");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: justify-content
 * ========================================================================= */
static void test_justify_content(void) {
    printf("\n=== justify-content Tests ===\n");

    /* center: 300px container, two 100px children → 50px on each side */
    {
        __html_node__* root = build(
            "<div id=\"row\"><div id=\"a\"></div><div id=\"b\"></div></div>",
            "#row { display:flex; flex-direction:row; width:400px; justify-content:center; }"
            "#a   { width:100px; height:40px; }"
            "#b   { width:100px; height:40px; }",
            800, 600);
        __html_node__* a = find_id(root,"a");
        __html_node__* b = find_id(root,"b");
        if (a&&a->layout && b&&b->layout) {
            CHECK_F(a->layout->x, 100, "justify:center — a.x=100");
            CHECK_F(b->layout->x, 200, "justify:center — b.x=200");
        }
        htmlnode_free(root);
    }

    /* flex-end: two 100px children in 400px container → start at x=200 */
    {
        __html_node__* root = build(
            "<div id=\"row\"><div id=\"a\"></div><div id=\"b\"></div></div>",
            "#row { display:flex; flex-direction:row; width:400px; justify-content:flex-end; }"
            "#a   { width:100px; height:40px; }"
            "#b   { width:100px; height:40px; }",
            800, 600);
        __html_node__* a = find_id(root,"a");
        __html_node__* b = find_id(root,"b");
        if (a&&a->layout && b&&b->layout) {
            CHECK_F(a->layout->x, 200, "justify:flex-end — a.x=200");
            CHECK_F(b->layout->x, 300, "justify:flex-end — b.x=300");
        }
        htmlnode_free(root);
    }
}

/* =========================================================================
 * Test: align-items (cross axis)
 * ========================================================================= */
static void test_align_items(void) {
    printf("\n=== align-items Tests ===\n");

    /* center: container 100px tall, child 40px tall → child.y = 30 */
    {
        __html_node__* root = build(
            "<div id=\"row\"><div id=\"child\"></div></div>",
            "#row   { display:flex; flex-direction:row; width:200px; height:100px; align-items:center; }"
            "#child { width:80px; height:40px; }",
            800, 600);
        __html_node__* child = find_id(root,"child");
        if (child && child->layout)
            CHECK_F(child->layout->y, 30, "align-items:center — child.y=30");
        htmlnode_free(root);
    }

    /* flex-end: child.y = container_h - child_h = 60 */
    {
        __html_node__* root = build(
            "<div id=\"row\"><div id=\"child\"></div></div>",
            "#row   { display:flex; flex-direction:row; width:200px; height:100px; align-items:flex-end; }"
            "#child { width:80px; height:40px; }",
            800, 600);
        __html_node__* child = find_id(root,"child");
        if (child && child->layout)
            CHECK_F(child->layout->y, 60, "align-items:flex-end — child.y=60");
        htmlnode_free(root);
    }
}

/* =========================================================================
 * Test: nested flex + block
 * ========================================================================= */
static void test_nested(void) {
    printf("\n=== Nested Layout Tests ===\n");

    __html_node__* root = build(
        "<div id=\"outer\">"
        "  <div id=\"row\">"
        "    <div id=\"left\"></div>"
        "    <div id=\"right\"></div>"
        "  </div>"
        "  <div id=\"footer\"></div>"
        "</div>",
        "#outer  { display:block; width:400px; }"
        "#row    { display:flex; flex-direction:row; height:80px; }"
        "#left   { width:150px; }"
        "#right  { flex-grow:1; }"
        "#footer { height:40px; }",
        800, 600);

    __html_node__* left   = find_id(root,"left");
    __html_node__* right  = find_id(root,"right");
    __html_node__* footer = find_id(root,"footer");

    if (left&&left->layout && right&&right->layout) {
        CHECK_F(left->layout->x,   0,   "nested: left.x=0");
        CHECK_F(left->layout->width, 150, "nested: left.w=150");
        CHECK_F(right->layout->x, 150,  "nested: right.x=150");
        CHECK_F(right->layout->width, 250, "nested: right flex-grows to 250");
    }
    if (footer && footer->layout)
        CHECK_F(footer->layout->y, 80, "nested: footer below row (y=80)");

    htmlnode_free(root);
}

/* =========================================================================
 * Test: full file-based layout
 * ========================================================================= */
static void test_from_files(const char* html_path, const char* css_path) {
    printf("\n=== File-based Layout Test (800x600) ===\n");
    char err[512];
    __html_node__* root = html_parse_file(html_path, err, sizeof(err));
    if (!root) { printf("  [FAIL] HTML: %s\n", err); failed++; return; }
    __css_rule_list__ rules; css_rule_list_init(&rules);
    if (css_parse_file(css_path, &rules, err, sizeof(err)) != 0) {
        printf("  [FAIL] CSS: %s\n", err); failed++;
        htmlnode_free(root); css_rule_list_free(&rules); return;
    }
    cascade_apply(root, &rules, 800, 600);
    layout_run(root, 800, 600);
    printf("  [PASS] layout_run completed\n"); passed++;
    printf("\n  [Layout Dump]\n");
    layout_dump(root, 2);

    /* Spot-check root container */
    __html_node__* app = find_id(root, "root");
    if (app && app->layout) {
        CHECK_F(app->layout->x, 0,   "app x=0");
        CHECK_F(app->layout->y, 0,   "app y=0");
        CHECK_GE(app->layout->width,  1,  "app has width");
        CHECK_GE(app->layout->height, 1,  "app has height");
        CHECK_F(app->layout->content_x, 32, "app content_x=32 (padding:32px)");
        CHECK_F(app->layout->content_y, 32, "app content_y=32 (padding:32px)");
    }
    css_rule_list_free(&rules);
    htmlnode_free(root);
}

/* =========================================================================
 * Entry point
 * ========================================================================= */
int main(int argc, char* argv[]) {
    printf("htmlui — Milestone 3: Layout Engine Test Harness\n");
    printf("=================================================\n");

    test_block_basic();
    test_block_stacking();
    test_margins();
    test_display_none();
    test_flex_row();
    test_flex_column();
    test_flex_gap();
    test_flex_grow();
    test_justify_content();
    test_align_items();
    test_nested();

    if (argc >= 3)
        test_from_files(argv[1], argv[2]);

    printf("\n=================================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("=================================================\n");
    return failed > 0 ? 1 : 0;
}
