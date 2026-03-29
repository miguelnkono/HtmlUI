/**
 * src/render/soft.h
 *
 * Software rasterizer — __display_list__ → RGBA pixel buffer.
 *
 * Renders the display list into a flat RGBA8 pixel buffer entirely on
 * the CPU. No GPU, no window system required.
 *
 * Used for:
 *   - CI/testing: render to PPM image and diff against reference
 *   - Platforms without SDL3 installed
 *   - Debugging: inspect pixel output without a display
 *
 * Font rendering uses FreeType for glyph rasterization.
 * When FreeType is unavailable, text commands are skipped silently.
 *
 * Rounded corners use a simple SDF (signed distance field) approach:
 *   For each pixel, compute distance to the nearest corner arc.
 *   Pixels outside the rounded rect are discarded.
 */

#ifndef HTMLUI_RENDER_SOFT_H
#define HTMLUI_RENDER_SOFT_H

#include "../internal/types.h"

/**
 * Opaque software renderer context.
 * Created with soft_renderer_create(), destroyed with soft_renderer_destroy().
 */
typedef struct SoftRenderer_t SoftRenderer;

/**
 * Create a software renderer targeting a pixel buffer of the given size.
 *
 * @param width   Buffer width  in pixels.
 * @param height  Buffer height in pixels.
 * @return        New SoftRenderer, or NULL on OOM.
 */
SoftRenderer *soft_renderer_create(int width, int height);

/**
 * Destroy a renderer and free all resources.
 */
void soft_renderer_destroy(SoftRenderer *sr);

/**
 * Resize the pixel buffer to new dimensions.
 * Existing pixel content is discarded.
 * Returns 0 on success, -1 on OOM (renderer is left unchanged on failure).
 */
int soft_renderer_resize(SoftRenderer *sr, int width, int height);

/**
 * Clear the pixel buffer to the given RGBA color.
 */
void soft_renderer_clear(SoftRenderer *sr, Color color);

/**
 * Execute all commands in dl, rendering into the pixel buffer.
 */
void soft_renderer_draw(SoftRenderer *sr, const __display_list__ *dl);

/**
 * Get a pointer to the raw RGBA8 pixel buffer.
 * Layout: row-major, 4 bytes per pixel (R, G, B, A).
 * The pointer is valid until the renderer is destroyed or resized.
 */
const uint8_t *soft_renderer_pixels(const SoftRenderer *sr);

/**
 * Save the current pixel buffer as a PPM image file.
 * PPM is a simple uncompressed format viewable in most image viewers.
 *
 * @param path  Output file path (e.g. "output.ppm").
 * @return      0 on success, -1 on file error.
 */
int soft_renderer_save_ppm(const SoftRenderer *sr, const char *path);

#endif /* HTMLUI_RENDER_SOFT_H */
