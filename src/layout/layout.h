/**
 * src/layout/layout.h
 *
 * Layout engine interface.
 *
 * Takes the DOM tree (with __computed_style__ on every node from the cascade)
 * and computes pixel-exact position and size for every node.
 * Populates the LayoutBox field on each __html_node__.
 *
 * Supported layout modes
 * ----------------------
 *  DISPLAY_BLOCK       — children stack vertically, full parent width
 *  DISPLAY_FLEX        — row or column flexbox with justify/align/gap/grow
 *  DISPLAY_INLINE      — treated as inline-block for now (sized by content)
 *  DISPLAY_INLINE_BLOCK— same as inline, laid out in parent flow
 *  DISPLAY_NONE        — node and children take zero space, not visited
 *
 * Coordinate system
 * -----------------
 *  Origin is top-left of the window.
 *  x increases rightward, y increases downward.
 *  All values are in logical pixels (no DPI scaling at this layer).
 *
 * LayoutBox fields
 * ----------------
 *  x, y            — position of the border-box top-left corner
 *  width, height   — outer size (border + padding + content)
 *  content_x/y     — top-left of the content area (inside padding+border)
 *  content_w/h     — size of the content area
 */

#ifndef HTMLUI_LAYOUT_H
#define HTMLUI_LAYOUT_H

#include "../internal/types.h"

/**
 * Run the layout pass over the entire DOM tree.
 *
 * Precondition: cascade_apply() must have been called first so that
 * every node has a valid node->style pointer.
 *
 * @param root        Root of the DOM tree.
 * @param viewport_w  Available width  in pixels (window content width).
 * @param viewport_h  Available height in pixels (window content height).
 */
void layout_run(__html_node__ *root, float viewport_w, float viewport_h);

/**
 * Debug: print the layout box of a node and all its descendants.
 * @param node   Root of the subtree to print.
 * @param depth  Initial indent (pass 0).
 */
void layout_dump(const __html_node__ *node, int depth);

#endif /* HTMLUI_LAYOUT_H */
