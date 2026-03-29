/**
 * src/paint/paint.h
 *
 * Display list builder — layout tree → DrawCommand list.
 *
 * Walks the layout tree in painter's order (back-to-front, respecting
 * z-index) and emits a flat list of DrawCommands into a __display_list__.
 *
 * The display list is the abstraction boundary between the layout system
 * and any rendering backend (software, SDL3, Vulkan). Neither layer needs
 * to know anything about the other.
 *
 * Command emission per node
 * -------------------------
 *  1. CMD_CLIP_PUSH    — if overflow:hidden
 *  2. CMD_RECT         — background fill + border + border-radius
 *  3. CMD_TEXT         — text content (text_content field of node or children)
 *  4. CMD_CLIP_POP     — if overflow:hidden was pushed
 *
 * Children are emitted recursively between steps 2 and 3.
 */

#ifndef HTMLUI_PAINT_H
#define HTMLUI_PAINT_H

#include "../internal/types.h"

/**
 * Walk the layout tree and populate dl with draw commands.
 * dl must have been initialised with display_list_init().
 * Clears dl before building.
 *
 * Preconditions:
 *   - cascade_apply() has been called (node->style valid)
 *   - layout_run()    has been called (node->layout valid)
 */
void paint_build(__html_node__* root, __display_list__* dl);

/**
 * Debug: print all commands in dl to stdout.
 */
void paint_dump(const __display_list__* dl);

#endif /* HTMLUI_PAINT_H */
