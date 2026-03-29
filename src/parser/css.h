/**
 * parser/css.h
 *
 * CSS parser interface.
 * Reads a CSS file and produces a __css_rule_list__.
 *
 * Supported CSS subset (Milestone 1)
 * ------------------------------------
 *  - Tag selectors:            button { }
 *  - Class selectors:          .container { }
 *  - ID selectors:             #submit-btn { }
 *  - Descendant combinator:    .app button { }
 *  - Multiple selectors:       h1, h2 { }
 *  - Property declarations:    color: #fff;
 *  - Single-line comments:     (not standard CSS, skipped gracefully)
 *  - Block comments:           (block comments are stripped)
 *
 * Not yet supported (future milestones)
 * ----------------------------------------
 *  - Pseudo-classes:   :hover, :focus, :active  (Milestone 7)
 *  - Pseudo-elements:  ::before, ::after
 *  - @media queries
 *  - @keyframes / animations
 *  - CSS variables (--custom-prop)
 *  - calc()
 */

#ifndef HTMLUI_PARSER_CSS_H
#define HTMLUI_PARSER_CSS_H

#include "../internal/types.h"

/**
 * Parse a CSS file and append rules to list.
 *
 * @param path       Path to the CSS file.
 * @param list       Output __css_rule_list__ (must already be initialised with
 *                   css_rule_list_init). Rules are appended in source order.
 * @param error_buf  Optional error message buffer.
 * @param error_len  Size of error_buf.
 * @return           HTMLUI_OK on success, error code on failure.
 */
int css_parse_file(const char* path,
                   __css_rule_list__* list,
                   char*        error_buf,
                   size_t       error_len);

/**
 * Parse a CSS string in memory and append rules to list.
 * Useful for inline <style> blocks and unit tests.
 */
int css_parse_string(const char* css,
                     __css_rule_list__* list,
                     char*        error_buf,
                     size_t       error_len);

/**
 * Debug helper: print all rules in list to stdout.
 */
void css_dump(const __css_rule_list__* list);

#endif /* HTMLUI_PARSER_CSS_H */
