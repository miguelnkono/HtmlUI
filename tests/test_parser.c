#define _POSIX_C_SOURCE 200809L
/**
 * tests/test_parser.c
 *
 * Reads an HTML file and a CSS file, runs both parsers, and dumps the results to stdout.
 *
 * Usage:
 *   ./test_parser <html_file> [css_file]
 *
 * Expected output (for the bundled example):
 *   [HTML] Parsed successfully.
 *   <html>
 *     <body>
 *       <div class="app">
 *         <h1> "Hello, htmlui!" </h1>
 *         <button id="btn" class="btn-primary"> "Click me" </button>
 *       </div>
 *     </body>
 *   </html>
 *
 *   [CSS] Parsed 5 rules.
 *   [0] "body" (2 decls, order=0)
 *         margin: 0
 *         font-family: Arial, sans-serif
 *   ...
 */

#include <stdio.h>
#include <string.h>

#include "../src/internal/types.h"
#include "../src/parser/html.h"
#include "../src/parser/css.h"

/**
 * Simple pass/fail counter
 **/
static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg)                                      \
    do {                                                      \
        if (cond) {                                           \
            printf("  [PASS] %s\n", msg);                    \
            passed++;                                         \
        } else {                                              \
            printf("  [FAIL] %s\n", msg);                    \
            failed++;                                         \
        }                                                     \
    } while (0)

/**
 * HTML parser unit tests
 **/
static void test_html_parser(void) {
    printf("\n=== HTML Parser Tests ===\n");
    char errbuf[256];

    // basic elements
    {
        __html_node__* root = html_parse_string("<div></div>", errbuf, sizeof(errbuf));
        CHECK(root != NULL, "parse simple <div>");
        if (root) {
            /* The parser wraps in <html> if no html root found,
             * or returns the first element as root. */
            __html_node__* node = (strcmp(root->tag, "html") == 0 && root->child_count > 0)
                ? root->children[0] : root;
            CHECK(root->child_count == 2, "the children count should be 02");
            CHECK(strcmp(node->tag, "body") == 0, "tag should a 'body'");
            CHECK(strcmp(root->children[1]->tag, "div") == 0, "tag should a 'div'");
            htmlnode_free(root);
        }
    }

    // TODO: Should check for the wrapping of metatags like <meta>, <link>, etc.

    /* -- Attributes -- */
    {
        __html_node__* root = html_parse_string(
                "<button id=\"btn\" class=\"primary\">OK</button>",
                errbuf, sizeof(errbuf));
        CHECK(root != NULL, "parse button with attributes");
        if (root) {
            /* Find the button node (may be wrapped in html) */
            __html_node__* btn = root;
            if (strcmp(btn->tag, "html") == 0 && btn->child_count > 0)
                btn = btn->children[0];

            const char* id  = htmlattr_list_get(&btn->attrs, "id");
            const char* cls = htmlattr_list_get(&btn->attrs, "class");
            CHECK(id  && strcmp(id,  "btn")     == 0, "id attribute is 'btn'");
            CHECK(cls && strcmp(cls, "primary")  == 0, "class attribute is 'primary'");
            htmlnode_free(root);
        }
    }

    /* -- Nesting -- */
    {
        const char* html =
            "<div class=\"app\">"
            "  <h1>Hello</h1>"
            "  <p>World</p>"
            "</div>";
        __html_node__* root = html_parse_string(html, errbuf, sizeof(errbuf));
        CHECK(root != NULL, "parse nested elements");
        if (root) {
            __html_node__* div = root;
            if (strcmp(div->tag, "html") == 0 && div->child_count > 0)
                div = div->children[0];
            /* Should have at least 2 children (h1 and p; text nodes may vary) */
            int elem_children = 0;
            for (int i = 0; i < div->child_count; i++)
                if (strcmp(div->children[i]->tag, "#text") != 0)
                    elem_children++;
            CHECK(elem_children >= 2, "div has at least 2 element children");
            htmlnode_free(root);
        }
    }

    /* -- Self-closing / void tag -- */
    {
        __html_node__* root = html_parse_string(
                "<div><input type=\"text\" /><br></div>",
                errbuf, sizeof(errbuf));
        CHECK(root != NULL, "parse void/self-closing tags");
        if (root) htmlnode_free(root);
    }

    /* -- Comment skipping -- */
    {
        __html_node__* root = html_parse_string(
                "<!-- comment --><div></div>",
                errbuf, sizeof(errbuf));
        CHECK(root != NULL, "HTML comment skipped, div still parsed");
        if (root) htmlnode_free(root);
    }

    /* -- Realistic UI fragment -- */
    {
        const char* ui =
            "<!DOCTYPE html>"
            "<html>"
            "<body>"
            "  <div class=\"app\" id=\"root\">"
            "    <h1 id=\"title\">Hello, htmlui!</h1>"
            "    <button id=\"btn\" class=\"btn-primary\">Click me</button>"
            "    <input type=\"text\" placeholder=\"Enter text...\" />"
            "  </div>"
            "</body>"
            "</html>";
        __html_node__* root = html_parse_string(ui, errbuf, sizeof(errbuf));
        CHECK(root != NULL, "full UI HTML fragment parsed");
        if (root) {
            printf("\n  [DOM Dump]\n");
            html_dump(root, 2);
            htmlnode_free(root);
        }
    }
}

/* =========================================================================
 * CSS parser unit tests
 * ========================================================================= */

static void test_css_parser(void) {
    printf("\n=== CSS Parser Tests ===\n");
    char errbuf[256];

    /* -- Basic rule -- */
    {
        __css_rule_list__ list;
        css_rule_list_init(&list);
        int r = css_parse_string("div { color: red; }", &list, errbuf, sizeof(errbuf));
        CHECK(r == 0,          "css_parse_string returns 0");
        CHECK(list.count == 1, "1 rule parsed");
        if (list.count > 0) {
            CHECK(strcmp(list.rules[0].selector, "div") == 0, "selector is 'div'");
            CHECK(list.rules[0].decl_count == 1,              "1 declaration");
            CHECK(strcmp(list.rules[0].declarations[0].property, "color") == 0,
                    "property is 'color'");
            CHECK(strcmp(list.rules[0].declarations[0].value, "red") == 0,
                    "value is 'red'");
        }
        css_rule_list_free(&list);
    }

    /* -- Multiple declarations -- */
    {
        __css_rule_list__ list;
        css_rule_list_init(&list);
        css_parse_string(
                ".btn { display: flex; background-color: #1d4ed8; color: #fff; }",
                &list, errbuf, sizeof(errbuf));
        CHECK(list.count == 1,          "1 rule for .btn");
        CHECK(list.rules[0].decl_count == 3, ".btn has 3 declarations");
        css_rule_list_free(&list);
    }

    /* -- Multiple rules -- */
    {
        __css_rule_list__ list;
        css_rule_list_init(&list);
        css_parse_string(
                "body { margin: 0; }"
                "h1   { font-size: 24px; color: #111; }"
                ".app { display: flex; flex-direction: column; }",
                &list, errbuf, sizeof(errbuf));
        CHECK(list.count == 3, "3 rules parsed");
        css_rule_list_free(&list);
    }

    /* -- Comma-separated selectors -- */
    {
        __css_rule_list__ list;
        css_rule_list_init(&list);
        css_parse_string("h1, h2, h3 { font-weight: bold; }",
                &list, errbuf, sizeof(errbuf));
        CHECK(list.count == 3, "comma selector expands to 3 rules");
        if (list.count == 3) {
            CHECK(strcmp(list.rules[0].selector, "h1") == 0, "first selector is h1");
            CHECK(strcmp(list.rules[1].selector, "h2") == 0, "second selector is h2");
            CHECK(strcmp(list.rules[2].selector, "h3") == 0, "third selector is h3");
        }
        css_rule_list_free(&list);
    }

    /* -- Block comments -- */
    {
        __css_rule_list__ list;
        css_rule_list_init(&list);
        css_parse_string(
                "/* This is a comment */\n"
                "div { color: blue; /* inline comment */ font-size: 16px; }",
                &list, errbuf, sizeof(errbuf));
        CHECK(list.count == 1, "comments ignored, 1 rule parsed");
        css_rule_list_free(&list);
    }

    /* -- Realistic stylesheet -- */
    {
        __css_rule_list__ list;
        css_rule_list_init(&list);
        int r = css_parse_string(
                "* { box-sizing: border-box; margin: 0; padding: 0; }\n"
                "body { font-family: Arial, sans-serif; background-color: #0f1117; color: #e2e8f0; }\n"
                ".app { display: flex; flex-direction: column; padding: 24px; gap: 16px; }\n"
                "#title { font-size: 32px; font-weight: 700; color: #ffffff; }\n"
                ".btn-primary { display: flex; background-color: #1d4ed8; color: #fff;\n"
                "               padding: 8px 16px; border-radius: 6px; cursor: pointer; }\n"
                ".btn-primary:hover { background-color: #2563eb; }\n",
                &list, errbuf, sizeof(errbuf));
        CHECK(r == 0, "realistic stylesheet parsed without error");
        CHECK(list.count >= 5, "at least 5 rules in realistic stylesheet");
        printf("\n  [CSS Dump]\n");
        /* Indent the dump output */
        css_dump(&list);
        css_rule_list_free(&list);
    }
}

/* =========================================================================
 * File-based test (uses the bundled example files)
 * ========================================================================= */

static void test_from_files(const char* html_path, const char* css_path) {
    printf("\n=== File-based Test ===\n");
    char errbuf[512];

    if (html_path) {
        printf("  Parsing HTML: %s\n", html_path);
        __html_node__* root = html_parse_file(html_path, errbuf, sizeof(errbuf));
        if (root) {
            printf("  [PASS] HTML file parsed.\n");
            printf("\n  [DOM Dump]\n");
            html_dump(root, 2);
            htmlnode_free(root);
            passed++;
        } else {
            printf("  [FAIL] HTML parse error: %s\n", errbuf);
            failed++;
        }
    }

    if (css_path) {
        printf("\n  Parsing CSS: %s\n", css_path);
        __css_rule_list__ list;
        css_rule_list_init(&list);
        int r = css_parse_file(css_path, &list, errbuf, sizeof(errbuf));
        if (r == 0) {
            printf("  [PASS] CSS file parsed (%d rules).\n", list.count);
            printf("\n  [CSS Dump]\n");
            css_dump(&list);
        } else {
            printf("  [FAIL] CSS parse error: %s\n", errbuf);
            failed++;
        }
        css_rule_list_free(&list);
    }
}

/**
 * Entry point
 **/
int main(int argc, char* argv[]) {
    printf("htmlui — Milestone 1: Parser Test Harness\n");
    printf("==========================================\n");

    /* Run unit tests */
    test_html_parser();
    test_css_parser();

    /* Run file-based test if paths were given */
    if (argc >= 2)
        test_from_files(argv[1], argc >= 3 ? argv[2] : NULL);

    /* Summary */
    printf("\n==========================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("==========================================\n");

    return failed > 0 ? 1 : 0;
}
