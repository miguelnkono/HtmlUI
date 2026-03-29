/**
 * src/style/cascade.h
 *
 * CSS cascade — style resolution interface.
 *
 * Takes the DOM tree and the CSS rule list produced by the parsers and
 * computes a fully-resolved __computed_style__ for every node in the tree.
 *
 * After this pass, every __html_node__ has a valid node->style pointer.
 * No CSS lookups are needed downstream — all values are absolute pixels.
 */

#ifndef HTMLUI_STYLE_CASCADE_H
#define HTMLUI_STYLE_CASCADE_H

#include "../internal/types.h"

/**
 * Run the CSS cascade over the entire DOM tree.
 *
 * For each node (depth-first):
 *   1. Collect all matching CSS rules from the rule list
 *   2. Sort by specificity, break ties by source order
 *   3. Apply declarations to build a __computed_style__
 *   4. Inherit unset inheritable properties from the parent
 *   5. Resolve relative values (em, %, keywords) to absolute px
 *
 * Allocates and assigns node->style for every non-text node.
 * Existing node->style pointers are freed and replaced.
 *
 * @param root      Root of the DOM tree.
 * @param rules     The CSS rule list from the parser.
 * @param viewport_w  Window width  in px — used to resolve % widths.
 * @param viewport_h  Window height in px — used to resolve % heights.
 */
void cascade_apply(__html_node__ *root, const __css_rule_list__ *rules,
                   float viewport_w, float viewport_h);

/**
 * Compute the CSS specificity score of a selector string.
 *
 * Returns a packed integer:  (id_count << 16) | (class_count << 8) | tag_count
 * Higher = more specific.
 *
 * Exposed for testing.
 */
int selector_specificity(const char *selector);

/**
 * Returns non-zero if selector matches node.
 *
 * Supported forms:
 *   tag           div, button, h1
 *   .class        .app, .btn-primary
 *   #id           #submit-btn
 *   tag.class     button.btn
 *   .a.b          multi-class
 *   A B           descendant (A is ancestor of B)
 *
 * Exposed for testing.
 */
int selector_matches(const char *selector, const __html_node__ *node);

/**
 * Debug: print the computed style of a node to stdout.
 */
void computed_style_dump(const __computed_style__ *s, const char *label);

#endif /* HTMLUI_STYLE_CASCADE_H */
