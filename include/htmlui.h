/**
 * htmlui.h — Public API for the htmlui library
 *
 * A native desktop UI toolkit that parses HTML/CSS and renders to a
 * native window via SDL3 or Vulkan. No browser engine is embedded.
 * Application logic is written in C and interacts with the UI tree
 * through this API.
 *
 * Typical usage:
 *
 *   UI* ui = ui_load("views/main.html", "styles/app.css", NULL);
 *
 *   Node* btn = ui_query(ui, "#submit-btn");
 *   node_on_click(btn, on_click_handler, NULL);
 *
 *   while (ui_is_running(ui)) {
 *       ui_poll_events(ui);
 *       ui_render_frame(ui);
 *   }
 *
 *   ui_destroy(ui);
 */

#ifndef HTMLUI_H
#define HTMLUI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

    /* =========================================================================
     * Version
     * ========================================================================= */

#define HTMLUI_VERSION_MAJOR 0
#define HTMLUI_VERSION_MINOR 1
#define HTMLUI_VERSION_PATCH 0
#define HTMLUI_VERSION_STRING "0.1.0"

    /* =========================================================================
     * Opaque handle types
     *
     * Internal layout of UI and Node is private.
     * All interaction goes through the functions below.
     * ========================================================================= */

    typedef struct UI_t UI;
    typedef struct html_node_t Node;

    /* =========================================================================
     * Result / error codes
     * ========================================================================= */

    typedef enum
    {
        HTMLUI_OK = 0,
        HTMLUI_ERR_IO = -1,          /* File not found or unreadable        */
        HTMLUI_ERR_PARSE_HTML = -2,  /* HTML could not be parsed            */
        HTMLUI_ERR_PARSE_CSS = -3,   /* CSS could not be parsed             */
        HTMLUI_ERR_LAYOUT = -4,      /* Layout computation failed           */
        HTMLUI_ERR_RENDER_INIT = -5, /* Renderer (SDL3/Vulkan) init failed  */
        HTMLUI_ERR_NULL = -6,        /* NULL pointer passed where forbidden */
        HTMLUI_ERR_NOT_FOUND = -7,   /* Selector matched no node            */
        HTMLUI_ERR_OOM = -8,         /* Out of memory                       */
    } HtmluiResult;

    /* Return a human-readable string for a result code. */
    const char *htmlui_result_str(HtmluiResult result);

    /* =========================================================================
     * Renderer backend
     * ========================================================================= */

    typedef enum
    {
        HTMLUI_RENDERER_SDL3 = 0,   /* Default — simpler, cross-platform   */
        HTMLUI_RENDERER_VULKAN = 1, /* Advanced — full GPU pipeline        */
    } HtmluiRenderer;

    /* =========================================================================
     * Initialisation options
     * ========================================================================= */

    typedef struct
    {
        const char *window_title; /* Window title bar text               */
        int window_width;         /* Initial window width  (px)          */
        int window_height;        /* Initial window height (px)          */
        bool resizable;           /* Allow the window to be resized      */
        HtmluiRenderer renderer;  /* Which rendering backend to use      */
        bool vsync;               /* Enable vertical sync                */
        bool debug_layout;        /* Draw layout box outlines (dev mode) */
        bool debug_hover;         /* Highlight hovered node  (dev mode)  */
    } HtmluiOptions;

    /* Fill opts with safe default values. Always call this before customising. */
    void htmlui_options_default(HtmluiOptions *opts);

    /* =========================================================================
     * Lifecycle
     * ========================================================================= */

    /**
     * Load a UI from an HTML file and an optional CSS file.
     * Parses, resolves styles, runs layout, and opens the native window.
     *
     * @param html_path  Path to the HTML file (required).
     * @param css_path   Path to the CSS file. May be NULL.
     * @param opts       Initialisation options. May be NULL (uses defaults).
     * @return           A new UI handle, or NULL on failure.
     *                   Call ui_last_error() for the reason.
     */
    UI *ui_load(const char *html_path,
                const char *css_path,
                const HtmluiOptions *opts);

    /**
     * Reload the HTML and CSS files without destroying the window.
     * Useful for hot-reloading during development.
     */
    HtmluiResult ui_reload(UI *ui);

    /**
     * Returns true while the window is open and the app has not quit.
     * Use as the condition of your main loop.
     */
    bool ui_is_running(UI *ui);

    /**
     * Destroy the UI handle and free all resources.
     * Safe to call with NULL.
     */
    void ui_destroy(UI *ui);

    /** Return a human-readable description of the last error. Do not free. */
    const char *ui_last_error(UI *ui);

    /* =========================================================================
     * Querying the node tree
     * ========================================================================= */

    /**
     * Find the first node matching a CSS selector.
     *
     * Supported selector forms (Milestone 1 subset):
     *   #id          — match by id attribute
     *   .classname   — match by CSS class
     *   tag          — match by tag name  (div, button, h1, …)
     *
     * @return  Matching Node, or NULL if no match.
     */
    Node *ui_query(UI *ui, const char *selector);

    /**
     * Find all nodes matching a CSS selector.
     *
     * @param out_count  Set to the number of matched nodes.
     * @return           Heap-allocated array of Node pointers. Caller must
     *                   free() the array itself (but NOT the Node pointers).
     *                   Returns NULL and sets *out_count = 0 on no match.
     */
    Node **ui_query_all(UI *ui, const char *selector, int *out_count);

    /** Return the parent node, or NULL for the root. */
    Node *node_parent(Node *node);

    /** Return child at index i, or NULL if out of range. */
    Node *node_child(Node *node, int i);

    /** Return the number of direct children. */
    int node_child_count(Node *node);

    /** Return the tag name string. Do not free. */
    const char *node_tag(Node *node);

    /** Return the value of an attribute, or NULL if absent. Do not free. */
    const char *node_get_attr(Node *node, const char *attr);

    /* =========================================================================
     * Mutating the node tree
     *
     * All mutations mark the affected subtree dirty. The next
     * ui_render_frame() call will re-layout and repaint dirty regions only.
     * ========================================================================= */

    /** Replace the text content of a node (the text inside a <p>, <h1>, etc.). */
    void node_set_text(Node *node, const char *text);

    /** Set or update an attribute value. */
    void node_set_attr(Node *node, const char *attr, const char *value);

    /** Set a single inline CSS property, e.g. node_set_style(n, "color", "#fff"). */
    void node_set_style(Node *node, const char *property, const char *value);

    /** Add a CSS class. No-op if already present. */
    void node_add_class(Node *node, const char *class_name);

    /** Remove a CSS class. No-op if not present. */
    void node_remove_class(Node *node, const char *class_name);

    /** Add if absent, remove if present. */
    void node_toggle_class(Node *node, const char *class_name);

    /** Returns true if the node has the given class. */
    bool node_has_class(Node *node, const char *class_name);

    /** Show or hide a node (sets/restores display property). */
    void node_set_visible(Node *node, bool visible);

    /** Returns whether the node is currently visible. */
    bool node_is_visible(Node *node);

    /* =========================================================================
     * Events
     * ========================================================================= */

    typedef struct
    {
        Node *target;       /* Node the event fired on              */
        float mouse_x;      /* Cursor X relative to window          */
        float mouse_y;      /* Cursor Y relative to window          */
        int key_code;       /* SDL3 scancode  (keyboard events)     */
        uint32_t modifiers; /* SDL_Keymod bitmask                   */
        void *user_data;    /* The pointer you passed at register   */
    } HtmluiEvent;

    typedef void (*HtmluiEventCb)(HtmluiEvent *event);

    /** Mouse click (press + release on same node). Pass NULL to unregister. */
    void node_on_click(Node *node, HtmluiEventCb cb, void *user_data);

    /** Mouse button pressed down. */
    void node_on_mousedown(Node *node, HtmluiEventCb cb, void *user_data);

    /** Mouse button released. */
    void node_on_mouseup(Node *node, HtmluiEventCb cb, void *user_data);

    /**
     * Hover enter / leave.
     * event->key_code == 1  → cursor entered the node
     * event->key_code == 0  → cursor left the node
     */
    void node_on_hover(Node *node, HtmluiEventCb cb, void *user_data);

    /** Key pressed while this node has focus. */
    void node_on_keydown(Node *node, HtmluiEventCb cb, void *user_data);

    /** Key released while this node has focus. */
    void node_on_keyup(Node *node, HtmluiEventCb cb, void *user_data);

    /**
     * Value changed on an <input> element.
     * Read new value with node_get_attr(node, "value").
     */
    void node_on_change(Node *node, HtmluiEventCb cb, void *user_data);

    /** Give keyboard focus to a node. */
    void node_focus(Node *node);

    /** Remove focus from whatever node currently holds it. */
    void ui_blur(UI *ui);

    /* =========================================================================
     * Render loop
     * ========================================================================= */

    /**
     * Pump the SDL3 event queue and dispatch pending events to callbacks.
     * Call once per frame, before ui_render_frame().
     */
    void ui_poll_events(UI *ui);

    /**
     * Re-layout dirty nodes and repaint the window.
     * If nothing is dirty this is a no-op (no GPU work).
     * Call once per frame, after ui_poll_events().
     */
    void ui_render_frame(UI *ui);

    /**
     * Force a full re-layout and repaint regardless of dirty state.
     * Use after window resize or explicit invalidation.
     */
    void ui_invalidate(UI *ui);

    /* =========================================================================
     * Utility
     * ========================================================================= */

    /** Current window width in pixels. */
    int ui_window_width(UI *ui);

    /** Current window height in pixels. */
    int ui_window_height(UI *ui);

    /**
     * Get the computed layout rectangle for a node, in window-space pixels.
     * Any out_ parameter may be NULL to skip it.
     */
    void node_get_rect(Node *node,
                       float *out_x, float *out_y,
                       float *out_w, float *out_h);

#ifdef __cplusplus
}
#endif

#endif /* HTMLUI_H */
