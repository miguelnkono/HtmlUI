/**
 * src/paint/display_list.c
 *
 * Milestone 4 — Display list builder (layout tree → draw commands).
 *
 * Walks the layout tree in painter's order (back-to-front, z-index aware)
 * and emits a flat list of DrawCommand structs into a __display_list__.
 *
 * Each node produces 1–3 draw commands:
 *   1. CMD_RECT     — background fill + border
 *   2. CMD_TEXT     — text content (if any)
 *   3. CMD_IMAGE    — background-image or <img> src (future)
 *   4. CMD_CLIP_PUSH/POP — if overflow: hidden is set
 *
 * The __display_list__ is completely renderer-agnostic. The SDL3 and Vulkan
 * backends consume it independently, with no knowledge of the layout tree.
 *
 * Not yet implemented. Uncomment src/paint/display_list.c in CMakeLists.txt
 * when this milestone is ready.
 *
 * See README.md for the full milestone build order.
 */
