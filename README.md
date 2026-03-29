# htmlui

A native desktop UI toolkit that parses HTML and CSS and renders to a native
window using SDL3 or Vulkan. **No browser engine embedded.**

Application logic is written in C and interacts with the UI tree through a
clean public C API.

---

## Architecture

```
HTML/CSS files
     │
     ▼
┌─────────────┐
│  Layer 1    │  HTML parser  (html.c)  →  DOM tree
│  Parser     │  CSS parser   (css.c)   →  Rule list
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Layer 2    │  CSS cascade (cascade.c) →  ComputedStyle per node
│  Styles     │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Layer 3    │  Yoga/Flexbox (layout.c) →  LayoutBox per node
│  Layout     │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Layer 4    │  Display list builder   →  [CMD_RECT, CMD_TEXT, …]
│  Paint      │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Layer 5    │  SDL3 or Vulkan renderer → Native window pixels
│  Renderer   │
└──────┬──────┘
       │
       ▼  ←──────────────────────── Native app (C / Rust / Go)
┌─────────────┐                     calls the public C API
│  Layer 6    │  Public C API (api.c)
│  API        │  ui_load / ui_query / node_on_click / ui_render_frame …
└─────────────┘
```

---

## Build

### Prerequisites

- CMake 3.20+
- A C11 compiler (GCC, Clang, MSVC)

SDL3 and Yoga are required from Milestone 4 onward.
Milestone 1 (parser + types) has **no external dependencies**.

## License

MIT
