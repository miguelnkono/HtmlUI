/**
 * src/events/events.h
 *
 * Event system — hit testing and callback dispatch.
 *
 * Responsibilities
 * ----------------
 *  - Hit testing: given (mouse_x, mouse_y), find the deepest visible node
 *    under the cursor whose pointer-events is not none.
 *  - Hover tracking: detect enter/leave transitions between frames and
 *    fire node_on_hover callbacks.
 *  - Click dispatch: fire node_on_click when press+release land on the
 *    same node.
 *  - Keyboard dispatch: route key events to the focused node.
 *  - Dirty marking: any event that mutates node state marks the node dirty.
 *
 * This module is renderer-agnostic. The caller feeds raw input events
 * (from SDL3 or any other source) via the functions below.
 */

#ifndef HTMLUI_EVENTS_H
#define HTMLUI_EVENTS_H

#include "../internal/types.h"
#include "../../include/htmlui.h"

/* =========================================================================
 * Event dispatcher state
 *
 * One EventState exists per UI context. It tracks hover and press state
 * across frames so that enter/leave and click can be detected.
 * ========================================================================= */

typedef struct {
    __html_node__* hovered;       /* node currently under the cursor            */
    __html_node__* pressed;       /* node where the last mouse-down happened    */
    __html_node__* focused;       /* node with keyboard focus                   */
    float     mouse_x;
    float     mouse_y;
} EventState;

void event_state_init(EventState* es);

/* =========================================================================
 * Hit testing
 * ========================================================================= */

/**
 * Return the deepest visible, pointer-events-enabled node at (x, y).
 * Searches the tree depth-first (front-to-back).
 * Returns NULL if no node is under the point.
 */
__html_node__* event_hit_test(__html_node__* root, float x, float y);

/* =========================================================================
 * Input event injection
 *
 * Call these from your event loop (SDL3 or otherwise).
 * Each function may fire registered callbacks and marks dirty nodes.
 * Returns a bitmask of EventFlags (see below).
 * ========================================================================= */

typedef enum {
    EV_NONE       = 0,
    EV_DIRTY      = 1 << 0,   /* at least one node was dirtied           */
    EV_QUIT       = 1 << 1,   /* application should exit                 */
    EV_CONSUMED   = 1 << 2,   /* event was handled by a node             */
} EventFlags;

/**
 * Mouse moved to (x, y).
 * Updates hover state, fires on_hover callbacks for enter/leave.
 */
int event_mouse_move(EventState* es, __html_node__* root, float x, float y);

/**
 * Mouse button pressed at current cursor position.
 * button: 1=left, 2=middle, 3=right
 */
int event_mouse_down(EventState* es, __html_node__* root, int button);

/**
 * Mouse button released.
 * Fires on_click if release is on the same node as press.
 */
int event_mouse_up(EventState* es, __html_node__* root, int button);

/**
 * Key pressed. key_code is the SDL3 scancode (or any integer key id).
 * modifiers is a bitmask of modifier keys (SDL_Keymod or 0).
 * Dispatched to es->focused node if set.
 */
int event_key_down(EventState* es, int key_code, uint32_t modifiers);

/**
 * Key released.
 */
int event_key_up(EventState* es, int key_code, uint32_t modifiers);

/**
 * Signal that the application should quit (e.g. window close button).
 * Always returns EV_QUIT.
 */
int event_quit(EventState* es);

#endif /* HTMLUI_EVENTS_H */
