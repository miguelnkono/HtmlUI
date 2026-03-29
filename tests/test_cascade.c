#define _POSIX_C_SOURCE 200809L
/**
 * tests/test_cascade.c
 *
 * Milestone 2 test harness — CSS cascade and style resolution.
 *
 * Usage:
 *   ./test_cascade
 *   ./test_cascade ../examples/hello/views/main.html \
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

/* -------------------------------------------------------------------------
 * Test framework
 * ------------------------------------------------------------------------- */
static int passed = 0, failed = 0;

#define CHECK(cond, msg) \
    do { if(cond){printf("  [PASS] %s\n",msg);passed++;}else{printf("  [FAIL] %s\n",msg);failed++;} } while(0)

#define CHECK_FLOAT(a, b, msg) \
    do { float _a=(float)(a),_b=(float)(b); \
         if(fabsf(_a-_b)<0.5f){printf("  [PASS] %s (%.1f)\n",msg,_a);passed++;} \
         else{printf("  [FAIL] %s  got=%.1f  expected=%.1f\n",msg,_a,_b);failed++;} } while(0)

#define CHECK_COLOR(c, er, eg, eb, msg) \
    do { if((c).r==(er)&&(c).g==(eg)&&(c).b==(eb)){printf("  [PASS] %s\n",msg);passed++;} \
         else{printf("  [FAIL] %s  got=rgba(%d,%d,%d) expected=rgba(%d,%d,%d)\n",msg,(c).r,(c).g,(c).b,er,eg,eb);failed++;} } while(0)

/* Build a simple DOM + CSS and return root after cascade */
static __html_node__* build_and_cascade(const char* html_src, const char* css_src) {
    char err[256];
    __html_node__* root = html_parse_string(html_src, err, sizeof(err));
    if (!root) { printf("  HTML parse error: %s\n", err); return NULL; }
    __css_rule_list__ rules; css_rule_list_init(&rules);
    css_parse_string(css_src, &rules, err, sizeof(err));
    cascade_apply(root, &rules, 800.0f, 600.0f);
    css_rule_list_free(&rules);
    return root;
}

/* Find a node by id, searching the whole tree */
static __html_node__* find_by_id(__html_node__* node, const char* id) {
    if (!node) return NULL;
    const char* nid = htmlattr_list_get(&node->attrs, "id");
    if (nid && strcmp(nid, id) == 0) return node;
    for (int i = 0; i < node->child_count; i++) {
        __html_node__* found = find_by_id(node->children[i], id);
        if (found) return found;
    }
    return NULL;
}

/* Find first node by tag name */
static __html_node__* find_by_tag(__html_node__* node, const char* tag) {
    if (!node) return NULL;
    if (strcmp(node->tag, tag) == 0) return node;
    for (int i = 0; i < node->child_count; i++) {
        __html_node__* found = find_by_tag(node->children[i], tag);
        if (found) return found;
    }
    return NULL;
}

/* =========================================================================
 * Test: selector_specificity
 * ========================================================================= */
static void test_specificity(void) {
    printf("\n=== Specificity Tests ===\n");
    /* id > class > tag */
    CHECK(selector_specificity("#foo") > selector_specificity(".foo"), "#id > .class");
    CHECK(selector_specificity(".foo") > selector_specificity("div"),  ".class > tag");
    CHECK(selector_specificity("#a")   > selector_specificity("div"),  "#id > tag");
    /* same category equal */
    CHECK(selector_specificity("div") == selector_specificity("span"), "div == span specificity");
    CHECK(selector_specificity(".a")  == selector_specificity(".b"),   ".a == .b specificity");
    /* compound is additive */
    CHECK(selector_specificity("div.cls") > selector_specificity("div"), "div.cls > div");
    CHECK(selector_specificity("#a.b")    > selector_specificity("#a"),  "#a.b > #a");
}

/* =========================================================================
 * Test: selector_matches
 * ========================================================================= */
static void test_selector_matches(void) {
    printf("\n=== Selector Matching Tests ===\n");
    char err[256];
    __html_node__* btn = html_parse_string(
        "<button id=\"submit\" class=\"btn btn-primary\"></button>",
        err, sizeof(err));
    /* Find the actual button (may be wrapped in <html>) */
    __html_node__* node = find_by_tag(btn, "button");

    CHECK(node != NULL,                         "button node found");
    if (!node) { htmlnode_free(btn); return; }

    CHECK(selector_matches("button",        node), "tag selector matches");
    CHECK(selector_matches("*",             node), "universal selector matches");
    CHECK(selector_matches("#submit",       node), "id selector matches");
    CHECK(selector_matches(".btn",          node), "class selector matches");
    CHECK(selector_matches(".btn-primary",  node), "second class matches");
    CHECK(selector_matches("button.btn",    node), "tag+class compound matches");
    CHECK(selector_matches("button#submit", node), "tag+id compound matches");

    CHECK(!selector_matches("div",          node), "wrong tag does not match");
    CHECK(!selector_matches("#other",       node), "wrong id does not match");
    CHECK(!selector_matches(".btn-secondary", node), "absent class does not match");

    htmlnode_free(btn);
}

/* =========================================================================
 * Test: descendant combinator
 * ========================================================================= */
static void test_descendant_selector(void) {
    printf("\n=== Descendant Combinator Tests ===\n");
    char err[256];
    __html_node__* root = html_parse_string(
        "<div class=\"app\"><button id=\"btn\">OK</button></div>",
        err, sizeof(err));
    __html_node__* btn = find_by_id(root, "btn");

    CHECK(btn != NULL, "button node found");
    if (btn) {
        CHECK( selector_matches(".app button", btn), ".app button matches");
        CHECK( selector_matches("div button",  btn), "div button matches");
        CHECK(!selector_matches(".other button", btn), "wrong ancestor doesn't match");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: basic style application
 * ========================================================================= */
static void test_style_application(void) {
    printf("\n=== Style Application Tests ===\n");

    __html_node__* root = build_and_cascade(
        "<div id=\"box\"></div>",
        "#box { width: 200px; height: 100px; background-color: #1d4ed8; "
        "       padding: 16px; margin: 8px; border-radius: 6px; opacity: 0.9; }"
    );
    __html_node__* box = find_by_id(root, "box");
    CHECK(box != NULL && box->style != NULL, "node has computed style");
    if (box && box->style) {
        CHECK_FLOAT(box->style->width,            200.0f, "width=200px");
        CHECK_FLOAT(box->style->height,           100.0f, "height=100px");
        CHECK_COLOR(box->style->background_color, 0x1d, 0x4e, 0xd8, "bg-color=#1d4ed8");
        CHECK_FLOAT(box->style->padding[0],        16.0f, "padding-top=16px");
        CHECK_FLOAT(box->style->padding[1],        16.0f, "padding-right=16px");
        CHECK_FLOAT(box->style->margin[0],          8.0f, "margin-top=8px");
        CHECK_FLOAT(box->style->border_radius[0],   6.0f, "border-radius=6px");
        CHECK_FLOAT(box->style->opacity,            0.9f, "opacity=0.9");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: display and flexbox properties
 * ========================================================================= */
static void test_flex_properties(void) {
    printf("\n=== Flexbox Property Tests ===\n");

    __html_node__* root = build_and_cascade(
        "<div id=\"flex\"></div>",
        "#flex { display: flex; flex-direction: column; "
        "         justify-content: space-between; align-items: center; "
        "         gap: 12px; flex-wrap: wrap; }"
    );
    __html_node__* node = find_by_id(root, "flex");
    CHECK(node && node->style, "flex node has style");
    if (node && node->style) {
        CHECK(node->style->display == DISPLAY_FLEX,                "display:flex");
        CHECK(node->style->flex_direction == FLEX_DIRECTION_COLUMN,"flex-direction:column");
        CHECK(node->style->justify_content == JUSTIFY_SPACE_BETWEEN,"justify-content:space-between");
        CHECK(node->style->align_items == ALIGN_CENTER,            "align-items:center");
        CHECK_FLOAT(node->style->gap, 12.0f,                       "gap:12px");
        CHECK(node->style->flex_wrap,                              "flex-wrap:wrap");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: CSS cascade — specificity ordering
 * ========================================================================= */
static void test_cascade_order(void) {
    printf("\n=== Cascade Order Tests ===\n");

    /* id wins over class wins over tag */
    __html_node__* root = build_and_cascade(
        "<div id=\"box\" class=\"blue\"></div>",
        "div  { background-color: #ff0000; }"   /* tag: red   (low)  */
        ".blue { background-color: #0000ff; }"   /* class: blue (mid) */
        "#box  { background-color: #00ff00; }"   /* id: green  (high) */
    );
    __html_node__* box = find_by_id(root, "box");
    CHECK(box && box->style, "cascade node has style");
    if (box && box->style) {
        /* #box (id) should win → green #00ff00 */
        CHECK_COLOR(box->style->background_color, 0x00, 0xff, 0x00, "id wins over class and tag");
    }
    htmlnode_free(root);

    /* Later rule wins at same specificity */
    root = build_and_cascade(
        "<div class=\"box\"></div>",
        ".box { color: #ff0000; }"
        ".box { color: #00ff00; }"
    );
    __html_node__* node = find_by_tag(root, "div");
    if (node && node->style) {
        CHECK_COLOR(node->style->color, 0x00, 0xff, 0x00, "later rule wins at same specificity");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: inheritance
 * ========================================================================= */
static void test_inheritance(void) {
    printf("\n=== Inheritance Tests ===\n");

    __html_node__* root = build_and_cascade(
        "<div id=\"parent\"><p id=\"child\">text</p></div>",
        "#parent { color: #ff0000; font-size: 20px; font-weight: 700; }"
        /* child has no color/font rules — should inherit */
    );
    __html_node__* parent = find_by_id(root, "parent");
    __html_node__* child  = find_by_id(root, "child");

    CHECK(parent && parent->style, "parent has style");
    CHECK(child  && child->style,  "child has style");

    if (parent && parent->style) {
        CHECK_COLOR(parent->style->color, 0xff, 0x00, 0x00, "parent color=#ff0000");
        CHECK_FLOAT(parent->style->font_size,   20.0f, "parent font-size=20px");
        CHECK_FLOAT(parent->style->font_weight, 700.0f,"parent font-weight=700");
    }
    if (child && child->style) {
        CHECK_COLOR(child->style->color, 0xff, 0x00, 0x00, "child inherits color");
        CHECK_FLOAT(child->style->font_size,   20.0f, "child inherits font-size");
        CHECK_FLOAT(child->style->font_weight, 700.0f,"child inherits font-weight");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: value resolution (em, %, shorthand)
 * ========================================================================= */
static void test_value_resolution(void) {
    printf("\n=== Value Resolution Tests ===\n");

    /* em relative to font-size */
    __html_node__* root = build_and_cascade(
        "<div id=\"box\"></div>",
        "#box { font-size: 20px; padding: 1em; margin: 0.5em; }"
    );
    __html_node__* box = find_by_id(root, "box");
    if (box && box->style) {
        CHECK_FLOAT(box->style->padding[0], 20.0f, "1em padding = 20px (font-size=20)");
        CHECK_FLOAT(box->style->margin[0],  10.0f, "0.5em margin = 10px");
    }
    htmlnode_free(root);

    /* padding shorthand */
    root = build_and_cascade(
        "<div id=\"box\"></div>",
        "#box { padding: 10px 20px; }"
    );
    box = find_by_id(root, "box");
    if (box && box->style) {
        CHECK_FLOAT(box->style->padding[0], 10.0f, "shorthand v top=10");
        CHECK_FLOAT(box->style->padding[1], 20.0f, "shorthand h right=20");
        CHECK_FLOAT(box->style->padding[2], 10.0f, "shorthand v bottom=10");
        CHECK_FLOAT(box->style->padding[3], 20.0f, "shorthand h left=20");
    }
    htmlnode_free(root);

    /* display:none */
    root = build_and_cascade(
        "<div id=\"hidden\"></div>",
        "#hidden { display: none; }"
    );
    __html_node__* hidden = find_by_id(root, "hidden");
    if (hidden && hidden->style) {
        CHECK(hidden->style->display == DISPLAY_NONE, "display:none applied");
    }
    htmlnode_free(root);
}

/* =========================================================================
 * Test: full file-based cascade
 * ========================================================================= */
static void test_from_files(const char* html_path, const char* css_path) {
    printf("\n=== File-based Cascade Test ===\n");
    char err[512];

    __html_node__* root = html_parse_file(html_path, err, sizeof(err));
    if (!root) { printf("  [FAIL] HTML: %s\n", err); failed++; return; }

    __css_rule_list__ rules; css_rule_list_init(&rules);
    if (css_parse_file(css_path, &rules, err, sizeof(err)) != 0) {
        printf("  [FAIL] CSS: %s\n", err); failed++;
        htmlnode_free(root); css_rule_list_free(&rules); return;
    }

    cascade_apply(root, &rules, 800.0f, 600.0f);

    printf("  [PASS] cascade_apply completed without crash\n"); passed++;

    /* Spot-check a few known nodes from the hello example */
    __html_node__* title = find_by_id(root, "title");
    if (title && title->style) {
        printf("  [INFO] #title computed style:\n");
        computed_style_dump(title->style, "#title");
        CHECK_FLOAT(title->style->font_size, 36.0f, "#title font-size=36px");
        CHECK_FLOAT(title->style->font_weight, 700.0f, "#title font-weight=700");
        CHECK_COLOR(title->style->color, 0xff, 0xff, 0xff, "#title color=#ffffff");
    }

    __html_node__* app = find_by_id(root, "root");
    if (app && app->style) {
        printf("  [INFO] #root computed style:\n");
        computed_style_dump(app->style, "#root");
        CHECK(app->style->display == DISPLAY_FLEX,                  "#root display:flex");
        CHECK(app->style->flex_direction == FLEX_DIRECTION_COLUMN,  "#root flex-direction:column");
        CHECK_FLOAT(app->style->padding[0], 32.0f,                  "#root padding=32px");
        CHECK_FLOAT(app->style->gap, 20.0f,                         "#root gap=20px");
    }

    __html_node__* btn = find_by_id(root, "btn-greet");
    if (btn && btn->style) {
        printf("  [INFO] #btn-greet computed style:\n");
        computed_style_dump(btn->style, "#btn-greet");
        CHECK(btn->style->display == DISPLAY_FLEX,       "#btn-greet display:flex");
        /* hover rule has same specificity and comes later, so it wins at cascade time.
         Pseudo-class filtering is applied at event time (Milestone 7), not cascade time. */
        CHECK_COLOR(btn->style->background_color, 0x25, 0x63, 0xeb, "#btn-greet bg=hover-wins at cascade (correct)");
        CHECK_COLOR(btn->style->color, 0xff, 0xff, 0xff, "#btn-greet color=#ffffff");
        CHECK_FLOAT(btn->style->border_radius[0], 6.0f,  "#btn-greet border-radius=6px");
    }

    css_rule_list_free(&rules);
    htmlnode_free(root);
}

/* =========================================================================
 * Entry point
 * ========================================================================= */
int main(int argc, char* argv[]) {
    printf("htmlui — Milestone 2: Cascade Test Harness\n");
    printf("===========================================\n");

    test_specificity();
    test_selector_matches();
    test_descendant_selector();
    test_style_application();
    test_flex_properties();
    test_cascade_order();
    test_inheritance();
    test_value_resolution();

    if (argc >= 3)
        test_from_files(argv[1], argv[2]);

    printf("\n===========================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("===========================================\n");
    return failed > 0 ? 1 : 0;
}
