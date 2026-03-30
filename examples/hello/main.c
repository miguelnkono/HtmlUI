#include "../../include/htmlui.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  UI *ui;
  Node *status_label;
  Node *first_number;
  Node *second_number;
  Node *result;
  int click_count;
} AppState;

static void on_first_number_click(HtmluiEvent *ev) {
  AppState *app = (AppState *)ev->user_data;

  /* Read name from input (if set via node_set_attr) */
  const char *f_num = node_get_attr(app->result, "value");
  if (!f_num)
    return;

  printf("first number: %s\n", f_num);
  int f_num_parse = atoi(f_num);
  printf("first number: %d\n", f_num_parse);

  node_add_class(app->first_number, "active");
}

static void on_second_number_click(HtmluiEvent *ev) {
  AppState *app = (AppState *)ev->user_data;
  app->click_count = 0;
  node_set_text(app->status_label, "");
  node_remove_class(app->first_number, "active");
  node_set_attr(app->result, "value", "");
}

int main(void) {
  AppState app = {0};

  /* Configure and load */
  HtmluiOptions opts;
  htmlui_options_default(&opts);
  opts.window_title = "Calculator App";
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
  app.first_number = ui_query(app.ui, "#first-number");
  app.second_number = ui_query(app.ui, "#second-number");
  app.result = ui_query(app.ui, "#result");

  if (!app.first_number || !app.second_number) {
    fprintf(stderr, "Required nodes not found\n");
    ui_destroy(app.ui);
    return 1;
  }

  /* Bind callbacks */
  node_on_click(app.first_number, on_first_number_click, &app);
  node_on_click(app.second_number, on_second_number_click, &app);

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
