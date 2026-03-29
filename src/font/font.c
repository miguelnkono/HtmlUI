/**
 * src/font/font.c
 *
 * Milestone 4 — Font rendering (FreeType + HarfBuzz).
 *
 * Responsibilities:
 *   - Load TTF/OTF font files via FreeType
 *   - Rasterise glyph bitmaps at requested sizes
 *   - Maintain a glyph atlas (a GPU texture packed with rendered glyphs)
 *   - Use HarfBuzz for text shaping (kerning, ligatures, bidirectional text)
 *   - Provide glyph metrics (advance width, bearing) for layout
 *
 * The atlas is invalidated and rebuilt when a new font size is requested
 * that does not fit in the current atlas page.
 *
 * Not yet implemented. See README.md.
 */
