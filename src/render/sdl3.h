/**
 * src/render/sdl3.h
 *
 * SDL3 renderer interface.
 * Only available when HTMLUI_SDL3 is defined.
 */

#ifndef HTMLUI_RENDER_SDL3_H
#define HTMLUI_RENDER_SDL3_H

#ifdef HTMLUI_SDL3

#include "../internal/types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Sdl3Renderer_t Sdl3Renderer;

/**
 * Returns the window of the renderer
 * */
SDL_Window *sdl3_renderer_get_window(Sdl3Renderer *sr);

/** Create an SDL3 window + renderer. */
Sdl3Renderer *sdl3_renderer_create(const char *title, int w, int h,
                                   bool resizable, bool vsync);

void sdl3_renderer_destroy(Sdl3Renderer *sr);

/** Clear the frame to bg color. Call at start of each frame. */
void sdl3_renderer_begin_frame(Sdl3Renderer *sr, Color bg);

/** Execute all draw commands in the display list. */
void sdl3_renderer_draw(Sdl3Renderer *sr, const __display_list__ *dl);

/** Present the rendered frame. Call at end of each frame. */
void sdl3_renderer_end_frame(Sdl3Renderer *sr);

/**
 * Pump the SDL3 event queue.
 * Returns false when the window is closed or ESC is pressed.
 */
bool sdl3_poll_events(Sdl3Renderer *sr);

/** Get current window pixel dimensions. */
void sdl3_renderer_get_size(Sdl3Renderer *sr, int *w, int *h);

#endif /* HTMLUI_SDL3 */
#endif /* HTMLUI_RENDER_SDL3_H */
