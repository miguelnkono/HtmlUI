/**
 * examples/calculator/main.c
 *
 * Calculator — htmlui example application.
 *
 * The user enters two numbers, selects an operator (+, -, x, /),
 * then clicks Calculate to see the result.
 *
 * Demonstrates:
 *  - Input fields (value read via node_get_attr "value")
 *  - Operator selection via toggle classes
 *  - Result display via node_set_text
 *  - Error handling (division by zero, empty / non-numeric input)
 */

#include "../../include/htmlui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * App state
 **/
typedef enum {
  OP_NONE = 0,
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
} Operator;

typedef struct {
  UI *ui;

  /* Input fields */
  Node *input_a;
  Node *input_b;

  /* Operator buttons */
  Node *btn_add;
  Node *btn_sub;
  Node *btn_mul;
  Node *btn_div;

  /* Calculate button */
  Node *btn_calc;

  /* Display nodes */
  Node *op_label;
  Node *result;
  Node *status;

  /* Logic state */
  Operator selected_op;
} AppState;

/*
 * Helpers
 **/
static const char *op_symbol(Operator op) {
  switch (op) {
  case OP_ADD:
    return "+";
  case OP_SUB:
    return "-";
  case OP_MUL:
    return "×";
  case OP_DIV:
    return "÷";
  default:
    return "?";
  }
}

/* Deactivate all operator buttons, activate the chosen one, update label. */
static void select_operator(AppState *app, Operator op) {
  app->selected_op = op;

  node_remove_class(app->btn_add, "active");
  node_remove_class(app->btn_sub, "active");
  node_remove_class(app->btn_mul, "active");
  node_remove_class(app->btn_div, "active");

  Node *active_btn = NULL;
  switch (op) {
  case OP_ADD:
    active_btn = app->btn_add;
    break;
  case OP_SUB:
    active_btn = app->btn_sub;
    break;
  case OP_MUL:
    active_btn = app->btn_mul;
    break;
  case OP_DIV:
    active_btn = app->btn_div;
    break;
  default:
    break;
  }
  if (active_btn)
    node_add_class(active_btn, "active");

  char buf[64];
  snprintf(buf, sizeof(buf), "Operator: %s", op_symbol(op));
  node_set_text(app->op_label, buf);
  node_set_text(app->status, "");
}

/*
 * Operator callbacks
 **/
static void on_add(HtmluiEvent *ev) { select_operator(ev->user_data, OP_ADD); }
static void on_sub(HtmluiEvent *ev) { select_operator(ev->user_data, OP_SUB); }
static void on_mul(HtmluiEvent *ev) { select_operator(ev->user_data, OP_MUL); }
static void on_div(HtmluiEvent *ev) { select_operator(ev->user_data, OP_DIV); }

/*
 * Calculate callback
 **/
static void on_calculate(HtmluiEvent *ev) {
  AppState *app = (AppState *)ev->user_data;

  /* Clear previous status */
  node_set_text(app->status, "");

  /* Validate operator */
  if (app->selected_op == OP_NONE) {
    node_set_text(app->status, "Please select an operator first.");
    return;
  }

  /* Read input values */
  const char *str_a = node_get_attr(app->input_a, "value");
  const char *str_b = node_get_attr(app->input_b, "value");

  if (!str_a || strlen(str_a) == 0) {
    node_set_text(app->status, "First number is empty.");
    return;
  }
  if (!str_b || strlen(str_b) == 0) {
    node_set_text(app->status, "Second number is empty.");
    return;
  }

  /* Parse — accept integers and decimals */
  char *end_a = NULL, *end_b = NULL;
  double a = strtod(str_a, &end_a);
  double b = strtod(str_b, &end_b);

  if (!end_a || *end_a != '\0') {
    node_set_text(app->status, "First value is not a valid number.");
    return;
  }
  if (!end_b || *end_b != '\0') {
    node_set_text(app->status, "Second value is not a valid number.");
    return;
  }

  /* Division by zero */
  if (app->selected_op == OP_DIV && b == 0.0) {
    node_set_text(app->result, "Error");
    node_set_text(app->status, "Cannot divide by zero.");
    return;
  }

  /* Compute */
  double result = 0.0;
  switch (app->selected_op) {
  case OP_ADD:
    result = a + b;
    break;
  case OP_SUB:
    result = a - b;
    break;
  case OP_MUL:
    result = a * b;
    break;
  case OP_DIV:
    result = a / b;
    break;
  default:
    break;
  }

  /* Format result — show integer form when there's no fractional part */
  char buf[128];
  if (result == floor(result) && fabs(result) < 1e12) {
    snprintf(buf, sizeof(buf), "%.0f", result);
  } else {
    snprintf(buf, sizeof(buf), "%.6g", result);
  }

  node_set_text(app->result, buf);

  /* Log to terminal */
  printf("  %s %s %s = %s\n", str_a, op_symbol(app->selected_op), str_b, buf);
}

/*
 * Main
 **/
int main(void) {
  AppState app = {0};

  HtmluiOptions opts;
  htmlui_options_default(&opts);
  opts.window_title = "Calculator";
  opts.window_width = 600;
  opts.window_height = 520;
  opts.resizable = true;
  opts.vsync = true;

  app.ui = ui_load("views/main.html", "styles/app.css", &opts);
  if (!app.ui) {
    fprintf(stderr, "ui_load failed\n");
    return 1;
  }

  /* Query nodes */
  /* app.input_a = ui_query(app.ui, "#input-a"); */
  app.input_b = ui_query(app.ui, "#input-b");
  app.btn_add = ui_query(app.ui, "#btn-add");
  app.btn_sub = ui_query(app.ui, "#btn-sub");
  app.btn_mul = ui_query(app.ui, "#btn-mul");
  app.btn_div = ui_query(app.ui, "#btn-div");
  app.btn_calc = ui_query(app.ui, "#btn-calc");
  app.op_label = ui_query(app.ui, "#op-label");
  app.result = ui_query(app.ui, "#result");
  app.status = ui_query(app.ui, "#status");

  /* Validate required nodes */
  const char *required_ids[] = {"#input-a", "#input-b", "#btn-add",  "#btn-sub",
                                "#btn-mul", "#btn-div", "#btn-calc", "#result"};
  Node **required_nodes[] = {&app.input_a,  &app.input_b, &app.btn_add,
                             &app.btn_sub,  &app.btn_mul, &app.btn_div,
                             &app.btn_calc, &app.result};
  int ok = 1;
  for (int i = 0; i < 8; i++) {
    if (!*required_nodes[i]) {
      fprintf(stderr, "Node not found: %s\n", required_ids[i]);
      ok = 0;
    }
  }
  if (!ok) {
    ui_destroy(app.ui);
    return 1;
  }

  /* Bind operator callbacks */
  node_on_click(app.btn_add, on_add, &app);
  node_on_click(app.btn_sub, on_sub, &app);
  node_on_click(app.btn_mul, on_mul, &app);
  node_on_click(app.btn_div, on_div, &app);

  /* Bind calculate */
  node_on_click(app.btn_calc, on_calculate, &app);

  printf("Calculator running. Close window or press ESC to quit.\n");

  /* Main loop */
  while (ui_is_running(app.ui)) {
    ui_poll_events(app.ui);
    ui_render_frame(app.ui);
  }

  ui_destroy(app.ui);
  return 0;
}
