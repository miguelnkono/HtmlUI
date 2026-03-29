/**
 * src/api/ui_internal.h
 *
 * Full definition of the UI struct.
 * Only included by src/api/api.c.
 */

#ifndef HTMLUI_UI_INTERNAL_H
#define HTMLUI_UI_INTERNAL_H

#include "../../include/htmlui.h"
#include "../internal/types.h"
#include "../events/events.h"
#include "../render/soft.h"

#ifdef HTMLUI_SDL3
#  include "../render/sdl3.h"
#endif

struct UI_t {
    /* ── Pipeline state ───────────────────────────────────────────── */
    __html_node__*     dom_root;
    __css_rule_list__   css_rules;
    __display_list__   display_list;

    /* Paths — stored for hot-reload */
    char*         html_path;
    char*         css_path;

    /* ── Renderers ────────────────────────────────────────────────── */
    SoftRenderer* soft;          /* always available                  */

#ifdef HTMLUI_SDL3
    Sdl3Renderer* sdl3;          /* non-NULL when SDL3 is active      */
#endif

    /* ── Dimensions ───────────────────────────────────────────────── */
    int           window_w;
    int           window_h;

    /* ── Loop state ───────────────────────────────────────────────── */
    bool          running;
    bool          dirty;          /* needs re-layout + repaint         */

    /* ── Event state ──────────────────────────────────────────────── */
    EventState    events;

    /* ── Options ──────────────────────────────────────────────────── */
    HtmluiOptions options;

    /* ── Error buffer ─────────────────────────────────────────────── */
    char          last_error[512];
};

#endif /* HTMLUI_UI_INTERNAL_H */
