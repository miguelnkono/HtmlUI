#define _POSIX_C_SOURCE 200809L
/**
 * parser/css.c
 *
 * Minimal CSS parser for the htmlui toolkit.
 *
 * Parsing strategy
 * ----------------
 * Single-pass character cursor, no backtracking.
 *
 *  parse_stylesheet()
 *    └─ skip_whitespace_and_comments()
 *    └─ parse_rule()
 *         ├─ parse_selector_list()   → one or more selectors separated by ','
 *         └─ parse_declaration_block()
 *              └─ parse_declaration()  → property: value;
 */

#include "css.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal parser state
 * ========================================================================= */

typedef struct
{
  const char *src;
  size_t len;
  size_t pos;
  int line;
  int source_order_counter;
  char *error;
  size_t errlen;
} CssParser;

/* =========================================================================
 * Cursor helpers
 * ========================================================================= */

static char css_peek(const CssParser *p)
{
  return (p->pos < p->len) ? p->src[p->pos] : '\0';
}

static char css_peek2(const CssParser *p)
{
  return (p->pos + 1 < p->len) ? p->src[p->pos + 1] : '\0';
}

static char css_advance(CssParser *p)
{
  char c = css_peek(p);
  if (c == '\n')
    p->line++;
  if (p->pos < p->len)
    p->pos++;
  return c;
}

static int css_at_end(const CssParser *p)
{
  return p->pos >= p->len;
}

/* =========================================================================
 * Whitespace and comment skipping
 * ========================================================================= */

static void skip_whitespace_and_comments(CssParser *p)
{
  while (!css_at_end(p))
  {
    /* Block comment */
    if (css_peek(p) == '/' && css_peek2(p) == '*')
    {
      p->pos += 2;
      while (!css_at_end(p))
      {
        if (css_peek(p) == '*' && css_peek2(p) == '/')
        {
          p->pos += 2;
          break;
        }
        css_advance(p);
      }
      continue;
    }
    /* Single-line comment  // ... (non-standard but common in dev CSS) */
    if (css_peek(p) == '/' && css_peek2(p) == '/')
    {
      while (!css_at_end(p) && css_peek(p) != '\n')
        css_advance(p);
      continue;
    }
    if (isspace((unsigned char)css_peek(p)))
    {
      css_advance(p);
      continue;
    }
    break;
  }
}

/* =========================================================================
 * String building helpers
 * ========================================================================= */

/* Read until any character in stop_chars is found. Does NOT consume the
 * stop character. Returns a heap-allocated, NUL-terminated string. */
static char *css_read_until(CssParser *p, const char *stop_chars)
{
  size_t start = p->pos;
  while (!css_at_end(p) && !strchr(stop_chars, css_peek(p)))
    css_advance(p);
  size_t len = p->pos - start;
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, p->src + start, len);
  out[len] = '\0';
  return out;
}

/* Trim leading and trailing whitespace in-place. Returns the same pointer. */
static char *css_trim(char *s)
{
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

/* =========================================================================
 * Declaration helpers
 * ========================================================================= */

static void rule_add_decl(__css_rule__ *rule,
                          const char *property,
                          const char *value)
{
  if (!property || !value || strlen(property) == 0 || strlen(value) == 0)
    return;

  /* Grow the declarations array if needed */
  if (rule->decl_count >= rule->decl_capacity)
  {
    int new_cap = (rule->decl_capacity == 0) ? 8 : rule->decl_capacity * 2;
    __css_declaration__ *decls = realloc(
        rule->declarations,
        sizeof(__css_declaration__) * (size_t)new_cap);
    if (!decls)
      return;
    rule->declarations = decls;
    rule->decl_capacity = new_cap;
  }

  rule->declarations[rule->decl_count].property = strdup(property);
  rule->declarations[rule->decl_count].value = strdup(value);
  rule->decl_count++;
}

/* =========================================================================
 * parse_declaration
 *
 * Parses one  property: value  pair from inside a { } block.
 * Cursor must be positioned at the start of the property name.
 * Advances past the terminating ';' (or '}' if the semicolon is omitted).
 * ========================================================================= */

static void parse_declaration(CssParser *p, __css_rule__ *rule)
{
  skip_whitespace_and_comments(p);
  if (css_at_end(p) || css_peek(p) == '}')
    return;

  /* Property name: read until ':' or '}' */
  char *prop_raw = css_read_until(p, ":;}");
  if (!prop_raw)
    return;
  char *prop = css_trim(prop_raw);

  if (css_peek(p) != ':')
  {
    /* Malformed declaration — skip to next ';' or '}' */
    free(prop_raw);
    while (!css_at_end(p) && css_peek(p) != ';' && css_peek(p) != '}')
      css_advance(p);
    if (!css_at_end(p) && css_peek(p) == ';')
      css_advance(p); /* consume ';' */
    return;
  }
  css_advance(p); /* consume ':' */

  /* Value: read until ';' or '}' */
  char *val_raw = css_read_until(p, ";}");
  char *val = css_trim(val_raw);

  if (prop && strlen(prop) > 0 && val && strlen(val) > 0)
    rule_add_decl(rule, prop, val);

  free(prop_raw);
  free(val_raw);

  if (!css_at_end(p) && css_peek(p) == ';')
    css_advance(p); /* consume ';' */
}

/* =========================================================================
 * parse_declaration_block
 *
 * Parses the { ... } block of a rule, filling rule->declarations.
 * Precondition: cursor is positioned at '{'.
 * Postcondition: cursor is positioned after '}'.
 * ========================================================================= */

static void parse_declaration_block(CssParser *p, __css_rule__ *rule)
{
  if (css_at_end(p) || css_peek(p) != '{')
    return;
  css_advance(p); /* consume '{' */

  while (!css_at_end(p))
  {
    skip_whitespace_and_comments(p);
    if (css_at_end(p))
      break;
    if (css_peek(p) == '}')
    {
      css_advance(p); /* consume '}' */
      break;
    }
    parse_declaration(p, rule);
  }
}

/* =========================================================================
 * parse_rule
 *
 * Parses one full CSS rule (selector list + declaration block) and appends
 * one __css_rule__ per selector to the list.
 *
 * Example:
 *   h1, .title { color: red; font-size: 24px; }
 *
 * produces two rules with the same declarations but different selectors.
 * ========================================================================= */

static void parse_rule(CssParser *p, __css_rule_list__ *list)
{
  skip_whitespace_and_comments(p);
  if (css_at_end(p))
    return;

  /* Skip @-rules (e.g. @media, @keyframes) — not supported yet */
  if (css_peek(p) == '@')
  {
    int depth = 0;
    while (!css_at_end(p))
    {
      char c = css_advance(p);
      if (c == '{')
        depth++;
      else if (c == '}')
      {
        if (--depth <= 0)
          break;
      }
      else if (c == ';' && depth == 0)
      {
        break; /* simple @-rule like @charset "utf-8"; */
      }
    }
    return;
  }

  /* Read the full selector list up to '{' */
  char *sel_block_raw = css_read_until(p, "{");
  if (!sel_block_raw)
    return;

  if (css_at_end(p) || css_peek(p) != '{')
  {
    /* No block found — malformed, skip */
    free(sel_block_raw);
    return;
  }

  /* Parse the declaration block into a temporary rule first */
  __css_rule__ tmp_rule;
  memset(&tmp_rule, 0, sizeof(tmp_rule));
  parse_declaration_block(p, &tmp_rule);

  if (tmp_rule.decl_count == 0)
  {
    /* Empty block — nothing to emit */
    free(tmp_rule.declarations);
    free(sel_block_raw);
    return;
  }

  /* Split the selector block on ',' to produce one rule per selector */
  char *sel_block = sel_block_raw; /* alias for clarity */
  char *saveptr = NULL;
  char *tok = strtok_r(sel_block, ",", &saveptr);

  while (tok)
  {
    char *sel = css_trim(tok);

    if (sel && strlen(sel) > 0)
    {
      __css_rule__ rule;
      memset(&rule, 0, sizeof(rule));
      rule.selector = strdup(sel);
      rule.source_order = p->source_order_counter++;

      /* Deep-copy declarations from the shared tmp_rule */
      for (int i = 0; i < tmp_rule.decl_count; i++)
      {
        rule_add_decl(&rule,
                      tmp_rule.declarations[i].property,
                      tmp_rule.declarations[i].value);
      }

      css_rule_list_add(list, rule);
    }

    tok = strtok_r(NULL, ",", &saveptr);
  }

  /* Free the temporary rule's declaration copies */
  for (int i = 0; i < tmp_rule.decl_count; i++)
  {
    free(tmp_rule.declarations[i].property);
    free(tmp_rule.declarations[i].value);
  }
  free(tmp_rule.declarations);
  free(sel_block_raw);
}

/* =========================================================================
 * parse_stylesheet
 *
 * Top-level driver: repeatedly calls parse_rule until the source is
 * exhausted.
 * ========================================================================= */

static void parse_stylesheet(CssParser *p, __css_rule_list__ *list)
{
  while (!css_at_end(p))
  {
    skip_whitespace_and_comments(p);
    if (css_at_end(p))
      break;
    parse_rule(p, list);
  }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int css_parse_string(const char *css,
                     __css_rule_list__ *list,
                     char *error_buf,
                     size_t error_len)
{
  if (!css || !list)
  {
    if (error_buf)
      snprintf(error_buf, error_len, "css or list is NULL");
    return -1;
  }

  CssParser p;
  memset(&p, 0, sizeof(p));
  p.src = css;
  p.len = strlen(css);
  p.pos = 0;
  p.line = 1;
  p.error = error_buf;
  p.errlen = error_len;
  p.source_order_counter = list->count; /* continue numbering from existing rules */

  parse_stylesheet(&p, list);
  return 0;
}

int css_parse_file(const char *path,
                   __css_rule_list__ *list,
                   char *error_buf,
                   size_t error_len)
{
  if (!path || !list)
  {
    if (error_buf)
      snprintf(error_buf, error_len, "path or list is NULL");
    return -1;
  }

  FILE *f = fopen(path, "rb");
  if (!f)
  {
    if (error_buf)
      snprintf(error_buf, error_len, "cannot open '%s'", path);
    return -1;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);

  if (size <= 0)
  {
    fclose(f);
    /* Empty file is not an error — just no rules */
    return 0;
  }

  char *buf = malloc((size_t)size + 1);
  if (!buf)
  {
    fclose(f);
    if (error_buf)
      snprintf(error_buf, error_len, "out of memory");
    return -1;
  }

  size_t nread = fread(buf, 1, (size_t)size, f);
  buf[nread] = '\0';
  fclose(f);

  int result = css_parse_string(buf, list, error_buf, error_len);
  free(buf);
  return result;
}

/* =========================================================================
 * Debug dump
 * ========================================================================= */

void css_dump(const __css_rule_list__ *list)
{
  if (!list)
    return;
  printf("__css_rule_list__ (%d rules)\n", list->count);
  printf("==================================\n");
  for (int i = 0; i < list->count; i++)
  {
    const __css_rule__ *r = &list->rules[i];
    printf("[%d] \"%s\" (%d decls, order=%d)\n",
           i, r->selector ? r->selector : "(null)",
           r->decl_count, r->source_order);
    for (int j = 0; j < r->decl_count; j++)
    {
      printf("      %s: %s\n",
             r->declarations[j].property,
             r->declarations[j].value);
    }
  }
}
