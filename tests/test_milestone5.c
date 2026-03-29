#define _POSIX_C_SOURCE 200809L
/**
 * tests/test_milestone5.c
 *
 * Milestone 5 integration test — public C API, headless mode.
 *
 * Tests the full public API end-to-end without a window:
 *  - ui_load / ui_destroy
 *  - ui_query / ui_query_all
 *  - node_set_text / node_set_attr / node_set_style
 *  - node_add_class / node_remove_class / node_has_class / node_toggle_class
 *  - node_set_visible
 *  - node_get_rect
 *  - node_on_click callback dispatch
 *  - ui_render_frame (dirty flagging)
 *  - event hit-test
 *  - ui_reload
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Include the full internals so we can test events directly */
#include "../src/api/ui_internal.h"
#include "../src/events/events.h"
#include "../src/parser/html.h"
#include "../src/parser/css.h"
#include "../src/style/cascade.h"
#include "../src/layout/layout.h"
#include "../src/paint/paint.h"
#include "../src/render/soft.h"
#include "../include/htmlui.h"

/* ── test framework ───────────────────────────────────────────────────── */
static int passed = 0, failed = 0;
#define CHECK(cond, msg) \
    do{if(cond){printf("  [PASS] %s\n",msg);passed++;}else{printf("  [FAIL] %s\n",msg);failed++;}}while(0)
#define CHECK_STR(a, b, msg) \
    do{ const char* _a=(a); const char* _b=(b); \
        if(_a&&_b&&strcmp(_a,_b)==0){printf("  [PASS] %s (\"%s\")\n",msg,_a);passed++;} \
        else{printf("  [FAIL] %s  got=\"%s\" expected=\"%s\"\n",msg,_a?_a:"(null)",_b?_b:"(null)");failed++;}}while(0)
#define CHECK_F(a, b, msg) \
    do{float _a=(float)(a),_b=(float)(b);\
       if(fabsf(_a-_b)<2.0f){printf("  [PASS] %s (%.1f)\n",msg,_a);passed++;}\
       else{printf("  [FAIL] %s  got=%.1f expected=%.1f\n",msg,_a,_b);failed++;}}while(0)

/* =========================================================================
 * Test: ui_load + ui_destroy
 * ========================================================================= */
static void test_load_destroy(void) {
    printf("\n=== ui_load / ui_destroy ===\n");

    HtmluiOptions opts;
    htmlui_options_default(&opts);
    opts.renderer = HTMLUI_RENDERER_SDL3; /* will fall back to soft */

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css",
                      &opts);
    CHECK(ui != NULL, "ui_load returns non-NULL");
    if (!ui) return;
    CHECK(ui_is_running(ui), "ui_is_running is true after load");
    CHECK(ui->dom_root != NULL, "dom_root is set");
    CHECK(ui->soft != NULL, "soft renderer created");
    CHECK(ui->display_list.count > 0, "display list populated on load");
    CHECK(ui->window_w == 800 && ui->window_h == 600, "window dimensions");

    ui_destroy(ui);
    printf("  [PASS] ui_destroy completed without crash\n");
    passed++;
}

/* =========================================================================
 * Test: ui_query
 * ========================================================================= */
static void test_query(void) {
    printf("\n=== ui_query / ui_query_all ===\n");

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css", NULL);
    if (!ui) { printf("  [SKIP] ui_load failed\n"); return; }

    Node* title = ui_query(ui, "#title");
    CHECK(title != NULL, "query #title finds node");
    if (title) CHECK_STR(node_tag(title), "h1", "  #title tag is h1");

    Node* btn = ui_query(ui, "#btn-greet");
    CHECK(btn != NULL, "query #btn-greet finds node");

    Node* missing = ui_query(ui, "#does-not-exist");
    CHECK(missing == NULL, "query for non-existent id returns NULL");

    /* Query by tag */
    Node* h1 = ui_query(ui, "h1");
    CHECK(h1 != NULL, "query by tag 'h1' finds node");

    /* Query by class */
    Node* card = ui_query(ui, ".card");
    CHECK(card != NULL, "query .card finds node");

    /* ui_query_all */
    int count = 0;
    Node** btns = ui_query_all(ui, ".btn", &count);
    CHECK(count >= 2, "ui_query_all .btn finds at least 2 buttons");
    free(btns);

    ui_destroy(ui);
}

/* =========================================================================
 * Test: mutations trigger dirty + re-render
 * ========================================================================= */
static void test_mutations(void) {
    printf("\n=== Mutations + Dirty Flagging ===\n");

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css", NULL);
    if (!ui) { printf("  [SKIP]\n"); return; }

    /* After load, not dirty */
    CHECK(!ui->dirty, "not dirty after initial load");

    /* node_set_text marks dirty */
    Node* title = ui_query(ui, "#title");
    if (title) {
        node_set_text(title, "New Title");
        CHECK(ui->dirty, "dirty after node_set_text");

        /* Re-render clears dirty */
        ui_render_frame(ui);
        CHECK(!ui->dirty, "not dirty after ui_render_frame");
    }

    /* node_set_style marks dirty */
    Node* btn = ui_query(ui, "#btn-greet");
    if (btn) {
        node_set_style(btn, "background-color", "#ff0000");
        CHECK(ui->dirty, "dirty after node_set_style");
        ui_render_frame(ui);
        CHECK(!ui->dirty, "clean after render");
    }

    /* Class operations */
    Node* card = ui_query(ui, ".card");
    if (card) {
        CHECK(!node_has_class(card, "active"), "card does not have 'active' initially");
        node_add_class(card, "active");
        CHECK(node_has_class(card, "active"),  "card has 'active' after add_class");
        node_remove_class(card, "active");
        CHECK(!node_has_class(card, "active"), "card loses 'active' after remove_class");
        node_toggle_class(card, "active");
        CHECK(node_has_class(card, "active"),  "toggle adds 'active'");
        node_toggle_class(card, "active");
        CHECK(!node_has_class(card, "active"), "toggle removes 'active'");
    }

    /* node_set_visible */
    Node* status = ui_query(ui, "#status");
    if (status) {
        node_set_visible(status, false);
        CHECK(!node_is_visible(status), "node_set_visible(false) hides node");
        ui_render_frame(ui);
        node_set_visible(status, true);
        CHECK(node_is_visible(status), "node_set_visible(true) shows node");
    }

    ui_destroy(ui);
}

/* =========================================================================
 * Test: node_get_rect returns layout positions
 * ========================================================================= */
static void test_node_rect(void) {
    printf("\n=== node_get_rect ===\n");

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css", NULL);
    if (!ui) { printf("  [SKIP]\n"); return; }

    float x, y, w, h;

    /* Root app div should start at (0,0) */
    Node* root_div = ui_query(ui, "#root");
    if (root_div) {
        node_get_rect(root_div, &x, &y, &w, &h);
        CHECK_F(x, 0,  "#root x=0");
        CHECK_F(y, 0,  "#root y=0");
        CHECK(w > 0,   "#root has positive width");
        CHECK(h > 0,   "#root has positive height");
        /* Content area starts at padding offset (32px) */
        CHECK_F(root_div->layout->content_x, 32, "#root content_x=32 (padding)");
    }

    /* Title should be inside root's content area */
    Node* title = ui_query(ui, "#title");
    if (title) {
        node_get_rect(title, &x, &y, &w, &h);
        CHECK(x >= 32, "#title x >= 32 (inside padding)");
        CHECK(y >= 32, "#title y >= 32 (inside padding)");
    }

    ui_destroy(ui);
}

/* =========================================================================
 * Test: event callbacks
 * ========================================================================= */

static int click_count  = 0;
static int hover_count  = 0;
static Node* last_clicked = NULL;

static void on_click(HtmluiEvent* ev) {
    click_count++;
    last_clicked = ev->target;
}

static void on_hover_cb(HtmluiEvent* ev) {
    hover_count += ev->key_code; /* +1 on enter, +0 on leave */
}

static void test_events(void) {
    printf("\n=== Event Callbacks ===\n");

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css", NULL);
    if (!ui) { printf("  [SKIP]\n"); return; }

    Node* btn = ui_query(ui, "#btn-greet");
    CHECK(btn != NULL, "btn-greet found for event test");
    if (!btn) { ui_destroy(ui); return; }

    node_on_click(btn, on_click, NULL);
    node_on_hover(btn, on_hover_cb, NULL);

    /* Get button rect for hit testing */
    float bx, by, bw, bh;
    node_get_rect(btn, &bx, &by, &bw, &bh);
    float cx = bx + bw / 2.0f;
    float cy = by + bh / 2.0f;

    /* Hit test should find the button */
    __html_node__* hit = event_hit_test(ui->dom_root, cx, cy);
    CHECK(hit == btn, "hit test at button center finds the button");

    /* Simulate click: move → down → up */
    click_count = 0;
    event_mouse_move(&ui->events, ui->dom_root, cx, cy);
    event_mouse_down(&ui->events, ui->dom_root, 1);
    event_mouse_up  (&ui->events, ui->dom_root, 1);
    CHECK(click_count == 1,     "on_click fires once after simulated click");
    CHECK(last_clicked == btn,  "on_click target is the button");

    /* Simulate hover enter */
    hover_count = 0;
    event_mouse_move(&ui->events, ui->dom_root, -999, -999); /* move away */
    int flags = event_mouse_move(&ui->events, ui->dom_root, cx, cy); /* enter */
    CHECK(hover_count == 1,          "on_hover fires on enter (key_code=1)");
    CHECK(flags & EV_DIRTY,          "hover enter sets EV_DIRTY");

    /* Miss click (button miss → count unchanged) */
    click_count = 0;
    event_mouse_move(&ui->events, ui->dom_root, -1, -1);
    event_mouse_down(&ui->events, ui->dom_root, 1);
    event_mouse_up  (&ui->events, ui->dom_root, 1);
    CHECK(click_count == 0, "click outside button does not fire callback");

    ui_destroy(ui);
}

/* =========================================================================
 * Test: render to PPM via API
 * ========================================================================= */
static void test_render_to_ppm(const char* out_path) {
    printf("\n=== Full API Render to PPM ===\n");

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css", NULL);
    if (!ui) { printf("  [SKIP]\n"); return; }

    /* Mutate: change title text */
    Node* title = ui_query(ui, "#title");
    if (title) node_set_text(title, "Milestone 5 Render!");

    /* Add a class */
    Node* btn = ui_query(ui, "#btn-greet");
    if (btn) node_add_class(btn, "active");

    /* Re-render */
    ui_render_frame(ui);

    /* Save via soft renderer */
    int r = soft_renderer_save_ppm(ui->soft, out_path);
    if (r == 0) {
        printf("  [PASS] Rendered and saved: %s\n", out_path);
        passed++;
    } else {
        printf("  [FAIL] Could not write %s\n", out_path);
        failed++;
    }

    ui_destroy(ui);
}

/* =========================================================================
 * Test: ui_reload
 * ========================================================================= */
static void test_reload(void) {
    printf("\n=== ui_reload ===\n");

    UI* ui = ui_load("examples/hello/views/main.html",
                      "examples/hello/styles/app.css", NULL);
    if (!ui) { printf("  [SKIP]\n"); return; }

    HtmluiResult r = ui_reload(ui);
    CHECK(r == HTMLUI_OK,          "ui_reload returns HTMLUI_OK");
    CHECK(ui->dom_root != NULL,    "dom_root valid after reload");
    CHECK(!ui->dirty,              "not dirty after reload");

    ui_destroy(ui);
}

/* =========================================================================
 * Entry point
 * ========================================================================= */
int main(int argc, char* argv[]) {
    printf("htmlui — Milestone 5: Full API Integration Test\n");
    printf("================================================\n");

    test_load_destroy();
    test_query();
    test_mutations();
    test_node_rect();
    test_events();

    const char* ppm = argc >= 2 ? argv[1] : "/tmp/milestone5.ppm";
    test_render_to_ppm(ppm);
    test_reload();

    printf("\n================================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("================================================\n");
    return failed > 0 ? 1 : 0;
}
