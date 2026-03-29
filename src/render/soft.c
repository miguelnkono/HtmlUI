#define _POSIX_C_SOURCE 200809L
/**
 * src/render/soft.c
 *
 * Software rasterizer.
 *
 * Implements:
 *  - Filled rectangles (axis-aligned)
 *  - Rounded corners via per-pixel SDF evaluation
 *  - Alpha-blended color compositing
 *  - Borders (uniform width)
 *  - Text via FreeType glyph rasterization + simple glyph cache
 *  - Clip rectangle stack
 *  - PPM image output
 */

#include "soft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* FreeType is optional — text is silently skipped if unavailable */
#ifdef HTMLUI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

/* =========================================================================
 * Types
 * ========================================================================= */

#define MAX_CLIP_STACK 32

typedef struct
{
    float x, y, w, h;
} ClipRect;

/* Simple glyph cache entry */
typedef struct GlyphEntry
{
    uint32_t codepoint;
    int size_px;             /* font size in pixels        */
    int bm_w, bm_h;          /* bitmap dimensions          */
    int bearing_x;           /* left bearing               */
    int bearing_y;           /* top bearing (from baseline) */
    int advance;             /* horizontal advance (pixels) */
    uint8_t *bitmap;         /* 8-bit alpha bitmap         */
    struct GlyphEntry *next; /* hash chain                 */
} GlyphEntry;

#define GLYPH_CACHE_SIZE 512

struct SoftRenderer_t
{
    int width, height;
    uint8_t *pixels; /* RGBA8, row-major            */

    ClipRect clip_stack[MAX_CLIP_STACK];
    int clip_depth;

#ifdef HTMLUI_FREETYPE
    FT_Library ft_lib;
    FT_Face ft_face;
    GlyphEntry *glyph_cache[GLYPH_CACHE_SIZE];
#endif
};

/* =========================================================================
 * Pixel helpers
 * ========================================================================= */

static inline void put_pixel_blend(SoftRenderer *sr,
                                   int x, int y, Color c)
{
    if (x < 0 || y < 0 || x >= sr->width || y >= sr->height)
        return;

    /* Clip stack check */
    if (sr->clip_depth > 0)
    {
        const ClipRect *cr = &sr->clip_stack[sr->clip_depth - 1];
        if ((float)x < cr->x || (float)x >= cr->x + cr->w)
            return;
        if ((float)y < cr->y || (float)y >= cr->y + cr->h)
            return;
    }

    uint8_t *p = sr->pixels + (y * sr->width + x) * 4;
    if (c.a == 255)
    {
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = 255;
    }
    else if (c.a > 0)
    {
        /* Porter-Duff source-over */
        uint32_t alpha = c.a;
        uint32_t inv = 255 - alpha;
        p[0] = (uint8_t)((c.r * alpha + p[0] * inv) / 255);
        p[1] = (uint8_t)((c.g * alpha + p[1] * inv) / 255);
        p[2] = (uint8_t)((c.b * alpha + p[2] * inv) / 255);
        p[3] = (uint8_t)(alpha + p[3] * inv / 255);
    }
}

/* =========================================================================
 * Rounded rectangle rasterizer (SDF-based)
 * ========================================================================= */

/*
 * Signed distance to a rounded-corner box.
 * px,py = point relative to box center
 * hw,hh = half-width, half-height of the box
 * r     = corner radius
 */
static float sdf_rounded_box(float px, float py,
                             float hw, float hh, float r)
{
    float qx = fabsf(px) - hw + r;
    float qy = fabsf(py) - hh + r;
    float outer = sqrtf(fmaxf(qx, 0) * fmaxf(qx, 0) +
                        fmaxf(qy, 0) * fmaxf(qy, 0));
    float inner = fminf(fmaxf(qx, qy), 0.0f);
    return outer + inner - r;
}

static void draw_rect(SoftRenderer *sr, const DrawCommand *cmd)
{
    float x = cmd->x, y = cmd->y;
    float w = cmd->w, h = cmd->h;
    float r = cmd->border_radius;
    float bw = cmd->border_width;
    Color fc = cmd->fill_color;
    Color bc = cmd->border_color;

    if (w <= 0 || h <= 0)
        return;

    /* Clamp to buffer with 1px AA fringe */
    int x0 = (int)floorf(x) - 1;
    int y0 = (int)floorf(y) - 1;
    int x1 = (int)ceilf(x + w) + 1;
    int y1 = (int)ceilf(y + h) + 1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > sr->width)
        x1 = sr->width;
    if (y1 > sr->height)
        y1 = sr->height;

    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    if (r > hw)
        r = hw;
    if (r > hh)
        r = hh;

    for (int py = y0; py < y1; py++)
    {
        for (int px = x0; px < x1; px++)
        {
            float fx = (float)px + 0.5f - cx;
            float fy = (float)py + 0.5f - cy;
            float d = sdf_rounded_box(fx, fy, hw, hh, r > 0 ? r : 0);

            /* Anti-aliased fill */
            if (d < 0.5f)
            {
                float fill_alpha = 1.0f - fmaxf(0.0f, fminf(1.0f, d + 0.5f));
                /* Border zone */
                if (bw > 0 && d > -bw - 0.5f)
                {
                    float b_alpha = 1.0f - fmaxf(0.0f, fminf(1.0f, d + 0.5f));
                    float b_inner = fmaxf(0.0f, fminf(1.0f, -(d + bw) + 0.5f));
                    /* Border ring = fill_alpha minus inner fill */
                    if (b_alpha > 0 && b_inner < 1.0f)
                    {
                        float ba = b_alpha * (1.0f - b_inner);
                        Color bc2 = bc;
                        bc2.a = (uint8_t)(bc.a * ba);
                        put_pixel_blend(sr, px, py, bc2);
                        /* inner fill on top */
                        Color fc2 = fc;
                        fc2.a = (uint8_t)(fc.a * b_inner * fill_alpha);
                        if (fc2.a > 0)
                            put_pixel_blend(sr, px, py, fc2);
                        continue;
                    }
                }
                Color fc2 = fc;
                fc2.a = (uint8_t)(fc.a * fill_alpha);
                put_pixel_blend(sr, px, py, fc2);
            }
        }
    }
}

/* =========================================================================
 * Text rendering via FreeType
 * ========================================================================= */

#ifdef HTMLUI_FREETYPE

static uint32_t glyph_hash(uint32_t cp, int sz)
{
    return (cp * 2654435761u ^ (uint32_t)sz) % GLYPH_CACHE_SIZE;
}

static GlyphEntry *cache_lookup(SoftRenderer *sr, uint32_t cp, int sz)
{
    GlyphEntry *e = sr->glyph_cache[glyph_hash(cp, sz)];
    while (e)
    {
        if (e->codepoint == cp && e->size_px == sz)
            return e;
        e = e->next;
    }
    return NULL;
}

static GlyphEntry *cache_insert(SoftRenderer *sr, uint32_t cp, int sz)
{
    FT_Set_Pixel_Sizes(sr->ft_face, 0, (FT_UInt)sz);
    FT_UInt gi = FT_Get_Char_Index(sr->ft_face, cp);
    if (FT_Load_Glyph(sr->ft_face, gi, FT_LOAD_RENDER))
        return NULL;
    FT_GlyphSlot gs = sr->ft_face->glyph;

    GlyphEntry *e = calloc(1, sizeof(GlyphEntry));
    if (!e)
        return NULL;
    e->codepoint = cp;
    e->size_px = sz;
    e->bm_w = (int)gs->bitmap.width;
    e->bm_h = (int)gs->bitmap.rows;
    e->bearing_x = gs->bitmap_left;
    e->bearing_y = gs->bitmap_top;
    e->advance = (int)(gs->advance.x >> 6);
    if (e->bm_w > 0 && e->bm_h > 0)
    {
        e->bitmap = malloc((size_t)(e->bm_w * e->bm_h));
        if (e->bitmap)
            memcpy(e->bitmap, gs->bitmap.buffer,
                   (size_t)(e->bm_w * e->bm_h));
    }
    uint32_t slot = glyph_hash(cp, sz);
    e->next = sr->glyph_cache[slot];
    sr->glyph_cache[slot] = e;
    return e;
}

static void draw_text(SoftRenderer *sr, const DrawCommand *cmd)
{
    if (!cmd->text || !sr->ft_face)
        return;
    int sz = (int)(cmd->font_size > 0 ? cmd->font_size : 16);
    FT_Set_Pixel_Sizes(sr->ft_face, 0, (FT_UInt)sz);

    /* Measure string width for alignment */
    int total_w = 0;
    const char *s = cmd->text;
    while (*s)
    {
        unsigned char uc = (unsigned char)*s++;
        if (uc >= 0x80)
            continue; /* skip non-ASCII for now */
        GlyphEntry *g = cache_lookup(sr, uc, sz);
        if (!g)
            g = cache_insert(sr, uc, sz);
        if (g)
            total_w += g->advance;
    }

    /* Horizontal alignment offset */
    float start_x = cmd->x;
    if (cmd->text_align == 1) /* center */
        start_x = cmd->x + (cmd->w - (float)total_w) * 0.5f;
    else if (cmd->text_align == 2) /* right  */
        start_x = cmd->x + cmd->w - (float)total_w;

    /* Baseline: approximately 75% from top of the content box */
    float baseline_y = cmd->y + cmd->h * 0.75f;
    if (cmd->h < sz * 1.5f)
        baseline_y = cmd->y + (float)sz;

    /* Render each glyph */
    float pen_x = start_x;
    s = cmd->text;
    while (*s)
    {
        unsigned char uc = (unsigned char)*s++;
        if (uc >= 0x80)
            continue;
        GlyphEntry *g = cache_lookup(sr, uc, sz);
        if (!g)
            g = cache_insert(sr, uc, sz);
        if (!g)
            continue;

        int gx = (int)(pen_x + g->bearing_x);
        int gy = (int)(baseline_y - g->bearing_y);

        if (g->bitmap)
        {
            for (int row = 0; row < g->bm_h; row++)
            {
                for (int col = 0; col < g->bm_w; col++)
                {
                    uint8_t alpha = g->bitmap[row * g->bm_w + col];
                    if (alpha == 0)
                        continue;
                    Color c = cmd->text_color;
                    c.a = (uint8_t)((c.a * alpha) / 255);
                    put_pixel_blend(sr, gx + col, gy + row, c);
                }
            }
        }
        pen_x += g->advance;
    }
}

#else /* no FreeType — draw a placeholder bar */

static void draw_text(SoftRenderer *sr, const DrawCommand *cmd)
{
    /* Draw a thin colored bar where the text would be */
    int bar_h = (int)(cmd->font_size * 0.6f);
    if (bar_h < 2)
        bar_h = 2;
    if (bar_h > (int)cmd->h)
        bar_h = (int)cmd->h;
    DrawCommand bar = *cmd;
    bar.type = CMD_RECT;
    bar.fill_color = cmd->text_color;
    bar.fill_color.a = 80; /* semi-transparent placeholder */
    bar.border_width = 0;
    bar.border_radius = 2;
    bar.h = (float)bar_h;
    bar.y = cmd->y + (cmd->h - bar.h) * 0.5f;
    draw_rect(sr, &bar);
}

#endif /* HTMLUI_FREETYPE */

/* =========================================================================
 * SoftRenderer lifecycle
 * ========================================================================= */

SoftRenderer *soft_renderer_create(int width, int height)
{
    SoftRenderer *sr = calloc(1, sizeof(SoftRenderer));
    if (!sr)
        return NULL;
    sr->width = width;
    sr->height = height;
    sr->pixels = calloc((size_t)(width * height * 4), 1);
    if (!sr->pixels)
    {
        free(sr);
        return NULL;
    }
    sr->clip_depth = 0;

#ifdef HTMLUI_FREETYPE
    if (FT_Init_FreeType(&sr->ft_lib) != 0)
    {
        fprintf(stderr, "[soft] FreeType init failed\n");
        sr->ft_face = NULL;
    }
    else
    {
        /* Try common system fonts in order */
        const char *fonts[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            NULL};
        sr->ft_face = NULL;
        for (int i = 0; fonts[i]; i++)
        {
            if (FT_New_Face(sr->ft_lib, fonts[i], 0, &sr->ft_face) == 0)
            {
                fprintf(stderr, "[soft] Font loaded: %s\n", fonts[i]);
                break;
            }
        }
        if (!sr->ft_face)
            fprintf(stderr, "[soft] No system font found — text will be placeholders\n");
    }
#endif
    return sr;
}

void soft_renderer_destroy(SoftRenderer *sr)
{
    if (!sr)
        return;
#ifdef HTMLUI_FREETYPE
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++)
    {
        GlyphEntry *e = sr->glyph_cache[i];
        while (e)
        {
            GlyphEntry *next = e->next;
            free(e->bitmap);
            free(e);
            e = next;
        }
    }
    if (sr->ft_face)
        FT_Done_Face(sr->ft_face);
    if (sr->ft_lib)
        FT_Done_FreeType(sr->ft_lib);
#endif
    free(sr->pixels);
    free(sr);
}

int soft_renderer_resize(SoftRenderer *sr, int width, int height)
{
    if (!sr || width <= 0 || height <= 0)
        return -1;
    uint8_t *new_pixels = calloc((size_t)(width * height * 4), 1);
    if (!new_pixels)
        return -1;
    free(sr->pixels);
    sr->pixels = new_pixels;
    sr->width = width;
    sr->height = height;
    sr->clip_depth = 0; /* old clip rects are stale after resize */
    return 0;
}

void soft_renderer_clear(SoftRenderer *sr, Color c)
{
    if (!sr)
        return;
    int n = sr->width * sr->height;
    uint8_t *p = sr->pixels;
    for (int i = 0; i < n; i++, p += 4)
    {
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = c.a;
    }
}

void soft_renderer_draw(SoftRenderer *sr, const __display_list__ *dl)
{
    if (!sr || !dl)
        return;
    for (int i = 0; i < dl->count; i++)
    {
        const DrawCommand *cmd = &dl->commands[i];
        switch (cmd->type)
        {
        case CMD_RECT:
            draw_rect(sr, cmd);
            break;
        case CMD_TEXT:
            draw_text(sr, cmd);
            break;
        case CMD_CLIP_PUSH:
            if (sr->clip_depth < MAX_CLIP_STACK)
            {
                ClipRect *cr = &sr->clip_stack[sr->clip_depth++];
                cr->x = cmd->x;
                cr->y = cmd->y;
                cr->w = cmd->w;
                cr->h = cmd->h;
            }
            break;
        case CMD_CLIP_POP:
            if (sr->clip_depth > 0)
                sr->clip_depth--;
            break;
        case CMD_IMAGE:
            /* TODO Milestone 4+: texture cache */
            break;
        }
    }
}

const uint8_t *soft_renderer_pixels(const SoftRenderer *sr)
{
    return sr ? sr->pixels : NULL;
}

int soft_renderer_save_ppm(const SoftRenderer *sr, const char *path)
{
    if (!sr || !path)
        return -1;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", sr->width, sr->height);
    int n = sr->width * sr->height;
    const uint8_t *p = sr->pixels;
    for (int i = 0; i < n; i++, p += 4)
        fwrite(p, 1, 3, f); /* PPM is RGB, drop alpha */
    fclose(f);
    return 0;
}
