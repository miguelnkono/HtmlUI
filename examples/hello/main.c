/**
 * examples/hello/main.c
 *
 * Hello — example application using the htmlui public C API.
 *
 * Demonstrates:
 *  - Loading a UI from HTML + CSS
 *  - Querying nodes by selector
 *  - Binding click and hover callbacks
 *  - Mutating the UI from native code
 *  - The main render loop
 *
 * Build (requires SDL3):
 *   cmake -DHTMLUI_ENABLE_SDL3=ON -DHTMLUI_BUILD_EXAMPLES=ON ..
 *   cmake --build .
 *   ./hello_app
 *
 * Build (software renderer, no SDL3):
 *   gcc -DHTMLUI_FREETYPE ... -o hello_soft examples/hello/main.c ...
 *   ./hello_soft  (saves output.ppm)
 */

#include "../../include/htmlui.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  UI *ui;
  Node *status_label;
  Node *btn_greet;
  Node *btn_reset;
  Node *name_input;
  int click_count;
} AppState;

static void on_greet_click(HtmluiEvent *ev) {
  AppState *app = (AppState *)ev->user_data;
  app->click_count++;

  /* Read name from input (if set via node_set_attr) */
  const char *name = node_get_attr(app->name_input, "value");

  char msg[256];
  if (name && strlen(name) > 0)
    snprintf(msg, sizeof(msg), "Hello, %s! (click #%d)", name,
             app->click_count);
  else
    snprintf(msg, sizeof(msg), "Hello! (click #%d)", app->click_count);

  node_set_text(app->status_label, msg);
  node_add_class(app->btn_greet, "active");
}

static void on_reset_click(HtmluiEvent *ev) {
  AppState *app = (AppState *)ev->user_data;
  app->click_count = 0;
  node_set_text(app->status_label, "");
  node_remove_class(app->btn_greet, "active");
  node_set_attr(app->name_input, "value", "");
}

static void on_btn_hover(HtmluiEvent *ev) {
  if (ev->key_code == 1)
    node_add_class(ev->target, "hovered");
  else
    node_remove_class(ev->target, "hovered");
}

int main(void) {
  AppState app = {0};

  /* Configure and load */
  HtmluiOptions opts;
  htmlui_options_default(&opts);
  opts.window_title = "Hello World";
  opts.window_width = 800;
  opts.window_height = 600;
  opts.resizable = true;
  opts.vsync = true;

  app.ui = ui_load("views/main.html", "styles/app.css", &opts);
  if (!app.ui) {
    fprintf(stderr, "ui_load failed\n");
    return 1;
  }

  /* Query nodes */
  app.status_label = ui_query(app.ui, "#status");
  app.btn_greet = ui_query(app.ui, "#btn-greet");
  app.btn_reset = ui_query(app.ui, "#btn-reset");
  app.name_input = ui_query(app.ui, "#name-input");

  if (!app.btn_greet || !app.btn_reset) {
    fprintf(stderr, "Required nodes not found\n");
    ui_destroy(app.ui);
    return 1;
  }

  /* Bind callbacks */
  node_on_click(app.btn_greet, on_greet_click, &app);
  node_on_click(app.btn_reset, on_reset_click, &app);
  node_on_hover(app.btn_greet, on_btn_hover, NULL);
  node_on_hover(app.btn_reset, on_btn_hover, NULL);

  /* Initial status */
  if (app.status_label)
    node_set_text(app.status_label, "Click 'Say Hello' to get started.");

  printf("htmlui hello app running. Close window or press ESC to quit.\n");

  /* Main loop */
  while (ui_is_running(app.ui)) {
    ui_poll_events(app.ui);
    ui_render_frame(app.ui);
  }

  /* Cleanup */
  ui_destroy(app.ui);
  printf("Goodbye. Total clicks: %d\n", app.click_count);
  return 0;
}
