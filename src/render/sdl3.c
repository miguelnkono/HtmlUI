#define _POSIX_C_SOURCE 200809L
/**
 * src/render/sdl3.c
 *
 * SDL3 renderer — __display_list__ → native window pixels via SDL3 GPU API.
 *
 * Enabled when HTMLUI_SDL3 is defined (cmake -DHTMLUI_ENABLE_SDL3=ON).
 *
 * Architecture
 * ------------
 *  Sdl3Renderer owns:
 *    SDL_Window*    — the OS native window
 *    SDL_Renderer*  — SDL3 hardware-accelerated 2D renderer
 *    FontCache      — FreeType glyph atlas backed by SDL_Texture
 *    TextureCache   — image textures loaded from CMD_IMAGE paths
 *
 *  Per-frame loop (called from ui_render_frame):
 *    1. SDL_SetRenderDrawColor + SDL_RenderClear
 *    2. Iterate display list, dispatch per command type
 *    3. SDL_RenderPresent
 *
 *  Text rendering:
 *    Glyphs are rasterized via FreeType into an 8-bit bitmap, uploaded
 *    to a per-size SDL_Texture atlas, and composited with
 *    SDL_SetTextureColorMod + SDL_RenderTexture.
 */

#ifdef HTMLUI_SDL3

#include "sdl3.h"
#include "../internal/types.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#ifdef HTMLUI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

/* =========================================================================
 * Glyph atlas
 * ========================================================================= */

#define ATLAS_W 1024
#define ATLAS_H 1024
#define MAX_GLYPHS 2048

typedef struct {
  uint32_t codepoint;
  int size_px;
  /* UV rect within the atlas texture */
  int atlas_x, atlas_y, atlas_w, atlas_h;
  /* Metrics */
  int bearing_x, bearing_y;
  int advance;
} GlyphSlot;

typedef struct {
  SDL_Texture *texture; /* ARGB8 atlas texture                        */
  int cursor_x;         /* next free column in current row            */
  int cursor_y;         /* current row top                            */
  int row_h;            /* height of tallest glyph in current row     */
  GlyphSlot slots[MAX_GLYPHS];
  int slot_count;
} GlyphAtlas;

/* =========================================================================
 * Renderer context
 * ========================================================================= */

struct Sdl3Renderer_t {
  SDL_Window *window;
  SDL_Renderer *renderer;

#ifdef HTMLUI_FREETYPE
  FT_Library ft_lib;
  FT_Face ft_face;
#endif

  GlyphAtlas atlas;
  int window_w;
  int window_h;
};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

Sdl3Renderer *sdl3_renderer_create(const char *title, int w, int h,
                                   bool resizable, bool vsync) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "[sdl3] SDL_Init failed: %s\n", SDL_GetError());
    return NULL;
  }

  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (resizable)
    flags |= SDL_WINDOW_RESIZABLE;

  Sdl3Renderer *sr = calloc(1, sizeof(Sdl3Renderer));
  if (!sr)
    return NULL;
  sr->window_w = w;
  sr->window_h = h;

  sr->window = SDL_CreateWindow(title, w, h, flags);
  if (!sr->window) {
    fprintf(stderr, "[sdl3] SDL_CreateWindow failed: %s\n", SDL_GetError());
    free(sr);
    return NULL;
  }

  const char *backend = NULL; /* NULL = SDL picks best available */
  sr->renderer = SDL_CreateRenderer(sr->window, backend);
  if (!sr->renderer) {
    fprintf(stderr, "[sdl3] SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(sr->window);
    free(sr);
    return NULL;
  }

  if (vsync)
    SDL_SetRenderVSync(sr->renderer, 1);

  /* Glyph atlas texture (ARGB8, streaming) */
  sr->atlas.texture =
      SDL_CreateTexture(sr->renderer, SDL_PIXELFORMAT_RGBA8888,
                        SDL_TEXTUREACCESS_STREAMING, ATLAS_W, ATLAS_H);
  if (!sr->atlas.texture) {
    fprintf(stderr, "[sdl3] Atlas texture creation failed: %s\n",
            SDL_GetError());
    /* Non-fatal — text will be skipped */
  } else {
    SDL_SetTextureBlendMode(sr->atlas.texture, SDL_BLENDMODE_BLEND);
  }

#ifdef HTMLUI_FREETYPE
  FT_Init_FreeType(&sr->ft_lib);
  sr->ft_face = NULL;
  const char *fonts[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf", NULL};
  for (int i = 0; fonts[i]; i++) {
    if (FT_New_Face(sr->ft_lib, fonts[i], 0, &sr->ft_face) == 0)
      break;
  }
#endif

  return sr;
}

void sdl3_renderer_destroy(Sdl3Renderer *sr) {
  if (!sr)
    return;
#ifdef HTMLUI_FREETYPE
  if (sr->ft_face)
    FT_Done_Face(sr->ft_face);
  if (sr->ft_lib)
    FT_Done_FreeType(sr->ft_lib);
#endif
  if (sr->atlas.texture)
    SDL_DestroyTexture(sr->atlas.texture);
  if (sr->renderer)
    SDL_DestroyRenderer(sr->renderer);
  if (sr->window)
    SDL_DestroyWindow(sr->window);
  free(sr);
  SDL_Quit();
}

/* =========================================================================
 * Glyph rasterization + atlas upload
 * ========================================================================= */

#ifdef HTMLUI_FREETYPE
static GlyphSlot *get_glyph(Sdl3Renderer *sr, uint32_t cp, int sz) {
  /* Lookup */
  for (int i = 0; i < sr->atlas.slot_count; i++) {
    GlyphSlot *g = &sr->atlas.slots[i];
    if (g->codepoint == cp && g->size_px == sz)
      return g;
  }
  if (sr->atlas.slot_count >= MAX_GLYPHS)
    return NULL;
  if (!sr->ft_face || !sr->atlas.texture)
    return NULL;

  FT_Set_Pixel_Sizes(sr->ft_face, 0, (FT_UInt)sz);
  FT_UInt gi = FT_Get_Char_Index(sr->ft_face, cp);
  if (FT_Load_Glyph(sr->ft_face, gi, FT_LOAD_RENDER))
    return NULL;
  FT_GlyphSlot gs = sr->ft_face->glyph;

  int bw = (int)gs->bitmap.width;
  int bh = (int)gs->bitmap.rows;
  if (bw <= 0 || bh <= 0) {
    /* Whitespace glyph — store metrics only */
    GlyphSlot *slot = &sr->atlas.slots[sr->atlas.slot_count++];
    memset(slot, 0, sizeof(*slot));
    slot->codepoint = cp;
    slot->size_px = sz;
    slot->advance = (int)(gs->advance.x >> 6);
    return slot;
  }

  /* Wrap to new row if needed */
  if (sr->atlas.cursor_x + bw > ATLAS_W) {
    sr->atlas.cursor_x = 0;
    sr->atlas.cursor_y += sr->atlas.row_h + 1;
    sr->atlas.row_h = 0;
  }
  if (sr->atlas.cursor_y + bh > ATLAS_H)
    return NULL; /* atlas full */

  /* Upload glyph bitmap into atlas — convert 8-bit alpha → RGBA */
  void *pixels;
  int pitch;
  SDL_Rect lock_rect = {sr->atlas.cursor_x, sr->atlas.cursor_y, bw, bh};
  if (!SDL_LockTexture(sr->atlas.texture, &lock_rect, &pixels, &pitch)) {
    fprintf(stderr, "[sdl3] LockTexture: %s\n", SDL_GetError());
    return NULL;
  }
  uint8_t *src = gs->bitmap.buffer;
  uint8_t *dst = (uint8_t *)pixels;
  for (int row = 0; row < bh; row++) {
    uint32_t *row_ptr = (uint32_t *)(dst + row * pitch);
    for (int col = 0; col < bw; col++) {
      uint8_t a = src[row * gs->bitmap.pitch + col];
      /* RGBA8888: r=a, g=a, b=a, a=a (white glyph, tinted at render) */
      row_ptr[col] = ((uint32_t)a << 24) | ((uint32_t)a << 16) |
                     ((uint32_t)a << 8) | (uint32_t)a;
    }
  }
  SDL_UnlockTexture(sr->atlas.texture);

  GlyphSlot *slot = &sr->atlas.slots[sr->atlas.slot_count++];
  slot->codepoint = cp;
  slot->size_px = sz;
  slot->atlas_x = sr->atlas.cursor_x;
  slot->atlas_y = sr->atlas.cursor_y;
  slot->atlas_w = bw;
  slot->atlas_h = bh;
  slot->bearing_x = gs->bitmap_left;
  slot->bearing_y = gs->bitmap_top;
  slot->advance = (int)(gs->advance.x >> 6);

  sr->atlas.cursor_x += bw + 1;
  if (bh > sr->atlas.row_h)
    sr->atlas.row_h = bh;
  return slot;
}
#endif /* HTMLUI_FREETYPE */

/* =========================================================================
 * Draw helpers
 * ========================================================================= */

static void sdl3_draw_rect(Sdl3Renderer *sr, const DrawCommand *cmd) {
  SDL_FRect r = {cmd->x, cmd->y, cmd->w, cmd->h};
  Color fc = cmd->fill_color;
  SDL_SetRenderDrawColor(sr->renderer, fc.r, fc.g, fc.b, fc.a);
  SDL_RenderFillRect(sr->renderer, &r);

  float bw = cmd->border_width;
  if (bw > 0) {
    Color bc = cmd->border_color;
    SDL_SetRenderDrawColor(sr->renderer, bc.r, bc.g, bc.b, bc.a);
    /* Simple 4-sided border via four filled rects */
    SDL_FRect top = {cmd->x, cmd->y, cmd->w, bw};
    SDL_FRect bottom = {cmd->x, cmd->y + cmd->h - bw, cmd->w, bw};
    SDL_FRect left = {cmd->x, cmd->y + bw, bw, cmd->h - bw * 2};
    SDL_FRect right = {cmd->x + cmd->w - bw, cmd->y + bw, bw, cmd->h - bw * 2};
    SDL_RenderFillRect(sr->renderer, &top);
    SDL_RenderFillRect(sr->renderer, &bottom);
    SDL_RenderFillRect(sr->renderer, &left);
    SDL_RenderFillRect(sr->renderer, &right);
  }
  /*
   * NOTE: Rounded corners via border_radius require either:
   *  a) Custom geometry triangles (SDL_RenderGeometry)
   *  b) The SDL3 GPU API for SDF-based rendering
   * This is a planned enhancement. For now, corners are square.
   * See: docs/ROADMAP.md → "Rounded corner rendering"
   */
}

static void sdl3_draw_text(Sdl3Renderer *sr, const DrawCommand *cmd) {
#ifdef HTMLUI_FREETYPE
  if (!cmd->text || !sr->ft_face || !sr->atlas.texture)
    return;
  int sz = (int)(cmd->font_size > 0 ? cmd->font_size : 16);

  /* Measure for alignment */
  int total_w = 0;
  const char *s = cmd->text;
  while (*s) {
    unsigned char uc = (unsigned char)*s++;
    GlyphSlot *g = get_glyph(sr, uc, sz);
    if (g)
      total_w += g->advance;
  }

  float pen_x = cmd->x;
  if (cmd->text_align == 1)
    pen_x = cmd->x + (cmd->w - total_w) * 0.5f;
  else if (cmd->text_align == 2)
    pen_x = cmd->x + cmd->w - total_w;

  float baseline_y = cmd->y + cmd->h * 0.75f;
  if (cmd->h < cmd->font_size * 1.5f)
    baseline_y = cmd->y + cmd->font_size;

  Color tc = cmd->text_color;
  SDL_SetTextureColorMod(sr->atlas.texture, tc.r, tc.g, tc.b);
  SDL_SetTextureAlphaMod(sr->atlas.texture, tc.a);

  s = cmd->text;
  while (*s) {
    unsigned char uc = (unsigned char)*s++;
    if (uc >= 0x80)
      continue; /* ASCII-only for now */
    GlyphSlot *g = get_glyph(sr, uc, sz);
    if (!g || g->atlas_w == 0) {
      if (g)
        pen_x += g->advance;
      continue;
    }

    SDL_FRect dst = {pen_x + g->bearing_x, baseline_y - g->bearing_y,
                     (float)g->atlas_w, (float)g->atlas_h};
    SDL_FRect src = {(float)g->atlas_x, (float)g->atlas_y, (float)g->atlas_w,
                     (float)g->atlas_h};
    SDL_RenderTexture(sr->renderer, sr->atlas.texture, &src, &dst);
    pen_x += g->advance;
  }
#else
  (void)sr;
  (void)cmd; /* FreeType not available */
#endif
}

/* =========================================================================
 * Public render functions
 * ========================================================================= */

void sdl3_renderer_begin_frame(Sdl3Renderer *sr, Color bg) {
  SDL_SetRenderDrawColor(sr->renderer, bg.r, bg.g, bg.b, bg.a);
  SDL_RenderClear(sr->renderer);
  SDL_SetRenderDrawBlendMode(sr->renderer, SDL_BLENDMODE_BLEND);
}

void sdl3_renderer_draw(Sdl3Renderer *sr, const __display_list__ *dl) {
  if (!sr || !dl)
    return;

  for (int i = 0; i < dl->count; i++) {
    const DrawCommand *cmd = &dl->commands[i];
    switch (cmd->type) {
    case CMD_RECT:
      sdl3_draw_rect(sr, cmd);
      break;
    case CMD_TEXT:
      sdl3_draw_text(sr, cmd);
      break;
    case CMD_CLIP_PUSH: {
      SDL_Rect clip = {(int)cmd->x, (int)cmd->y, (int)cmd->w, (int)cmd->h};
      SDL_SetRenderClipRect(sr->renderer, &clip);
      break;
    }
    case CMD_CLIP_POP:
      SDL_SetRenderClipRect(sr->renderer, NULL);
      break;
    case CMD_IMAGE:
      /* TODO: texture cache + SDL_RenderTexture */
      break;
    }
  }
}

void sdl3_renderer_end_frame(Sdl3Renderer *sr) {
  SDL_RenderPresent(sr->renderer);
}

bool sdl3_poll_events(Sdl3Renderer *sr) {
  (void)sr;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT)
      return false;
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE)
      return false;
  }
  return true;
}

void sdl3_renderer_get_size(Sdl3Renderer *sr, int *w, int *h) {
  if (sr)
    SDL_GetWindowSizeInPixels(sr->window, w, h);
}

#endif /* HTMLUI_SDL3 */
