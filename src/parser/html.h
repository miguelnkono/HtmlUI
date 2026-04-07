/**
 * parser/html.h
 *
 * HTML parser interface.
 * Reads an HTML file and produces a DOM tree of __html_node__ structs.
 *
 * Implementation notes
 * --------------------
 *
 *   - Elements with attributes  (<div id="x" class="a b">)
 *   - Self-closing tags          (<input type="text" />  <br>)
 *   - Nested elements
 *   - Text nodes (stored as text_content on the parent)
 *   - HTML5 boolean attributes   (<button disabled>)
 *   - Comments (skipped)
 *   - DOCTYPE (skipped)
 *
 * Later can swap in Gumbo for full HTML5 compliance by
 * replacing html_parse_file() with a Gumbo wrapper, keeping the same
 * __html_node__* return type.
 */

#ifndef HTMLUI_PARSER_HTML_H
#define HTMLUI_PARSER_HTML_H

#include "../internal/types.h"

/**
 * Parse an HTML file and return the root DOM node.
 *
 * @param path        Path to the HTML file.
 * @param error_buf   Buffer to write a human-readable error message into.
 *                    May be NULL if you don't need the message.
 * @param error_len   Size of error_buf.
 * @return            Root __html_node__* (caller owns it — free with
 * htmlnode_free), or NULL on failure.
 */
__html_node__ *html_parse_file(const char *path, char *error_buf,
                               size_t error_len);

/**
 * Parse an HTML string in memory (useful for tests).
 *
 * @param html        Null-terminated HTML source string.
 * @param error_buf   Error buffer -  may be NULL.
 * @param error_len   Size of error_buf.
 * @return            Root __html_node__*, or NULL on failure.
 */
__html_node__ *html_parse_string(const char *html, char *error_buf,
                                 size_t error_len);

/**
 * Debug helper: print the DOM tree to stdout with indentation.
 * @param node   Root node to print from.
 * @param depth  Initial indent level (pass 0 from user code).
 */
void html_dump(const __html_node__ *node, int depth);

#endif /* HTMLUI_PARSER_HTML_H */
