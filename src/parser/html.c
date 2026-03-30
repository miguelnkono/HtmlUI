#define _POSIX_C_SOURCE 200809L
/**
 * parser/html.c
 *
 * Hand-written recursive descent HTML parser.
 *
 * Handles the subset of HTML used in UI definitions.
 * Designed to be replaced by a Gumbo wrapper later if needed.
 *
 * Parsing strategy
 * ----------------
 * We use a single-pass character cursor that walks the source string.
 * The parser is intentionally lenient — it skips unknown constructs
 * rather than failing hard, because UI HTML files are developer-authored
 * and tend to be clean.
 *
 *  parse_document()
 *    └─ parse_node()          ← recursively builds the tree
 *         ├─ parse_tag()      ← reads tag name + attributes
 *         ├─ parse_text()     ← collects text until next '<'
 *         └─ (recurse for children until matching close tag)
 */

#include "html.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *src; /* Full source string             */
  size_t len;      /* Length of src                  */
  size_t pos;      /* Current read cursor            */
  int line;        /* Current line number (for errors) */
  char *error;     /* Error buffer (may be NULL)     */
  size_t errlen;
} Parser;

static char peek(Parser *p) {
  if (p->pos >= p->len)
    return '\0';
  return p->src[p->pos];
}

static char peek2(Parser *p) {
  if (p->pos + 1 >= p->len)
    return '\0';
  return p->src[p->pos + 1];
}

static char advance(Parser *p) {
  char c = peek(p);
  if (c == '\n')
    p->line++;
  p->pos++;
  return c;
}

static bool at_end(Parser *p) { return p->pos >= p->len; }

/* Skip whitespace characters. */
static void skip_whitespace(Parser *p) {
  while (!at_end(p) && isspace((unsigned char)peek(p)))
    advance(p);
}

/* Return true if the next bytes match str (does not advance). */
static bool match_str(Parser *p, const char *str) {
  size_t slen = strlen(str);
  if (p->pos + slen > p->len)
    return false;
  return memcmp(p->src + p->pos, str, slen) == 0;
}

/* Read characters into a heap buffer until stop_ch is found.
 * The stop character is NOT consumed. Caller must free the result. */
static char *read_until_char(Parser *p, char stop_ch) {
  size_t start = p->pos;
  while (!at_end(p) && peek(p) != stop_ch)
    advance(p);
  size_t len = p->pos - start;
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, p->src + start, len);
  out[len] = '\0';
  return out;
}

/* Same but stops at any character in the stop string. */
static char *read_until_any(Parser *p, const char *stop_chars) {
  size_t start = p->pos;
  while (!at_end(p) && !strchr(stop_chars, peek(p)))
    advance(p);
  size_t len = p->pos - start;
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, p->src + start, len);
  out[len] = '\0';
  return out;
}

/* Read a tag name or attribute name: [a-zA-Z0-9_-:] */
static char *read_name(Parser *p) {
  size_t start = p->pos;
  while (!at_end(p)) {
    char c = peek(p);
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':')
      advance(p);
    else
      break;
  }
  size_t len = p->pos - start;
  if (len == 0)
    return NULL;
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, p->src + start, len);
  out[len] = '\0';
  /* Lowercase the name for case-insensitive matching */
  for (size_t i = 0; i < len; i++)
    out[i] = (char)tolower((unsigned char)out[i]);
  return out;
}

/* Trim leading/trailing whitespace in-place. Returns the original pointer. */
static char *str_trim(char *s) {
  if (!s)
    return s;
  /* Leading */
  size_t start = 0;
  while (s[start] && isspace((unsigned char)s[start]))
    start++;
  if (start > 0)
    memmove(s, s + start, strlen(s) - start + 1);
  /* Trailing */
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1]))
    s[--len] = '\0';
  return s;
}

static const char *VOID_TAGS[] = {"area",  "base",   "br",    "col",  "embed",
                                  "hr",    "img",    "input", "link", "meta",
                                  "param", "source", "track", "wbr",  NULL};

static bool is_void_tag(const char *tag) {
  for (int i = 0; VOID_TAGS[i]; i++)
    if (strcmp(VOID_TAGS[i], tag) == 0)
      return true;
  return false;
}

/*
 * Skip a comment <!-- ... --> or DOCTYPE <!DOCTYPE ...>
 * Precondition: cursor is at '<'
 **/
static void skip_comment_or_doctype(Parser *p) {
  /* consume '<' */
  advance(p);
  if (peek(p) == '!') {
    advance(p);
    if (match_str(p, "--")) {
      /* HTML comment: <!-- ... --> */
      advance(p);
      advance(p); /* skip "--" */
      while (!at_end(p)) {
        if (match_str(p, "-->")) {
          p->pos += 3;
          return;
        }
        advance(p);
      }
    } else {
      /* DOCTYPE or unknown <! construct — skip to '>' */
      while (!at_end(p) && peek(p) != '>')
        advance(p);
      if (!at_end(p))
        advance(p); /* consume '>' */
    }
  }
}

/* -------------------------------------------------------------------------
 * Parse a quoted or unquoted attribute value.
 * Returns a heap-allocated string. Caller must free.
 * ------------------------------------------------------------------------- */
static char *parse_attr_value(Parser *p) {
  skip_whitespace(p);
  if (at_end(p))
    return NULL;

  char quote = peek(p);
  if (quote == '"' || quote == '\'') {
    advance(p); /* consume opening quote */
    char *val = read_until_char(p, quote);
    if (!at_end(p))
      advance(p); /* consume closing quote */
    return val;
  } else {
    /* Unquoted value: read until whitespace or '>' */
    return read_until_any(p, " \t\n\r>/");
  }
}

/*
 * Parse a tag's attributes.
 * Cursor must be positioned after the tag name.
 * Returns when '>' or '/>' is reached; does NOT consume it.
 **/
static void parse_attributes(Parser *p, __html_node__ *node) {
  while (!at_end(p)) {
    skip_whitespace(p);
    char c = peek(p);

    if (c == '>' || c == '/')
      return;
    if (at_end(p))
      return;

    /* Read attribute name */
    char *name = read_name(p);
    if (!name || strlen(name) == 0) {
      free(name);
      advance(p); /* skip unexpected character */
      continue;
    }

    skip_whitespace(p);

    if (peek(p) == '=') {
      advance(p); /* consume '=' */
      char *value = parse_attr_value(p);
      htmlattr_list_set(&node->attrs, name, value ? value : "");
      free(value);
    } else {
      /* Boolean attribute: <button disabled> → disabled="" */
      htmlattr_list_set(&node->attrs, name, "");
    }
    free(name);
  }
}

/*
 * Forward declaration
 **/
static __html_node__ *parse_node(Parser *p);

/*
 * Parse children of a node until we hit </tag_name> or end of input.
 * tag_name is the parent's tag (so we know when to stop).
 **/
static void parse_children(Parser *p, __html_node__ *parent) {
  while (!at_end(p)) {
    skip_whitespace(p);
    if (at_end(p))
      break;

    /* Check for a closing tag matching the parent */
    if (peek(p) == '<' && peek2(p) == '/') {

      size_t saved_pos = p->pos;
      int saved_line = p->line;
      p->pos += 2; /* skip '</' */
      skip_whitespace(p);
      char *close_tag = read_name(p);
      skip_whitespace(p);
      bool matched = close_tag && (strcmp(close_tag, parent->tag) == 0);
      free(close_tag);

      if (matched) {
        if (!at_end(p) && peek(p) == '>')
          advance(p); /* consume '>' */
        return;       /* done with this parent */
      }
      /* Not our tag — restore position and let parse_node handle it */
      p->pos = saved_pos;
      p->line = saved_line;
    }

    __html_node__ *child = parse_node(p);
    if (!child)
      continue;
    htmlnode_append_child(parent, child);
  }
}

/*
 * Parse one node (element or text).
 * Returns a newly allocated __html_node__*, or NULL on error / end.
 **/
static __html_node__ *parse_node(Parser *p) {
  skip_whitespace(p);
  if (at_end(p))
    return NULL;

  if (peek(p) != '<') {
    /* Collect text until the next '<' */
    char *text = read_until_char(p, '<');
    str_trim(text);
    if (!text || strlen(text) == 0) {
      free(text);
      return NULL;
    }
    /* Text nodes are represented as a synthetic "#text" element */
    __html_node__ *node = htmlnode_create("#text");
    node->text_content = text;
    return node;
  }

  if (peek(p) == '<' && peek2(p) == '!') {
    skip_comment_or_doctype(p);
    return NULL; /* comments produce no node */
  }

  if (peek(p) == '<' && peek2(p) == '/') {
    while (!at_end(p) && peek(p) != '>')
      advance(p);
    if (!at_end(p))
      advance(p);
    return NULL;
  }

  advance(p); /* consume '<' */
  skip_whitespace(p);

  char *tag = read_name(p);
  if (!tag || strlen(tag) == 0) {
    free(tag);
    /* Skip to '>' and return nothing */
    while (!at_end(p) && peek(p) != '>')
      advance(p);
    if (!at_end(p))
      advance(p);
    return NULL;
  }

  __html_node__ *node = htmlnode_create(tag);
  free(tag);

  /* Parse attributes */
  parse_attributes(p, node);

  skip_whitespace(p);

  /* Self-closing?  <br/>  <input/>  or void tag <br> <input> */
  bool self_close = false;
  if (peek(p) == '/') {
    advance(p); /* consume '/' */
    self_close = true;
  }
  if (!at_end(p) && peek(p) == '>')
    advance(p); /* consume '>' */

  if (self_close || is_void_tag(node->tag)) {
    return node; /* no children */
  }

  /* Recurse into children */
  parse_children(p, node);

  return node;
}

/*
 * Parse the full document and return the <html> root (or a synthetic root
 * if the source is a fragment).
 **/
static __html_node__ *parse_document(Parser *p) {
  /* Collect all top-level nodes */
  __html_node__ *root = NULL;

  while (!at_end(p)) {
    __html_node__ *node = parse_node(p);
    if (!node)
      continue;

    /* If we found an <html> element, use it as the root */
    if (strcmp(node->tag, "html") == 0) {
      htmlnode_free(root);
      return node;
    }

    /* Otherwise accumulate under a synthetic root */
    if (!root)
      root = htmlnode_create("html");
    htmlnode_append_child(root, node);
  }

  return root ? root : htmlnode_create("html");
}

/*
 * Public API
 * */
__html_node__ *html_parse_string(const char *html, char *error_buf,
                                 size_t error_len) {
  if (!html) {
    if (error_buf)
      snprintf(error_buf, error_len, "html string is NULL");
    return NULL;
  }

  Parser p = {
      .src = html,
      .len = strlen(html),
      .pos = 0,
      .line = 1,
      .error = error_buf,
      .errlen = error_len,
  };

  return parse_document(&p);
}

__html_node__ *html_parse_file(const char *path, char *error_buf,
                               size_t error_len) {
  if (!path) {
    if (error_buf)
      snprintf(error_buf, error_len, "path is NULL");
    return NULL;
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    if (error_buf)
      snprintf(error_buf, error_len, "cannot open '%s'", path);
    return NULL;
  }

  /* Read whole file */
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);

  if (size <= 0) {
    fclose(f);
    if (error_buf)
      snprintf(error_buf, error_len, "'%s' is empty", path);
    return NULL;
  }

  char *buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    if (error_buf)
      snprintf(error_buf, error_len, "out of memory");
    return NULL;
  }

  size_t read = fread(buf, 1, (size_t)size, f);
  buf[read] = '\0';
  fclose(f);

  __html_node__ *root = html_parse_string(buf, error_buf, error_len);
  free(buf);
  return root;
}

/*
 * Debug dump
 * */
void html_dump(const __html_node__ *node, int depth) {
  if (!node)
    return;

  /* Indent */
  for (int i = 0; i < depth; i++)
    printf("  ");

  if (strcmp(node->tag, "#text") == 0) {
    printf("#text \"%s\"\n", node->text_content ? node->text_content : "");
    return;
  }

  printf("<%s", node->tag);

  /* Print attributes */
  for (int i = 0; i < node->attrs.count; i++) {
    printf(" %s=\"%s\"", node->attrs.items[i].key, node->attrs.items[i].value);
  }

  if (node->child_count == 0 && !node->text_content) {
    printf(" />\n");
    return;
  }

  printf(">\n");

  /* Print text content */
  if (node->text_content && strlen(node->text_content) > 0) {
    for (int i = 0; i < depth + 1; i++)
      printf("  ");
    printf("\"%s\"\n", node->text_content);
  }

  /* Recurse */
  for (int i = 0; i < node->child_count; i++)
    html_dump(node->children[i], depth + 1);

  for (int i = 0; i < depth; i++)
    printf("  ");
  printf("</%s>\n", node->tag);
}
