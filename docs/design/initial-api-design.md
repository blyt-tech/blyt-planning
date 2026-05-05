# Blyt — API Design Decisions

This document captures the API design decisions made for the Blyt project, including rationale and motivation. It assumes familiarity with the high-level design document (`docs/design/high-level-design.md`).

**Scope:** This document describes the **Blyt32** variant — the initial console focus (320×240 paletted, dpad+4face+2shoulder input, 2D scene-graph). The two sibling variants, **BlyTTY** (text-mode) and **Blyt3D** (far future), share the runtime infrastructure (RV32IMFC, Lua, state, audio, lifecycle) but vary the graphics and input surface; they will get their own API design documents when their work begins. See ADR-0105 for the variant model and naming.

---

## 1. Overall Shape and Conventions

### Handles

All runtime objects are represented as opaque `uint32_t` integer IDs. Zero is always invalid. The runtime owns all resources; carts and frontends never free memory directly.

```c
typedef uint32_t blyt_cart_h;
typedef uint32_t blyt_resource_h;
typedef uint32_t blyt_buffer_h;
typedef uint32_t blyt_image_h;
typedef uint32_t blyt_voice_h;
typedef uint32_t blyt_voice_group_h;
typedef uint32_t blyt_speech_h;
typedef uint32_t blyt_rng_h;
typedef uint32_t blyt_cycle_h;
typedef uint32_t blyt_layout_h;
typedef uint32_t blyt_field_h;
typedef uint32_t blyt_loc_key_t;
typedef uint32_t blyt_achievement_h;
typedef uint32_t blyt_tilemap_h;
typedef uint32_t blyt_tilemap_ref;  // accepts either blyt_resource_h or blyt_tilemap_h
```

**Rationale:** Opaque integer handles are safe to store in state buffers (which are POD), survive serialization across platforms, and don't expose internal memory layout. Pointers would break cross-platform save state portability.

### Error Handling

Functions that can fail return `blyt_result_t`. Successful calls that produce a value use out-parameters. A human-readable last error is always retrievable.

```c
blyt_result_t blyt_resource_load(blyt_resource_h resource);
const char *blyt_last_error(void);
blyt_result_t blyt_last_error_code(void);
```

**Rationale:** C doesn't have exceptions. Return codes are idiomatic, composable, and don't require try/catch infrastructure. Out-parameters keep function signatures consistent — functions always return a result code, never the value itself.

### Naming Convention

`blyt_<subsystem>_<verb>` throughout. Subsystem is optional for truly global operations. Consistent verb ordering: noun before action (`blyt_buffer_alloc`, not `blyt_alloc_buffer`).

### Two Headers

`blyt32.h` — everything a cart author includes. `blyt_runtime.h` — everything a frontend author uses to drive the core. Cart code never includes `blyt_runtime.h`; frontend code never needs `blyt32.h`.

### Flags

Bitmask flags (`uint32_t`) for C. Options tables for Lua. This is idiomatic in both languages and libretro targets C89/C99 broadly — compound literals and struct flags are not universally idiomatic in C game code.

```c
// C — bitmask
blyt_gfx_blit(image, ..., BLYT_BLIT_FLIP_H | BLYT_BLIT_OPAQUE);

// Lua — options table
gfx.blit(image, ..., { flip_h = true, opaque = true })
```

---

## 2. Frontend API (blyt_runtime.h)

The frontend API follows a **frontend-pulls** model: the frontend calls the runtime, reads results, and decides when to present. This contrasts with libretro's push model, but the libretro adapter translates between them trivially.

```c
// Lifecycle
blyt_result_t blyt_runtime_init(const blyt_runtime_config_t *config);
void        blyt_runtime_shutdown(void);
blyt_result_t blyt_runtime_play_chime(void);   // play startup chime + show splash, blocks until chime done
blyt_result_t blyt_runtime_show_splash(void);  // show splash without chime

blyt_result_t blyt_cart_load(const void *data, size_t size, blyt_cart_h *out);
void        blyt_cart_unload(blyt_cart_h cart);

// Frame loop
blyt_result_t blyt_cart_update(blyt_cart_h cart);
blyt_result_t blyt_cart_draw(blyt_cart_h cart);
uint32_t    blyt_cart_fps(blyt_cart_h cart);   // declared in cart.info, default 60

// Input (frontend pushes state before update)
blyt_result_t blyt_input_set_state(blyt_cart_h cart, uint32_t player, uint32_t buttons);

// Framebuffer (frontend reads after draw)
const uint8_t  *blyt_framebuffer_pixels(blyt_cart_h cart);  // 320*240 palette indices
const uint32_t *blyt_framebuffer_palette(blyt_cart_h cart); // 256 RGBA entries

// Audio
blyt_result_t blyt_audio_read(blyt_cart_h cart, int16_t *buf, size_t frames);

// Save state
blyt_result_t blyt_state_save(blyt_cart_h cart, void *buf, size_t *size);
blyt_result_t blyt_state_load(blyt_cart_h cart, const void *buf, size_t size);
size_t      blyt_state_size(blyt_cart_h cart);

// Events
typedef void (*blyt_event_fn)(blyt_cart_h cart, uint32_t event, const void *data, void *userdata);
blyt_result_t blyt_events_subscribe(blyt_cart_h cart, blyt_event_fn fn, void *userdata);
```

### Fixed-Timestep Ownership

The frontend owns the fixed-timestep accumulator, not the runtime. This is necessary because:

- Libretro calls `retro_run` at the declared fps and owns timing. The libretro adapter just calls `blyt_cart_update` once per `retro_run`.
- The SDL frontend runs its own accumulator loop.
- Carts can declare a framerate other than 60 (e.g. 35fps for a Doom port). `blyt_cart_fps` reads this from the cart manifest. The frontend adjusts its accumulator target accordingly.

`update` always receives `dt = 1/declared_fps`. No sub-rate rendering — update and draw always run together at the declared rate.

### Startup Chime

`blyt_runtime_play_chime` is called by the frontend before `blyt_cart_load`. It plays the runtime's branded startup chime and shows the splash screen, blocking until the chime finishes. The cart then loads while the splash remains visible, hiding loading latency on slow hardware.

The chime is a frontend decision — a screensaver frontend might call it once at application start and not per-cart; a kiosk might suppress it entirely. No manifest involvement.

`blyt_runtime_show_splash` decouples splash from chime for frontends that want splash but manage audio themselves.

### Libretro Adapter Shape

```c
void retro_run(void) {
    input_poll_cb();
    for (int p = 0; p < 4; p++) {
        blyt_input_set_state(cart, p, read_buttons_from_libretro(p));
    }
    blyt_cart_update(cart);
    blyt_cart_draw(cart);
    submit_framebuffer_to_libretro();  // video_refresh_cb
    blyt_audio_read(cart, audio_buf, frames);
    audio_batch_cb(audio_buf, frames);
}

size_t retro_serialize_size(void)              { return blyt_state_size(cart); }
bool   retro_serialize(void *buf, size_t size) { return blyt_state_save(cart, buf, &size) == BLYT_OK; }
bool   retro_unserialize(const void *buf, size_t size) { return blyt_state_load(cart, buf, size) == BLYT_OK; }
```

The adapter is thin (~400-500 lines). The API shape holds across all frontend types.

---

## 3. Cart Lifecycle

The runtime finds these as well-known exported symbols in the cart ELF. Native carts implement them directly; the Lua shim exports them and forwards into Lua.

```c
void blyt_cart_init(void);
void blyt_cart_update(void);
void blyt_cart_draw(void);
void blyt_cart_on_save(void);              // optional
void blyt_cart_on_load(void);             // optional
void blyt_cart_cleanup(void);             // optional
void blyt_cart_on_return_to_title(void);  // optional
void blyt_cart_on_credits(void);          // optional
void blyt_cart_on_quit(void);             // optional
void blyt_cart_panic(blyt_panic_reason_t reason);

blyt_result_t blyt_quit_ready(void);        // cart signals ready to quit
blyt_result_t blyt_credits_done(void);      // cart signals credits finished

typedef enum {
    BLYT_PANIC_WATCHDOG,       // update/draw exceeded time budget
    BLYT_PANIC_MEMORY,         // unrecoverable allocation failure
    BLYT_PANIC_ILLEGAL_INSN,   // illegal instruction (native carts)
    BLYT_PANIC_API_VIOLATION,  // cart misused the API fatally
} blyt_panic_reason_t;
```

### Design Decisions

**No `dt` parameter.** `update` takes no arguments. Carts get time via `blyt_time_frame()` — a deterministic frame counter. Passing `dt` would imply it could vary, which it cannot (fixed timestep). Consistent with determinism requirements.

**`blyt_cart_panic` is called by the runtime**, not by cart code. Cart gets a brief window to log diagnostic state before the runtime kills it. Only `blyt_log_*` functions are valid inside panic — the runtime may be in a bad state. Runtime kills the cart after a hard deadline regardless of whether panic returns.

**Optional callbacks use weak-linked SDK defaults.** The SDK provides empty weak-linked implementations of `on_save`, `on_load`, `cleanup`, `on_return_to_title`, `on_credits`, `on_quit`, and `panic`. Carts that don't need them don't define them.

**`blyt_cart_on_quit` / `blyt_quit_ready` pattern.** If `blyt_cart_on_quit` is defined, the runtime calls it instead of quitting immediately. Cart can show a save prompt, auto-save, clean up, then call `blyt_quit_ready`. If not defined, runtime shows a standard confirmation dialog and quits directly. Full save/load design deferred to a later design session.

---

## 4. Graphics

### Framebuffer Model

```c
blyt_result_t blyt_gfx_acquire(uint8_t **pixels);  // 320*240 palette indices
blyt_result_t blyt_gfx_present(void);
blyt_result_t blyt_gfx_clear(uint8_t color);
```

Carts call `acquire`, get a raw writable pointer, write freely into the 320×240 paletted buffer, then call `present`. No per-pixel API overhead in hot loops.

**Why no clip rectangle in the runtime.** Clip state persists between API calls but not between frames (save state is taken between frames, not mid-frame). Storing clip state in the runtime adds global mutable state that carts forget to reset, causing subtle bugs ("why is my HUD scrolling with the world?"). Raw buffer access bypasses the clip rect anyway, so the "safety" guarantee is partial. Cart-managed clipping is consistent with the "no hidden state" philosophy. SDK provides a clip helper for carts that want it.

**Why no camera offset in the runtime.** Same argument. Camera offset is trivial arithmetic the cart applies to its own coordinates. Runtime-owned camera state creates invisible global state that causes confusing bugs.

### Palette

```c
blyt_result_t blyt_gfx_palette_set(const uint32_t *rgba, uint32_t count, uint32_t offset);
blyt_result_t blyt_gfx_palette_get(uint32_t *rgba, uint32_t count, uint32_t offset);
blyt_result_t blyt_gfx_pal_remap(const uint8_t *map, uint32_t count);  // null to clear
```

Cart sets the palette in `blyt_cart_init`. No manifest declaration for default palette — keeps the manifest simpler and init code explicit. `blyt_gfx_pal_remap` provides per-draw color remapping (damage flashes, palette swaps, team colors) without modifying the palette permanently.

### Fill Pattern

```c
typedef uint16_t blyt_fillp_t;  // 4x4 bitmask

blyt_result_t blyt_gfx_fillp_set(blyt_fillp_t pattern);
blyt_result_t blyt_gfx_fillp_clear(void);

#define BLYT_FILLP_SOLID     0xFFFF
#define BLYT_FILLP_EMPTY     0x0000
#define BLYT_FILLP_CHECKER   0xAAAA
#define BLYT_FILLP_DITHER_25 0x1111
#define BLYT_FILLP_DITHER_50 0x5555
#define BLYT_FILLP_DITHER_75 0x7777
```

Applies to filled primitives (rect_fill, circ_fill, ellipse_fill). Enables dithering, hatching, crossfade effects — all cheap palette tricks that become tedious without runtime support. Inspired by PICO-8's `fillp` but genuinely useful beyond PICO-8 aesthetics.

### Screen Shake

```c
blyt_result_t blyt_gfx_shake(uint32_t duration_frames, float intensity, float falloff);
blyt_result_t blyt_gfx_shake_stop(void);
bool        blyt_gfx_shake_is_active(void);
```

Runtime applies shake as a pixel offset to the framebuffer at present time — cart renders normally to 320×240, runtime shifts output when presenting. Cart never sees the offset.

**Shake state is tracked in save state.** This was explicitly decided over treating it as presentation-only state. Rationale: palette cycling is state and survives save/restore; screen shake is no different. A restored game should look and feel identical to what would have happened without the restore. Shake state is small (4 floats) and trivially serialized.

**Replace-only.** New shake always replaces current. No compounding. Simpler model; carts that want compounding can track it themselves and call `blyt_gfx_shake` with accumulated values.

### Primitives

```c
// Single
blyt_result_t blyt_gfx_pixel(int32_t x, int32_t y, uint8_t color);
blyt_result_t blyt_gfx_pget(int32_t x, int32_t y, uint8_t *out);
blyt_result_t blyt_gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color);
blyt_result_t blyt_gfx_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
blyt_result_t blyt_gfx_rect_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
blyt_result_t blyt_gfx_circ(int32_t x, int32_t y, int32_t r, uint8_t color);
blyt_result_t blyt_gfx_circ_fill(int32_t x, int32_t y, int32_t r, uint8_t color);
blyt_result_t blyt_gfx_ellipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint8_t color);
blyt_result_t blyt_gfx_ellipse_fill(int32_t x, int32_t y, int32_t rx, int32_t ry, uint8_t color);

// Batch (single API call for N elements — meaningful performance win)
blyt_result_t blyt_gfx_pixels(const int32_t *xy, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_lines(const int32_t *xy, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_rects(const int32_t *xywh, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_rects_fill(const int32_t *xywh, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_circs(const int32_t *xyr, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_circs_fill(const int32_t *xyr, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_ellipses(const int32_t *xyrxry, uint32_t count, uint8_t color);
blyt_result_t blyt_gfx_ellipses_fill(const int32_t *xyrxry, uint32_t count, uint8_t color);

// Framebuffer ops
blyt_result_t blyt_gfx_copy(int32_t src_x, int32_t src_y, int32_t w, int32_t h,
                         int32_t dst_x, int32_t dst_y);
blyt_result_t blyt_gfx_fade(uint8_t target_color, float t);
blyt_result_t blyt_gfx_palette_lerp(const uint32_t *pal_a, const uint32_t *pal_b,
                                  float t, uint32_t *out, uint32_t count);
uint8_t     blyt_gfx_nearest_color(uint32_t rgba);
```

**Ellipses included** (not just circles). Common use: shadow blobs under sprites, health bars, map indicators. Cost to implement is low; cost to omit is carts writing it into the raw buffer.

**Batch variants included.** Drawing 1000 particles as one call is meaningfully faster than 1000 separate calls. Inspired by SDL2's batch draw functions.

**`blyt_gfx_fade` and `blyt_gfx_palette_lerp`** leverage palette-indexed effects being essentially free — manipulating 256 entries rather than touching every pixel. The design doc explicitly calls this out as a feature of paletted graphics.

### Images

```c
// Loading
blyt_result_t blyt_image_load(blyt_resource_h resource, blyt_image_h *out);      // read-only
blyt_result_t blyt_image_load_rw(blyt_resource_h resource, blyt_image_h *out);   // read-write copy
blyt_result_t blyt_image_alloc(uint32_t w, uint32_t h, blyt_image_h *out);     // fresh writable

// Info and lifecycle
blyt_result_t blyt_image_size(blyt_image_h image, uint32_t *w, uint32_t *h);
blyt_result_t blyt_image_free(blyt_image_h image);

// Raw access
blyt_result_t blyt_image_acquire(blyt_image_h image, uint8_t **pixels);
blyt_result_t blyt_image_release(blyt_image_h image);

// Pixel ops
blyt_result_t blyt_image_clear(blyt_image_h image, uint8_t color);
blyt_result_t blyt_image_pget(blyt_image_h image, int32_t x, int32_t y, uint8_t *out);
blyt_result_t blyt_image_pixel(blyt_image_h image, int32_t x, int32_t y, uint8_t color);
blyt_result_t blyt_image_pal_remap(blyt_image_h image, const uint8_t *map, uint32_t count);
```

**`blyt_image_load` vs `blyt_image_load_rw`.** Intent is explicit at the call site. Read-only images are zero-copy (pointer into cart resource data). Read-write makes a copy into working memory. Calling `blyt_image_acquire` on a read-only image returns `BLYT_ERR_RESOURCE_READ_ONLY` — clear error, obvious fix. Dev mode adds a hint: "use blyt_image_load_rw if you need to modify this image."

**Image memory counts against the cart's 16MB working budget.** Read-only images show as zero allocation since they're already counted as resource cache.

**Unified `blyt_image_h` type for loaded resources and offscreen buffers.** Both are paletted pixel buffers — all image operations work uniformly regardless of source. Runtime detects font vs image resource at load time and adds appropriate metatable (Lua) or behaves accordingly (C).

### Blit

```c
typedef uint32_t blyt_blit_flags_t;
#define BLYT_BLIT_NONE   0
#define BLYT_BLIT_FLIP_H (1 << 0)
#define BLYT_BLIT_FLIP_V (1 << 1)
#define BLYT_BLIT_OPAQUE (1 << 2)   // skip transparency check, treat 255 as solid

blyt_result_t blyt_gfx_blit(blyt_image_h image,
    int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h,
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    blyt_blit_flags_t flags);

blyt_result_t blyt_gfx_blit_frame(blyt_image_h image,
    uint32_t frame, int32_t frame_w, int32_t frame_h,
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    blyt_blit_flags_t flags);

typedef struct {
    int32_t src_x, src_y, src_w, src_h;
    int32_t dst_x, dst_y, dst_w, dst_h;
    blyt_blit_flags_t flags;
} blyt_blit_entry_t;

blyt_result_t blyt_gfx_blit_batch(blyt_image_h image,
    const blyt_blit_entry_t *entries, uint32_t count);

blyt_result_t blyt_gfx_blit_image(blyt_image_h image,
    int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h,
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    blyt_blit_flags_t flags);

blyt_result_t blyt_image_blit_to(blyt_image_h src, blyt_image_h dst,
    int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h,
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    blyt_blit_flags_t flags);
```

**Transparency via packer convention.** Palette index 255 is always transparent. The packer processes images and ensures a consistent transparent index — no per-call transparency argument, no manifest declaration. Cart authors work with PNGs that have alpha; packer remaps to 255 for the transparent pixels.

**`BLYT_BLIT_OPAQUE` flag.** Skip the 255 transparency check entirely. Just a branch skipped in the inner blit loop — cheap enough to include. Useful for blitting backgrounds or any image known to have no transparent pixels.

**Scaling included in v1.** `dst_w`/`dst_h` of 0,0 means no scaling (use src dimensions). Nearest-neighbor scaling only — consistent with pixel art aesthetic. Cheap to implement. Common enough use case (UI elements at different sizes, boss sprites, screen transitions) to justify inclusion.

**`blyt_gfx_blit_frame`.** Addressing helper for sprite sheets — "draw frame N from this uniform grid." Cart owns animation state (which frame, timing, state machine). This just handles the geometry. Not an animation system.

**`img:blit_to(target, ...)` naming in Lua.** Subject is the source, argument is the destination — reads naturally as "blit this image to that target."

### Tilemap

```c
blyt_result_t blyt_gfx_tilemap_draw(blyt_tilemap_ref tilemap, blyt_resource_h tileset,
    int32_t scroll_x, int32_t scroll_y,
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    blyt_blit_flags_t flags);

blyt_result_t blyt_gfx_tilemap_get(blyt_tilemap_ref tilemap,
    int32_t tile_x, int32_t tile_y, uint16_t *out);
```

`blyt_tilemap_ref` accepts either `blyt_resource_h` (immutable) or `blyt_tilemap_h` (mutable) — both drawn and queried identically. See Mutable Tilemaps section.

### Text

```c
typedef enum {
    BLYT_TEXT_ALIGN_LEFT,
    BLYT_TEXT_ALIGN_CENTER,
    BLYT_TEXT_ALIGN_RIGHT,
} blyt_text_align_t;

typedef enum {
    BLYT_TEXT_BORDER_NONE,
    BLYT_TEXT_BORDER_4,      // cardinal directions — lighter look
    BLYT_TEXT_BORDER_8,      // all 8 directions — fuller, more legible
    BLYT_TEXT_BORDER_SHADOW, // bottom-right only — subtle depth
} blyt_text_border_t;

typedef struct {
    blyt_image_h       font;
    int32_t          x, y;
    int32_t          max_w, max_h;
    int32_t          line_spacing;
    int32_t          padding;            // inner padding between text and background
    int32_t          margin;             // transparent border outside background
                                         // auto_reposition respects this
    uint8_t          color;
    uint8_t          border_color;
    blyt_text_border_t border_style;
    uint8_t          background_color;   // 255 = no background
    blyt_text_align_t  align;
    bool             auto_reposition;    // nudge box to stay on screen
} blyt_text_params_t;

typedef struct {
    int32_t  x, y;           // actual draw position after reposition
    uint32_t lines_drawn;
    uint32_t chars_drawn;    // for typewriter effect / pagination
} blyt_text_result_t;

blyt_result_t blyt_gfx_text(blyt_image_h font, int32_t x, int32_t y,
    const char *str, uint8_t color);
blyt_result_t blyt_gfx_text_draw(const blyt_text_params_t *params,
    const char *str, blyt_text_result_t *result);
blyt_result_t blyt_gfx_text_size(blyt_image_h font, const char *str,
    int32_t max_w, int32_t line_spacing, uint32_t *w, uint32_t *h);
```

**Params struct approach.** Handles the combinatorial explosion of text options cleanly — no function with 12 arguments. Easy to extend without breaking existing call sites. Cart initializes a struct, sets what it needs, passes it in.

**Word wrap on spaces only.** No hyphenation. Sufficient for game dialog; hyphenation adds complexity for marginal benefit.

**`chars_drawn` out parameter.** Enables typewriter effect and pagination without reimplementing text layout. Cart advances `chars_visible` each frame; runtime draws only that many characters.

**`auto_reposition`.** Runtime nudges the box inward to keep it on screen. Uses `margin` for breathing room — box stays `margin` pixels from screen edge. Cart can draw a speech bubble tail or pointer arrow using `result.x/y` to know the actual draw position.

**Spatial model:**
```
|-- margin --|-- padding --|-- text --|-- padding --|-- margin --|
```

**LucasArts-style bordered text.** `border_style` with `BLYT_TEXT_BORDER_4` and `BLYT_TEXT_BORDER_8` options. Renders glyph in `border_color` at offsets, then main glyph in `color` on top. Dramatically improves legibility over complex backgrounds without requiring a background box. Classic technique from LucasArts adventure games.

**Background color.** 255 = no background. Allows drawing text bubbles over an already-rendered scene.

### Font Format

Internal format is BMFont for all fonts — fixed-width grids are a degenerate case where every glyph has identical advance width and no kerning.

**Packer accepts:**
- PNG grid + optional width table → generates BMFont descriptor
- BMFont `.fnt` + atlas PNG → validates and repackages
- TTF/OTF → rasterizes at specified sizes to BMFont at build time

**TTF as input-only.** Authors work with TTF (huge font library, familiar tools). Runtime only sees bitmaps. Packer handles palette quantization. No FreeType dependency in runtime.

**Variable-width fonts.** BMFont format supports per-glyph advance widths and kerning pairs. Runtime has one font renderer; one code path; no special cases between fixed and variable.

**Charset defaults to ASCII.** Authors who need extended characters opt in explicitly — the friction is appropriate because the cost (larger atlas, more memory) is real. Packer warns if cart code references a character not in the declared charset (best-effort static analysis). Runtime shows placeholder glyph for missing characters.

**Font constants generated as resources.** `R.FONT_DIALOG`, `R.FONT_HUD` etc. Runtime resource constants in `FC.*` namespace. Fonts loaded via `blyt_image_load` — runtime detects font type and adds text methods.

---

## 5. Input

### Button State

```c
typedef enum {
    BLYT_BTN_UP     = 1 << 0,
    BLYT_BTN_DOWN   = 1 << 1,
    BLYT_BTN_LEFT   = 1 << 2,
    BLYT_BTN_RIGHT  = 1 << 3,
    BLYT_BTN_A      = 1 << 4,
    BLYT_BTN_B      = 1 << 5,
    BLYT_BTN_X      = 1 << 6,
    BLYT_BTN_Y      = 1 << 7,
    BLYT_BTN_L      = 1 << 8,
    BLYT_BTN_R      = 1 << 9,
    BLYT_BTN_START  = 1 << 10,
    BLYT_BTN_SELECT = 1 << 11,
} blyt_button_t;

bool     blyt_input_button_down(uint32_t player, blyt_button_t btn);
bool     blyt_input_button_pressed(uint32_t player, blyt_button_t btn);
bool     blyt_input_button_released(uint32_t player, blyt_button_t btn);
uint32_t blyt_input_buttons(uint32_t player);
bool     blyt_input_button_repeat(uint32_t player, blyt_button_t btn,
             uint32_t delay_frames, uint32_t interval_frames);

bool     blyt_input_is_connected(uint32_t player);
uint32_t blyt_input_local_player(void);  // for netplay per-player rendering

#define BLYT_INPUT_REPEAT_DELAY    30   // half second at 60fps
#define BLYT_INPUT_REPEAT_INTERVAL 6    // 10 repeats per second
```

**Edge detection.** `pressed`/`released` are relative to the current frame snapshot. Runtime computes by diffing consecutive snapshots — carts don't manage this themselves.

**Button repeat.** `blyt_input_button_repeat` with delay and interval — for menu navigation. Standard provided defaults cover most cases.

### Device Info (Cosmetic Only)

```c
typedef enum {
    BLYT_DEVICE_KIND_UNKNOWN,
    BLYT_DEVICE_KIND_KEYBOARD,
    BLYT_DEVICE_KIND_GAMEPAD,
} blyt_device_kind_t;

typedef enum {
    BLYT_DEVICE_FAMILY_UNKNOWN,
    BLYT_DEVICE_FAMILY_KEYBOARD,
    BLYT_DEVICE_FAMILY_GENERIC,
    BLYT_DEVICE_FAMILY_AB,          // Xbox-style: A bottom, B right
    BLYT_DEVICE_FAMILY_AB_REVERSED, // Nintendo-style: B bottom, A right
    BLYT_DEVICE_FAMILY_SHAPES,      // PlayStation-style: geometric face buttons
} blyt_device_family_t;

blyt_device_kind_t   blyt_input_device_kind(uint32_t player);
blyt_device_family_t blyt_input_device_family(uint32_t player);
const char        *blyt_input_button_label(uint32_t player, blyt_button_t btn);
```

**No trademarked names.** `BLYT_DEVICE_FAMILY_AB`, `BLYT_DEVICE_FAMILY_AB_REVERSED`, `BLYT_DEVICE_FAMILY_SHAPES` avoid PlayStation/Xbox/Nintendo trademark exposure while being descriptive. Carts use these for icon sprite selection and button label display only — never for capability detection.

**Capability info not exposed.** Carts cannot meaningfully branch on "this controller has more buttons." The console's input spec is fixed and unconditional. This keeps the input contract stable across all hardware.

### Pointer

```c
bool blyt_input_pointer_held(void);
bool blyt_input_pointer_pressed(void);
bool blyt_input_pointer_released(void);
void blyt_input_pointer_position(int32_t *x, int32_t *y);
```

Single pointer abstraction — mouse on desktop, first touch on mobile, frontend-provided on libretro. No player index; pointer belongs to local player implicitly. Multi-touch deferred to v2 (would be additive, doesn't change single-touch API).

### Text Input

```c
typedef enum {
    BLYT_TEXT_INPUT_HOST_ONLY,    // only host enters text, result broadcast to all
    BLYT_TEXT_INPUT_ALL_PLAYERS,  // all players enter simultaneously
} blyt_text_input_mode_t;

typedef enum {
    BLYT_TEXT_INPUT_CANCEL_PROPAGATES,  // any cancel = all cancelled
    BLYT_TEXT_INPUT_CANCEL_AS_EMPTY,    // cancel submits empty string
} blyt_text_input_cancel_t;

typedef struct {
    const char             *prompt;
    const char             *initial;              // pre-fill value
    uint32_t                max_len;
    uint32_t                flags;
    blyt_text_input_mode_t    mode;
    blyt_text_input_cancel_t  cancel;
    const char             *waiting_prompt;       // HOST_ONLY: shown to non-host
    const char             *waiting_others_prompt; // ALL_PLAYERS: shown after local confirms
} blyt_text_input_params_t;

typedef enum {
    BLYT_TEXT_INPUT_PENDING,
    BLYT_TEXT_INPUT_CONFIRMED,
    BLYT_TEXT_INPUT_CANCELLED,
} blyt_text_input_state_t;

blyt_result_t           blyt_input_text_request(const blyt_text_input_params_t *params);
blyt_text_input_state_t blyt_input_text_state(void);
const char           *blyt_input_text_result(void);
const char           *blyt_input_text_result_player(uint32_t player);
```

**Blocking model.** Cart calls `blyt_input_text_request` during `update`. Runtime suspends frame loop after next `draw`. Shows last rendered frame as background with soft keyboard overlaid. Resumes when all relevant players confirm or cancel. Next `update` sees CONFIRMED or CANCELLED state.

**Two modes.** `HOST_ONLY` — one player decides, result broadcast to all (game settings, difficulty). `ALL_PLAYERS` — everyone enters simultaneously (player names, character customization). ALL_PLAYERS resumes when all players have confirmed or cancelled.

**Netplay constraint.** Text input returns `BLYT_ERR_NOT_AVAILABLE` during active netplay sessions. V1 limitation; v2 will revisit with proper input-injection approach. Carts should design name entry in pre-game menus using `ALL_PLAYERS` mode — all machines prompt simultaneously, resume when all players done, simulation stays locked.

**Pre-fill.** `initial` parameter allows pre-filling with previous value — returning players don't retype their names.

**Runtime-rendered soft keyboard.** Uses console's identity visual language — same theming as pause menu and settings UI. On mobile, invokes native soft keyboard via hidden HTML input. On TV/gamepad-only setups, shows navigable on-screen keyboard. Cart never manages input method.

**Waiting screens.** `waiting_prompt` shown to non-host in HOST_ONLY mode. `waiting_others_prompt` shown to players who have confirmed but are waiting for others in ALL_PLAYERS mode. Runtime shows who has confirmed and who hasn't.

---

## 6. Audio

```c
typedef uint32_t blyt_voice_h;
typedef uint32_t blyt_voice_group_h;

// Music (single channel — one tracker module at a time)
blyt_result_t blyt_audio_music_play(blyt_resource_h module, bool loop);
blyt_result_t blyt_audio_music_stop(void);
blyt_result_t blyt_audio_music_pause(void);
blyt_result_t blyt_audio_music_resume(void);
blyt_result_t blyt_audio_music_volume(float volume);
blyt_result_t blyt_audio_music_mute_channel(uint32_t channel, bool mute);
blyt_result_t blyt_audio_music_mute_channels(uint32_t bitmask);
bool        blyt_audio_music_is_playing(void);

// SFX
blyt_result_t blyt_audio_sfx_play(blyt_resource_h sfx, blyt_voice_h *out);
blyt_result_t blyt_audio_sfx_stop(blyt_voice_h voice);
blyt_result_t blyt_audio_sfx_volume(blyt_voice_h voice, float volume);
blyt_result_t blyt_audio_sfx_pan(blyt_voice_h voice, float pan);     // -1.0 left, 1.0 right
blyt_result_t blyt_audio_sfx_pitch(blyt_voice_h voice, float pitch); // 1.0 = normal
bool        blyt_audio_sfx_is_playing(blyt_voice_h voice);

// Streams (Large/Flagship only)
blyt_result_t blyt_audio_stream_play(blyt_resource_h stream, bool loop);
blyt_result_t blyt_audio_stream_stop(void);
bool        blyt_audio_stream_is_playing(void);

// Voice groups (manifest-declared)
blyt_result_t blyt_audio_group_volume(blyt_voice_group_h group, float volume);
blyt_result_t blyt_audio_group_pan(blyt_voice_group_h group, float pan);
blyt_result_t blyt_audio_group_stop(blyt_voice_group_h group);
blyt_result_t blyt_audio_group_pause(blyt_voice_group_h group);
blyt_result_t blyt_audio_group_resume(blyt_voice_group_h group);

// Voice tagging (scene-local batch stopping)
blyt_result_t blyt_audio_sfx_play_tagged(blyt_resource_h sfx, uint32_t tag, blyt_voice_h *out);
blyt_result_t blyt_audio_stop_tag(uint32_t tag);

// Master
blyt_result_t blyt_audio_master_volume(float volume);
```

**Single music channel.** Only one tracker module playing at a time. Multiple simultaneous music tracks is not a supported use case. Tracker channel muting (`blyt_audio_music_mute_channel`) provides the compositing needed for atmosphere variation without multiple tracks — composers design modules with this in mind (common tracker technique, "stems via channels").

**`is_playing` queries actual mixer state.** The design doc originally proposed frame-count-based playback queries for determinism. This was rejected as taking determinism too far — audio completion is observable state the cart reasonably expects to query, forcing frame arithmetic is cumbersome and error-prone in edge cases. If fast-forward causes audio to finish early, the cart sees that and can adapt.

**Pitch shifting via resampling.** Useful for variety — same SFX at slightly randomized pitch avoids the "machine gun" effect of repeated identical sounds.

**Voice groups vs tags.** Two complementary mechanisms:
- **Groups** (manifest-declared, persistent) — for mixer categories (music, sfx, ambient, ui) and player settings. State saved/restored.
- **Tags** (runtime, per-voice) — for scene-local batch stopping ("stop all sounds tagged with SCENE_CAVE on transition"). No manifest declaration needed.

**Audio at non-1x speed (fast-forward).** Music plays at wrong pitch at 2x+ speed — known libretro fast-forward artifact that every core deals with. Documented as a known limitation.

**Three layers of volume state:**
1. **Frontend-owned master volume** — applies across all carts, invisible to cart
2. **Cart preferences** — per-cart player settings via `blyt_prefs`, read at init and applied to groups
3. **In-game audio state** — transient ducking, cutscene volume changes, reapplied from game state on load

---

## 7. Speech and Lip Sync

```c
typedef uint32_t blyt_speech_h;

blyt_result_t blyt_speech_play(blyt_resource_h speech, blyt_speech_h *out);
blyt_result_t blyt_speech_stop(blyt_speech_h speech);
bool        blyt_speech_is_playing(blyt_speech_h speech);
blyt_result_t blyt_speech_mouth_shape(blyt_speech_h speech, char *out_shape);
// BLYT_ERR_NOT_FOUND if no lip sync data packed for this clip
// BLYT_ERR_SPEECH_NO_LIP_SYNC = 1200
```

### Lip Sync Pipeline

**Rhubarb Lip Sync** is the supported tool — open source (MIT), takes audio + optional dialog text, outputs timed mouth shape data (Preston Blair set: X, A, B, C, D, E, F, G, H where X = rest).

**Packer drives Rhubarb.** Packer detects speech audio files, checks for paired JSON by name (`greeting.wav` → `greeting.json`), runs Rhubarb if JSON absent. Rhubarb path configured in `.console/config.toml`.

**Packer behavior:**
- Rhubarb found, no JSON → run Rhubarb, cache JSON, pack binary
- Rhubarb found, JSON exists → use existing JSON, pack binary
- Rhubarb not found, no JSON → warn, pack audio without lip sync
- Rhubarb not found, JSON exists → use existing JSON, pack binary

**Binary format at pack time.** Packer reads Rhubarb JSON (which is time-based) and converts to frame-based binary at the cart's declared fps. Runtime reads binary only — no JSON parser in runtime.

**Lip sync data embedded in speech resource.** Single `RES_SPEECH_GREETING` handle. `blyt_speech_mouth_shape` returns current shape for playing clip.

**Opt-in by file presence.** No manifest declaration. If JSON exists → lip sync active. If not → `blyt_speech_mouth_shape` returns `BLYT_ERR_SPEECH_NO_LIP_SYNC`, mouth stays at rest pose. Safe default; development workflow is natural — add JSON when ready.

**Locale interaction.** Speech resources resolve to locale-correct audio at play time. In-flight voices hold a reference to their audio data (not the locale-resolved handle), so locale changes don't affect playing clips. `blyt_speech_is_playing` works correctly across locale switches. German lines may be longer than English — `blyt_speech_is_playing` waits for the actual playing clip regardless of locale, making it naturally locale-safe for dialog advancement.

**Lip sync watch-mode.** In watch mode, packer runs Rhubarb on changed audio files automatically and hot-reloads.

---

## 8. State Buffers

State buffers are the persistence mechanism for all cart-observable game state. They enable save states, rewind, replay, and netplay as structural properties.

### Declaration

Buffers are declared in `cart.config` manifest — never allocated in cart code. Packer generates constants:

```c
// generated/cart_state.h
#define S_PLAYER   ((blyt_buffer_h)0)
#define S_ENEMIES  ((blyt_buffer_h)1)

// Field constants per layout
#define S_ENEMY_X           ((blyt_field_h)0)
#define S_ENEMY_Y           ((blyt_field_h)1)
#define S_ENEMY_HP          ((blyt_field_h)2)
#define S_ENEMY_WAYPOINTS_X ((blyt_field_h)3)
```

**Rationale for manifest declaration.** Carts that don't declare get no save state, rewind, or netplay. The cost of not declaring is high enough to push authors toward correct behavior. Retrofitting is painful — state buffer discipline shapes how you structure your whole game. Opt-out via `stateless = true` in manifest for carts that genuinely don't need these features (Demo-class non-interactive carts, pure generative pieces).

**Named identity for save compatibility.** Save format uses layout name as canonical identity, not ordinal index. Adding layouts is non-breaking. Removing layouts → old saves have data with nowhere to go, runtime warns and drops. Renaming is breaking (same as renaming a field).

### Field Constants

Field names use `blyt_field_h` integer constants, not `const char*`. Generated by packer alongside buffer constants. Benefits: compiler checking, autocomplete, no runtime string lookups, typos fail at compile time.

```c
#define BLYT_FIELD_NONE ((blyt_field_h)0xFFFFFFFF)  // sentinel for optional field params
```

### Buffer API

```c
uint32_t    blyt_buffer_count(blyt_buffer_h buf);
uint32_t    blyt_buffer_active_count(blyt_buffer_h buf);
blyt_result_t blyt_buffer_field(blyt_buffer_h buf, blyt_field_h field,
                void **out, uint32_t *stride);
uint32_t    blyt_buffer_field_count(blyt_buffer_h buf, blyt_field_h field);
                // returns 1 for scalar, N for array fields
blyt_result_t blyt_buffer_clear(blyt_buffer_h buf);
blyt_result_t blyt_buffer_copy(blyt_buffer_h dst, blyt_buffer_h src);
bool        blyt_buffer_was_restored(blyt_buffer_h buf);

// Active slot management
bool        blyt_buffer_is_active(blyt_buffer_h buf, uint32_t index);
blyt_result_t blyt_buffer_set_active(blyt_buffer_h buf, uint32_t index, bool active);
blyt_result_t blyt_buffer_alloc_slot(blyt_buffer_h buf, uint32_t *out);
blyt_result_t blyt_buffer_free_slot(blyt_buffer_h buf, uint32_t index);

// Iteration
blyt_result_t blyt_buffer_iter_begin(blyt_buffer_h buf, uint32_t *out);
blyt_result_t blyt_buffer_iter_next(blyt_buffer_h buf, uint32_t current, uint32_t *out);
```

**Active count built in.** Entity pools (enemies, particles, projectiles) almost universally need active count and "find me a free slot." Building this into the runtime saves every cart from reimplementing it.

**Eager slot search.** Runtime finds the next free slot during frame boundaries when there's spare time. `blyt_buffer_alloc_slot` reads pre-computed result rather than scanning. Degrades gracefully to scan if pre-computed slot is taken.

**Active flags in save state.** They're part of cart-observable state — a restored game should have the same active entities.

**Dev overlay shows per-buffer stats automatically:**
```
STATE_ENEMIES    47 / 200 active
STATE_PARTICLES  312 / 1000 active
```

**`blyt_buffer_was_restored`.** Only meaningful inside `blyt_cart_on_load`. Returns true if buffer's data came from save, false if default-initialized (layout wasn't in save file). Lets cart handle its own migration: "this save predates the quest system — initialize sensibly."

### Layout Field Types

Scalar: `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `f32`, `bool`.

Fixed arrays — 1D only, same primitive types, max 256 elements:
```yaml
# cart.config.yaml — under layouts:
Enemy:
  waypoints_x: { type: f32, count: 8 }
  inventory:   { type: i32, count: 16 }
```

No nested layouts. No dynamic arrays. POD discipline enables save state via essentially memcpy of tracked regions.

---

## 9. Resources

### Compile-Time Constants

Resources are never referenced by string at runtime. Packer generates headers:

```c
// generated/cart_resources.h — build artifact, not committed
#define R_HERO_SPRITES   ((blyt_resource_h)1)
#define R_MUSIC_THEME    ((blyt_resource_h)2)
#define R_SFX_JUMP       ((blyt_resource_h)3)
```

```lua
-- generated/cart_resources.lua
R = {}
R.HERO_SPRITES = 1
R.MUSIC_THEME  = 2
R.SFX_JUMP     = 3
```

Runtime resource constants (high bit set, stable forever):
```c
// blyt_resources.h — ships with SDK, never changes
#define BLYT_FONT_BUILTIN    ((blyt_resource_h)0x80000000)
#define BLYT_FONT_SMALL      ((blyt_resource_h)0x80000001)
#define BLYT_PALETTE_DEFAULT ((blyt_resource_h)0x80000003)
```

**Rationale.** String lookup at runtime is slow and typo-unsafe. Compile-time constants give compiler checking, autocomplete, and zero-cost lookup. High bit for runtime constants prevents collision with cart-allocated IDs.

**ID stability within dev session.** Packer only renumbers if resources are added or removed. Adding → appended at end, existing IDs unchanged. Removing → ID retired, gap left. Renaming → ID unchanged, constant name updated. Compaction only on `console bundle` or explicit `console renumber`.

**Generated files are build artifacts.** Gitignored; regenerated every build. Not committed because they're derived from the project structure.

### Resource API

```c
blyt_result_t blyt_resource_load(blyt_resource_h resource);
blyt_result_t blyt_resource_release(blyt_resource_h resource);
bool        blyt_resource_is_loaded(blyt_resource_h resource);
blyt_result_t blyt_resource_data(blyt_resource_h resource,
                const void **data, size_t *size);
```

No `blyt_resource_load` by string — handles are compile-time constants. `blyt_resource_load` by handle triggers loading of a non-persistent resource.

**Persistent resources** declared in manifest are auto-loaded at init and never evicted. Explicit load/release for transient resources.

**Handle stability across save/restore.** Resource handles encode logical identity (resource name + load generation), not physical address. Save state preserves which resources were loaded; restore re-establishes the physical mapping.

---

## 10. Mutable Tilemaps

Tilemaps that change at runtime (doors opening, destructible terrain, building placement) need per-instance state that survives save/restore. Immutable tilemaps are just resources.

```c
typedef uint32_t blyt_tilemap_h;
typedef uint32_t blyt_tilemap_ref;  // accepts blyt_resource_h or blyt_tilemap_h

// Mutable tilemaps declared in manifest, accessed via generated constants
// TILEMAP_WORLD, TILEMAP_DUNGEON etc.

blyt_result_t blyt_tilemap_set(blyt_tilemap_h tilemap,
    uint32_t tx, uint32_t ty, uint16_t tile_id);
blyt_result_t blyt_tilemap_get(blyt_tilemap_h tilemap,
    uint32_t tx, uint32_t ty, uint16_t *out);

// Per-instance tile flags
blyt_result_t blyt_tilemap_set_tile_flags(blyt_tilemap_h tilemap,
    uint32_t tx, uint32_t ty, uint32_t flags);
blyt_result_t blyt_tilemap_clear_tile_flags(blyt_tilemap_h tilemap,
    uint32_t tx, uint32_t ty);
uint32_t    blyt_tilemap_get_tile_flags(blyt_tilemap_h tilemap,
    uint32_t tx, uint32_t ty);  // instance flags if set, type flags otherwise
```

**Per-instance flags** (not per-type). Most non-trivial games need individual doors, switches, destructible blocks with independent state. Per-type flags would make `blyt_tile_set_flags(TILE_DOOR, 0)` open ALL doors.

**Save format.** Mutable tilemap state saved as a diff against the original resource — only changed cells stored. Compact for typical use (a few dozen changed tiles in a dungeon).

**Tile type flags** (global, per tile ID) separate from instance flags:

```c
#define BLYT_TILE_SOLID        (1 << 0)
#define BLYT_TILE_ONE_WAY_UP   (1 << 1)
#define BLYT_TILE_ONE_WAY_DOWN (1 << 2)
#define BLYT_TILE_TRIGGER      (1 << 3)
#define BLYT_TILE_SLOW         (1 << 4)

blyt_result_t blyt_tile_set_flags(uint16_t tile_id, uint32_t flags);
blyt_result_t blyt_tile_set_flags_range(uint16_t start, uint16_t end, uint32_t flags);
uint32_t    blyt_tile_get_flags(uint16_t tile_id);
```

Tile type flags registered at init are tracked in save state — a door that was opened stays opened after restore.

---

## 11. Save and Preferences

### Three-Layer Model

| Layer | Owner | API | Scope | Survives |
|-------|-------|-----|-------|----------|
| Platform settings | Frontend | Frontend UI | All carts | Everything |
| Cart preferences | Cart | `blyt_prefs` | This cart | Restarts, updates, save slots |
| Game state | Cart | State buffers | This save | Save/load cycle |

**Rule:** if a player would be annoyed to reconfigure it after a cart restart, it's a preference. If it changes during play and should reset on load, it's game state.

**Frontend master volume** is applied transparently at mixer output — cart never sees it.

**Cart preferences** (audio volumes, subtitles, control scheme) read at init, applied to groups, persisted via `blyt_prefs`. Independent of save slots.

**In-game audio state** (boss fight ducking, cutscene muffling) is transient — reapplied from game state on load, not persisted separately.

### Cart Saves

```c
typedef struct {
    char     slot[64];
    char     label[256];
    uint32_t size;
    uint32_t timestamp;  // frame count at save time, not wall clock
} blyt_save_info_t;

blyt_result_t blyt_save_write(const char *slot, const void *data,
                size_t size, const char *label);
blyt_result_t blyt_save_read(const char *slot, void *data, size_t *size);
blyt_result_t blyt_save_delete(const char *slot);
blyt_result_t blyt_save_exists(const char *slot, bool *out);
blyt_result_t blyt_save_list(blyt_save_info_t *out, uint32_t *count);
size_t      blyt_save_quota_remaining(void);
```

**Timestamp as frame count** (not wall clock) — consistent with determinism. Per-cart quota (default 10MB). Atomic writes (write-then-rename) to prevent corruption.

### Preferences

```c
blyt_result_t blyt_prefs_set_float(const char *key, float value);
blyt_result_t blyt_prefs_set_int(const char *key, int32_t value);
blyt_result_t blyt_prefs_set_bool(const char *key, bool value);
blyt_result_t blyt_prefs_set_string(const char *key, const char *value);
blyt_result_t blyt_prefs_get_float(const char *key, float *out, float default_value);
blyt_result_t blyt_prefs_get_int(const char *key, int32_t *out, int32_t default_value);
blyt_result_t blyt_prefs_get_bool(const char *key, bool *out, bool default_value);
blyt_result_t blyt_prefs_get_string(const char *key, char *out,
                size_t size, const char *default_value);
blyt_result_t blyt_prefs_delete(const char *key);
blyt_result_t blyt_prefs_show_ui(void);  // runtime-rendered settings UI
```

**Recommended key naming convention:**
```
audio.music_volume
audio.sfx_volume
audio.ambient_volume
display.subtitles
controls.scheme
accessibility.high_contrast
```

**Runtime settings UI.** Cart declares preferences with types and display names in manifest. `blyt_prefs_show_ui` invokes a runtime-rendered settings panel — same visual language as soft keyboard and pause menu. Carts that want custom settings UI can still build one.

---

## 12. Time, RNG, Math

### Time

```c
uint32_t blyt_time_frame(void);   // i32 frame count since cart start
float    blyt_time_delta(void);   // always 1.0f / declared fps, convenience only
```

**Frame count only.** No wall-clock time exposed to carts. Two identical runs make identical frame counts at identical game moments. `dt` is a convenience; frame count is the authoritative time source for determinism.

### RNG

```c
typedef uint32_t blyt_rng_h;

blyt_result_t blyt_rng_seed(blyt_rng_h rng, uint32_t seed);  // optional explicit reseed
uint32_t    blyt_rng_u32(blyt_rng_h rng);
int32_t     blyt_rng_i32(blyt_rng_h rng, int32_t min, int32_t max);
float       blyt_rng_f32(blyt_rng_h rng, float min, float max);
bool        blyt_rng_bool(blyt_rng_h rng, float probability);
```

**Manifest-declared, constant handles.** RNG streams declared in `cart.config`. Packer generates `RNG_GAMEPLAY`, `RNG_PARTICLES` etc. Runtime initializes all streams at cart start with seeds derived from master seed. Cart code uses constants — no `blyt_rng_get` by name.

**Multiple named streams.** Separate gameplay randomness from visual-effects randomness so cosmetic jitter doesn't affect game state or break replays.

**State in tracked regions.** RNG stream state saves/restores automatically.

### Math

```c
float blyt_math_sin(float x);
float blyt_math_cos(float x);
float blyt_math_tan(float x);
float blyt_math_atan2(float y, float x);
float blyt_math_sqrt(float x);
float blyt_math_pow(float x, float y);
float blyt_math_log(float x);
float blyt_math_floor(float x);
float blyt_math_ceil(float x);
float blyt_math_abs(float x);
float blyt_math_clamp(float x, float min, float max);
float blyt_math_lerp(float a, float b, float t);
float blyt_math_sign(float x);
float blyt_math_round(float x);
```

**Deterministic implementations.** These wrap musl libm — not the host's libm. IEEE 754 does NOT mandate bit-identical transcendentals across implementations. Carts must use `blyt_math_*` for determinism; `math.h` stripped from cart SDK headers to enforce this.

**Extra functions vs standard C math.** `clamp`, `lerp`, `sign`, `round` are universally needed in game code and not in standard C math. Every cart reimplements them without SDK inclusion.

---

## 13. Color and Palette

```c
// Palette conversion
blyt_result_t blyt_color_rgba_to_index(uint32_t rgba, uint8_t *out);
blyt_result_t blyt_color_index_to_rgba(uint8_t index, uint32_t *out);

// Color space
blyt_result_t blyt_color_rgb_to_hsv(uint32_t rgba, float *h, float *s, float *v);
blyt_result_t blyt_color_hsv_to_rgb(float h, float s, float v, uint32_t *out);

// Mixing
blyt_result_t blyt_color_lerp(uint32_t rgba_a, uint32_t rgba_b, float t, uint32_t *out);
blyt_result_t blyt_color_lerp_index(uint8_t a, uint8_t b, float t, uint8_t *out);
blyt_result_t blyt_color_palette_lerp(const uint32_t *pal_a, const uint32_t *pal_b,
                float t, uint32_t *out, uint32_t count);

// Palette cycling
typedef uint32_t blyt_cycle_h;

typedef enum {
    BLYT_CYCLE_FORWARD  =  1,
    BLYT_CYCLE_BACKWARD = -1,
} blyt_cycle_dir_t;

blyt_result_t blyt_color_cycle_set_direction(blyt_cycle_h cycle, blyt_cycle_dir_t direction);
blyt_result_t blyt_color_cycle_set_interval(blyt_cycle_h cycle, uint32_t interval_frames);
blyt_result_t blyt_color_cycle_pause(blyt_cycle_h cycle);
blyt_result_t blyt_color_cycle_resume(blyt_cycle_h cycle);
blyt_result_t blyt_color_cycle_reset(blyt_cycle_h cycle);
```

**Palette cycling is manifest-declared.** Cycles declared in `cart.config` with start index, count, direction, interval. Packer generates `CYCLE_WATER`, `CYCLE_LAVA` etc. Runtime auto-advances all active cycles every frame based on interval. Cart calls `CYCLE_WATER:pause()` etc for game events.

**Cycle state in save state.** Same argument as shake — a restored game should have identical palette cycling. Cycle state is small (a few integers per cycle) and trivially serialized.

**Multiple simultaneous cycles.** Water, lava, city lights all cycling independently. Direction configurable per cycle. Interval configurable — speed up lava cycle in a boss fight.

---

## 14. Localisation

```c
typedef uint32_t blyt_loc_key_t;

blyt_result_t blyt_locale_set(const char *locale);
const char *blyt_locale_get(void);
const char *blyt_locale_system(void);
const char *blyt_loc(blyt_loc_key_t key);
const char *blyt_loc_fmt(blyt_loc_key_t key, ...);
const char *blyt_loc_plural(blyt_loc_key_t key, int32_t n);
blyt_result_t blyt_loc_format_int(int32_t value, char *out, size_t size);
blyt_result_t blyt_loc_format_float(float value, uint32_t decimals, char *out, size_t size);

typedef enum {
    BLYT_LOC_MODE_NORMAL,
    BLYT_LOC_MODE_KEYS,
} blyt_loc_mode_t;

blyt_result_t blyt_loc_set_mode(blyt_loc_mode_t mode);
blyt_result_t blyt_loc_set_key_color(uint8_t color);
```

**Packer-driven locale detection.** Packer scans `localisation/` directory. Single locale file → default automatically. Multiple files → must declare `default_locale` in manifest. Plural rules inferred from locale codes for known languages. No manifest declarations for font overrides or locale-specific settings — packer infers font requirements from character sets found in locale strings.

**Integer key constants.** Packer generates `L_MENU_START`, `L_DIALOG_GREETING` etc. Fast lookup, compiler checking, autocomplete. No string lookups at runtime.

**Fallback chain.** Missing key in active locale → falls back to default locale. Game always playable even with incomplete translations.

**Dev tooling:**
- `F8` — cycle through all declared locales
- `F9` — toggle key mode (show raw key names in magenta instead of translated strings)
- Dev overlay shows string overflow warnings when translated string exceeds declared text box width

**Packer loc-report:**
```
console loc-report ./myproject

L_MENU_START   en ✓  fr ✓  de ✗ (+12px)  ja ✓
```

Shows missing keys and layout overflow per locale. `overflow = "warn"` or `"error"` in manifest build config controls whether overflow fails the build.

**Locale as preference.** Current locale lives in `blyt_prefs`, not game state. Changes take effect immediately since strings are looked up at draw time.

**RTL deferred to v2.** Full RTL support (bidirectional text, Arabic shaping) is months of work. LTR-only for v1 with clear documentation.

---

## 15. Achievements

```c
typedef uint32_t blyt_achievement_h;

blyt_result_t blyt_achievement_unlock(blyt_achievement_h id);
blyt_result_t blyt_achievement_progress(blyt_achievement_h id,
                uint32_t current, uint32_t total);
bool        blyt_achievement_is_unlocked(blyt_achievement_h id);
```

**Manifest-declared.** Achievement IDs, names, descriptions, icons, hidden flags all in `cart.info`. Packer generates `ACH_FINAL_BOSS`, `ACH_FLAWLESS` etc.

**Runtime handles display.** Notification banner on unlock, achievement browser UI, persistence in per-cart `achievements.json`. Cart code just announces unlocks.

**Never re-locks.** Save state/rewind don't undo unlocks. Once unlocked, always unlocked.

---

## 16. Pause Menu, Credits, and Quit

```c
typedef enum {
    BLYT_PAUSE_MENU_NONE,
    BLYT_PAUSE_MENU_ITEM,
} blyt_pause_menu_event_t;

blyt_pause_menu_event_t blyt_pause_menu_event(uint32_t *item_id);
```

**Manifest-declared items.** Pause menu items declared in `cart.config`. Packer generates `PAUSE_ITEM_RETURN_TO_TITLE`, `PAUSE_ITEM_CREDITS` etc. No dynamic registration.

**Standard runtime-provided items:** Resume, Settings, Save State, Load State, Rewind, Quit. Cart can suppress via manifest flags. Cart adds its own items via manifest — they appear in the pause menu and signal via `blyt_pause_menu_event`.

**Return to title.** Runtime shows confirmation dialog with cart-customizable message, confirm/cancel labels. If confirmed, calls `blyt_cart_on_return_to_title`. Cart resets state to title screen condition.

**Credits — two modes:**
- Source declared in manifest (`credits.source = "credits.txt"`) → runtime renders scrolling text. Runtime saves game state snapshot before credits, restores after player backs out. Cart zero involvement.
- No source → runtime calls `blyt_cart_on_credits`. Cart shows its own credits. Cart calls `blyt_credits_done()` when finished. Runtime restores saved snapshot.

**Quit.** If `blyt_cart_on_quit` defined → runtime calls it. Cart shows save prompt / auto-saves / cleans up, then calls `blyt_quit_ready()`. If not defined → runtime shows standard confirmation and exits.

**Runtime-rendered confirmation dialogs.** Standard message with cart-customizable text. Same visual language as soft keyboard and settings UI.

---

## 17. Dev/Instrumentation

All `blyt_dev_*` calls are no-ops in release builds — stripped by preprocessor. Zero cost, zero binary size. Cart code needs no `#ifdef` guards.

```c
// Shapes (rendered above framebuffer in RGBA, not palette)
void blyt_dev_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba);
void blyt_dev_circ(int32_t x, int32_t y, int32_t r, uint32_t rgba);
void blyt_dev_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba);
void blyt_dev_point(int32_t x, int32_t y, uint32_t rgba);
void blyt_dev_arrow(int32_t x, int32_t y, float dx, float dy, uint32_t rgba);

// Text
void blyt_dev_text(int32_t x, int32_t y, uint32_t rgba, const char *fmt, ...);

// Watches
void blyt_dev_watch_float(const char *label, const float *ptr);
void blyt_dev_watch_int(const char *label, const int32_t *ptr);
void blyt_dev_watch_bool(const char *label, const bool *ptr);

typedef void (*blyt_dev_watch_fn)(char *out, size_t size, void *userdata);
void blyt_dev_watch_custom(const char *label, blyt_dev_watch_fn fn, void *userdata);
void blyt_dev_watch_unregister(const char *label);

// Timing
void blyt_dev_start(const char *label);
void blyt_dev_finish(const char *label);
```

**"dev" not "debug"** — avoids confusion with DAP and GDB debuggers. `dev` means runtime instrumentation; "debug" means source-level stepping.

**RGBA not palette index** — debug drawing renders above the cart framebuffer in a separate layer. Full color so collision boxes, waypoints, vectors are clearly visible regardless of cart palette.

**Watches auto-expire** if not updated for N frames. Pointer-based watches (`blyt_dev_watch_float`) registered once; runtime dereferences each frame. Per-frame push watches (`blyt_dev_watch_int` with value) auto-expire if not called.

**Custom watch callbacks.** `blyt_dev_watch_fn` for derived values that require computation — only runs in dev mode, safe to do expensive work.

**Timing sections** — `blyt_dev_start`/`blyt_dev_finish` wrap code sections. Runtime accumulates timing per label, averages over several frames, displays in overlay with nesting support.

**`finish` not `end`** — `end` is a reserved keyword in Lua.

---

## 18. Easing

```c
typedef enum {
    BLYT_EASE_LINEAR,
    BLYT_EASE_QUAD_IN,    BLYT_EASE_QUAD_OUT,    BLYT_EASE_QUAD_IN_OUT,
    BLYT_EASE_CUBIC_IN,   BLYT_EASE_CUBIC_OUT,   BLYT_EASE_CUBIC_IN_OUT,
    BLYT_EASE_QUART_IN,   BLYT_EASE_QUART_OUT,   BLYT_EASE_QUART_IN_OUT,
    BLYT_EASE_SINE_IN,    BLYT_EASE_SINE_OUT,    BLYT_EASE_SINE_IN_OUT,
    BLYT_EASE_EXPO_IN,    BLYT_EASE_EXPO_OUT,    BLYT_EASE_EXPO_IN_OUT,
    BLYT_EASE_CIRC_IN,    BLYT_EASE_CIRC_OUT,    BLYT_EASE_CIRC_IN_OUT,
    BLYT_EASE_BACK_IN,    BLYT_EASE_BACK_OUT,    BLYT_EASE_BACK_IN_OUT,
    BLYT_EASE_ELASTIC_IN, BLYT_EASE_ELASTIC_OUT, BLYT_EASE_ELASTIC_IN_OUT,
    BLYT_EASE_BOUNCE_IN,  BLYT_EASE_BOUNCE_OUT,  BLYT_EASE_BOUNCE_IN_OUT,
} blyt_ease_t;

float   blyt_ease(blyt_ease_t type, float t);
float   blyt_ease_lerp(blyt_ease_t type, float a, float b, float t);
int32_t blyt_ease_lerp_i(blyt_ease_t type, int32_t a, int32_t b, float t);
```

**No runtime tween manager.** Active tweens are dynamic — a big fight might have dozens simultaneously. Forcing that into a fixed manifest declaration would be wasteful or restrictive. Raw easing in the API; tween tracking in cart state buffers.

**SDK ships a lightweight tween helper** (`sdk/lua/blyt_tween.lua`) — a small struct and update function. Not runtime machinery; just a header/module that carts include if wanted.

**Tween fields in state buffer layouts.** Store `start_frame`, `duration`, `from`, `to`, `ease`, `active` as scalar fields. Works naturally with SOA; save state falls out automatically.

---

## 19. Lua API

### Module Structure

All runtime functionality under `console.*` namespace:

```lua
blyt32.gfx.*       -- graphics
blyt32.input.*     -- input
blyt32.audio.*     -- audio (with audio.sfx.*, audio.music.*, audio.stream.*)
blyt32.speech.*    -- speech / lip sync
blyt32.state.*     -- state buffers
blyt32.resource.*  -- resource loading
blyt32.save.*      -- cart saves
blyt32.prefs.*     -- preferences
blyt32.time.*      -- time
blyt32.rng.*       -- random number generation
blyt32.loc.*       -- localisation
blyt32.log.*       -- logging
blyt32.math.*      -- deterministic math (replaces standard math.*)
blyt32.dev.*       -- instrumentation (dev mode only)
blyt32.color.*     -- color utilities
blyt32.mem.*       -- memory introspection
blyt32.achievements.*
blyt32.speedrun.*
```

### Default Imports

SDK provides `blyt_preamble.lua` loaded before cart code. Imports most-used subsystems as globals:

```lua
gfx     = blyt32.gfx
input   = blyt32.input
audio   = blyt32.audio
-- etc.
BTN     = blyt32.input    -- BTN.A, BTN.UP etc.
ERR     = blyt32.errors
```

Cart can disable or customize via manifest. Preamble loaded as a separate `require` chunk — cart source line numbers unaffected.

Generated constants auto-required in same order:

```
1. blyt_preamble.lua          ← SDK
2. cart/preamble.lua        ← optional cart-provided
3. generated/*.lua          ← R.*, S.*, RNG.*, L.*, etc.
4. cart/main.lua            ← cart entry point
```

**Generated constant tables:**

| Generated file | C prefix | Lua table |
|---------------|----------|-----------|
| cart_resources | `R_` | `R.` |
| cart_state | `S_` | `S.` |
| cart_rng | `RNG_` | `RNG.` |
| cart_loc | `L_` | `L.` |
| cart_cycles | `CYCLE_` | `CYCLE.` |
| cart_achievements | `ACH_` | `ACH.` |
| cart_pause | `PAUSE_` | `PAUSE.` |
| cart_audio_groups | `GRP_` | `GRP.` |
| cart_tilemaps | `TM_` | `TM.` |

### Object Methods

All handles in Lua are wrapped objects with methods — not raw integers. Runtime wraps handles automatically when generated files are loaded.

```lua
-- Buffer as object
for i in enemies:active() do
    enemies.x[i] = enemies.x[i] + enemies.vx[i] * dt
end
local i = enemies:alloc_slot()
enemies:free_slot(i)

-- Image as object
local img = resource.load_image(R.HERO_SPRITES)
local w, h = img:size()
img:blit(src_x, src_y, src_w, src_h, dst_x, dst_y, dst_w, dst_h, flags)
img:blit_to(target, ...)
img:pget(x, y)
img:pixel(x, y, color)

-- Voice as object (expired voices are silent no-ops)
local voice = audio.sfx.play(R.SFX_JUMP)
voice:is_playing()
voice:stop()
voice:volume(0.5)
voice:pan(-0.5)
voice:pitch(1.2)

-- Speech as object
local line = speech.play(R.SPEECH_GREETING)
line:is_playing()
local shape = line:mouth_shape()

-- RNG stream as object
RNG.GAMEPLAY:f32(0, 1)
RNG.GAMEPLAY:i32(1, 6)
RNG.GAMEPLAY:seed(42)

-- Palette cycle as object
CYCLE.WATER:pause()
CYCLE.WATER:set_interval(1)
CYCLE.WATER:set_direction(color.FORWARD)
```

### Error Handling

```lua
-- nil return for fallible operations
local data, metadata = save.read("slot1")
if not data then
    local code = blyt32.last_error_code()
    if code == ERR.SAVE_NOT_FOUND then
        -- start fresh
    end
end

-- Silent failure for drawing calls
gfx.pixel(x, y, color)  -- nobody checks if this fails

-- Expired voice handles degrade gracefully
voice:volume(0.5)        -- silent no-op if voice finished
voice:is_playing()       -- returns false, not an error
```

### Math Override

Lua's standard `math.*` replaced entirely with deterministic implementations plus game-useful additions:

```lua
math.sin, math.cos, math.tan  -- deterministic, not host libm
math.clamp(x, min, max)       -- not in standard Lua
math.lerp(a, b, t)            -- not in standard Lua
math.sign(x)                  -- not in standard Lua
math.round(x)                 -- not in standard Lua
```

### Logging

```lua
-- Simple — args tostring'd and concatenated
log.debug("player pos", player.x, player.y)
log.info("level loaded", level_name)
log.warn("enemy count near budget", enemies:active_count())
log.error("save failed")

-- _fmt variants — format string only evaluated if level active
log.debug_fmt("player pos %.1f, %.1f", player.x, player.y)
log.warn_fmt("budget at %d%%", math.floor(pct * 100))
```

### Lua Version

Targeting **Lua 5.4**. Reasons:
- `LUA_32BITS` support (`LUA_INT_TYPE=LUA_INT_INT`, `LUA_FLOAT_TYPE=LUA_FLOAT_FLOAT`)
- Native bitwise operators (`&`, `|`, `~`, `>>`, `<<`)
- `_ENV` for clean import implementation
- Current maintained version

### Bytecode in Release Builds

Release builds ship `luac` bytecode, not source. Benefits: smaller size (30-50% smaller before compression), no compile step at cart start, source not directly readable. SDK ships matching `luac` binary; version mismatch is a hard packer error.

Debug builds ship source for readable stack traces. `lua_strip_debug = false` by default in release — keep line numbers in crash reports.

---

## 20. Generated Files

### Three Categories

**Build artifacts (gitignored, regenerated every build):**
```
myproject/generated/
    cart_resources.h / .lua
    cart_state.h / .lua
    cart_rng.h / .lua
    cart_loc.h / .lua
    cart_cycles.h / .lua
    cart_achievements.h / .lua
    cart_pause.h / .lua
    cart_audio_groups.h / .lua
    cart_tilemaps.h / .lua
```

**Cached build artifacts (gitignore optional):**
```
myproject/cache/
    speech/*.json      ← Rhubarb output, expensive to regenerate
    sprites/*.meta     ← palette quantization cache
```

Committing Rhubarb JSON makes builds reproducible without Rhubarb installed. Gitignoring keeps the repo clean. SDK template leaves this to author preference.

**SDK-provided files (never modified by packer):**
```
sdk/
    include/blyt32.h, blyt_runtime.h, blyt_resources.h, blyt_dev.h
    lua/blyt_preamble.lua, blyt_resources.lua, blyt_buttons.lua, blyt_errors.lua
    lua/blyt_tween.lua, blyt_vec2.lua
    bin/luac, rhubarb, console
    schemas/cart.info.json, cart.config.json, cart.build.json  ← JSON Schema for yaml-language-server
```

### Packer Stability Guarantee

Within a dev session: adding a resource → appended, existing IDs unchanged. Removing → ID retired, gap left. Renaming → ID unchanged, constant name updated. Compaction only on `console bundle` or `console renumber`.

### External Tool Access to cart.info

```
blytbuild info myproject.blyt --json > cart_info.json
```

Also ships `blyt_cartinfo` C library for tools that need to read `.cart.info` ELF sections without a full runtime.

---

## 21. Manifest Files

Three YAML source files with distinct consumers, compiled to FlatBuffers
binaries by the packer (see ADR-0073). The cart carries no developer-declared
identifier — per-cart identity is owned by the frontend (see ADR-0016) and
netplay matchmaking uses the cart binary hash (see ADR-0021).

### cart.info.yaml

Read by frontend without loading the cart. Compiled to the `.cart.info` ELF
section as FlatBuffers. Frontend reads directly without runtime.

```yaml
# cart.info.yaml
# yaml-language-server: $schema=.console/schemas/cart.info.json
title:        My Game
author:       Studio Name
version:      1.0.0
api_version:  "1.0"

size_class:   standard
interactive:  true
min_players:  1
max_players:  4

icon:         assets/icon.png
supported_locales: [en, fr, de, ja]

netplay:
  supported: true
  modes:     [coop, versus]

content:
  rating:   everyone
  violence: false
  language: false

store:
  description: A short description.
  tags:        [platformer, action]
  banner:      banner.png
  screenshots: [screenshots/1.png]

# debug: true   # injected by packer in debug builds; absent in release
```

`fps` is not in `cart.info.yaml`; it lives in `cart.config.yaml` and the
frontend reads it via `blyt_cart_fps()` after cart load (see ADR-0047).

### cart.config.yaml

Read by the runtime at cart load. Compiled to the `.cart.config` ELF
section as FlatBuffers.

```yaml
# cart.config.yaml
# yaml-language-server: $schema=.console/schemas/cart.config.json
fps: 60   # optional; default 60 (ADR-0047)

inputs_used:
  dpad:     Move
  button_a: Jump
  start:    Pause
touch_scheme:         virtual_gamepad_classic
touch_shoulder_style: corner_buttons

voice_groups: [ambient, ui]    # extends runtime defaults (ADR-0054)
rng_streams:  [gameplay, particles]

layouts:
  Player: { x: f32, y: f32, hp: i32 }
  Enemy:
    x:  f32
    y:  f32
    hp: i32
    waypoints_x: { type: f32, count: 8 }

state_buffers:
  player:  { type: Player, count: 1 }
  enemies: { type: Enemy,  count: 200 }

tilemaps:
  world:   { source: map_world,   mutable: true }
  dungeon: { source: map_dungeon, mutable: false }

persistent_resources: [font_ui, player_sprites]

palette_cycles:
  water: { start: 32, count: 8, direction: 1, interval: 3 }

locale_keys:
  - menu.start
  - menu.options
  - hud.score

pause_items:
  - { id: resume,            label_key: pause.resume }
  - { id: credits,           label_key: pause.credits }
  - { id: return_to_title,   label_key: pause.return_to_title,
      confirm_key: pause.return_confirm }
  - { id: quit,              label_key: pause.quit }

credits_file: credits.txt    # plain text; runtime formats and scrolls

prefs:
  music_volume: { type: f32,  default: 1.0, min: 0.0, max: 1.0 }
  subtitles:    { type: bool, default: false }

achievements:
  final_boss: { name_key: ach.final_boss.name,
                desc_key: ach.final_boss.desc, icon: crown }
  flawless:   { name_key: ach.flawless.name,
                desc_key: ach.flawless.desc,   hidden: true }
```

### cart.build.yaml

Read only by the packer. Never compiled into the cart. Optional — defaults
suit most projects (see ADR-0073).

```yaml
# cart.build.yaml
# yaml-language-server: $schema=.console/schemas/cart.build.json
resource_roots: [assets/]
speech_dir:     speech/
generated_dir:  generated/
cache_dir:      cache/

fonts:
  font_ui:     { source: fonts/main.ttf,   sizes: [8, 16] }
  font_dialog: { source: fonts/dialog.ttf, sizes: [12], charset: latin }
  font_hud:    { source: fonts/hud.png,    glyph_w: 8, glyph_h: 8 }

speech:
  lip_sync_quality:        basic
  lip_sync_quality_bundle: high

build:
  loc_overflow:         warn
  strip_unused_locales: true
  lua_strip_debug:      false
```

For Lua carts the packer also writes a `.cart.lua` ELF section (FlatBuffers)
containing Lua-specific runtime configuration (currently only the bytecode
version, derived from `api_version` and not authored). When author-facing
Lua settings are needed, a `cart.lua.yaml` source file will be introduced
following the same `cart.*.yaml` convention (see ADR-0073).

### Why YAML

YAML 1.2 reads naturally for the nested structures these manifests carry
(type definitions, achievements, palette cycles), is supported by the
ubiquitous `yaml-language-server` extension with JSON-Schema-driven
completion and validation, and avoids the boolean-coercion footguns of
YAML 1.1 (`no`, `yes`, `on`, `off`). The SDK ships JSON Schemas for each
manifest; `blytbuild new` generates projects with a `# yaml-language-server:
$schema=` header in each file. See ADR-0073 for the rationale against
Lua-source manifests.

### Compiled Binary Format

All three YAML files are compiled to FlatBuffers at pack time — the
runtime and frontend never parse YAML. Each FlatBuffers buffer is stored
in its ELF section preceded by an 8-byte preamble (4-byte ASCII type tag
plus uint16 LE major/minor format version) so a reader can reject an
unsupported format before any deserialization (see ADR-0073). Reads are
zero-copy from the mmap'd ELF; all schema validation and authoring errors
are caught at pack time.

**No Lua interpreter needed for native carts.** The runtime detects Lua
carts by the presence of the `.cart.lua` section; native carts ship without
it and the Lua VM is never loaded.

---

## 22. Native Performance APIs

### Spatial Queries

```c
blyt_result_t blyt_spatial_query_rect(blyt_buffer_h buf,
    blyt_field_h x_field, blyt_field_h y_field,
    blyt_rect_t bounds,
    uint32_t *out_indices, uint32_t *out_count, uint32_t max_results);

blyt_result_t blyt_spatial_query_circle(blyt_buffer_h buf,
    blyt_field_h x_field, blyt_field_h y_field,
    blyt_circle_t bounds,
    uint32_t *out_indices, uint32_t *out_count, uint32_t max_results);

blyt_result_t blyt_spatial_nearest(blyt_buffer_h buf,
    blyt_field_h x_field, blyt_field_h y_field,
    int32_t x, int32_t y,
    uint32_t *out_index);

blyt_result_t blyt_spatial_raycast(blyt_buffer_h buf,
    blyt_field_h x_field, blyt_field_h y_field,
    blyt_field_h r_field,   // BLYT_FIELD_NONE for point entities
    int32_t ox, int32_t oy,
    float dx, float dy,
    float max_dist,
    uint32_t *out_index, float *out_dist);

// Tilemap queries
blyt_result_t blyt_spatial_tilemap_query_rect(blyt_tilemap_ref tilemap,
    blyt_rect_t bounds,
    uint32_t *out_tx, uint32_t *out_ty, uint16_t *out_tiles,
    uint32_t *out_count, uint32_t max_results);

blyt_result_t blyt_spatial_tilemap_query_circle(blyt_tilemap_ref tilemap,
    blyt_circle_t bounds,
    uint32_t *out_tx, uint32_t *out_ty, uint16_t *out_tiles,
    uint32_t *out_count, uint32_t max_results);
```

All entity queries operate on active slots only. Field constants used throughout — no string lookups.

### Collision

```c
typedef struct { int32_t x, y, w, h; } blyt_rect_t;
typedef struct { int32_t x, y, r; }    blyt_circle_t;

// Detection
bool blyt_col_rect_rect(blyt_rect_t a, blyt_rect_t b);
bool blyt_col_rect_rect_depth(blyt_rect_t a, blyt_rect_t b, int32_t *out_dx, int32_t *out_dy);
bool blyt_col_circle_circle(blyt_circle_t a, blyt_circle_t b);
bool blyt_col_rect_circle(blyt_rect_t r, blyt_circle_t c);
bool blyt_col_point_rect(int32_t x, int32_t y, blyt_rect_t r);
bool blyt_col_point_circle(int32_t x, int32_t y, blyt_circle_t c);
bool blyt_col_point_poly(int32_t x, int32_t y, const int32_t *verts, uint32_t count);

// Swept (continuous)
bool blyt_col_swept_rect_rect(blyt_rect_t mover, float dx, float dy,
    blyt_rect_t obstacle, float *out_t, float *out_nx, float *out_ny);
bool blyt_col_swept_circle_circle(blyt_circle_t mover, float dx, float dy,
    blyt_circle_t obstacle, float *out_t, float *out_nx, float *out_ny);

// Resolution strategies
typedef enum {
    BLYT_COL_RESOLVE_SHORTEST,   // shortest penetration axis — top-down, arcade
    BLYT_COL_RESOLVE_Y_FIRST,    // resolve Y then X — platformers with gravity
    BLYT_COL_RESOLVE_X_FIRST,
    BLYT_COL_RESOLVE_SLIDE,      // slide along surface — smooth wall sliding
    BLYT_COL_RESOLVE_BOUNCE,     // reflect velocity — ball games, projectiles
} blyt_col_resolve_mode_t;

blyt_result_t blyt_col_resolve(float *x, float *y, float *vx, float *vy,
    blyt_rect_t bounds, blyt_rect_t obstacle,
    float nx, float ny, float t,
    blyt_col_resolve_mode_t mode);

// Contact bitmask
#define BLYT_COL_CONTACT_NONE   0
#define BLYT_COL_CONTACT_TOP    (1 << 0)   // grounded check
#define BLYT_COL_CONTACT_BOTTOM (1 << 1)   // hit ceiling
#define BLYT_COL_CONTACT_LEFT   (1 << 2)
#define BLYT_COL_CONTACT_RIGHT  (1 << 3)

blyt_result_t blyt_col_resolve_tilemap(float *x, float *y,
    float prev_y,           // for one-way platform check
    float *vx, float *vy,
    blyt_rect_t bounds,
    blyt_tilemap_ref tilemap,
    blyt_col_resolve_mode_t mode,
    uint32_t *out_contacts, // optional — BLYT_COL_CONTACT_* bitmask
    uint32_t *out_triggers  // optional — packed tx<<16|ty of trigger tiles
);

blyt_result_t blyt_col_resolve_buffer(blyt_buffer_h buf,
    blyt_field_h x_field, blyt_field_h y_field, blyt_field_h r_field,
    blyt_col_resolve_mode_t mode, uint32_t max_iterations);
```

**One-way platforms.** `BLYT_TILE_ONE_WAY_UP` — solid when entity moving downward AND entity bottom edge was above tile top edge last frame. `prev_y` parameter provides previous position for this check. Cart already tracks this for physics.

**Trigger tiles.** `BLYT_TILE_TRIGGER` tiles detected but not resolved during `blyt_col_resolve_tilemap`. Returned as packed `tx<<16|ty` values in `out_triggers`. Cart handles spikes, water, collectibles from this list.

**Slopes out of scope for v1.** Full slope collision significantly more complex; deferred.

**Pathfinding deferred to v1.x.** Carts that need A* can implement against tilemap data using existing APIs.

### Bulk Vector Math

```c
// Typed scalar ops (C)
blyt_result_t blyt_buf_field_set_f(blyt_buffer_h buf, blyt_field_h field, float value);
blyt_result_t blyt_buf_field_set_i(blyt_buffer_h buf, blyt_field_h field, int32_t value);
blyt_result_t blyt_buf_field_set_all_f(blyt_buffer_h buf, blyt_field_h field, float value);
blyt_result_t blyt_buf_field_set_all_i(blyt_buffer_h buf, blyt_field_h field, int32_t value);

blyt_result_t blyt_buf_field_add_scalar_f(blyt_buffer_h buf, blyt_field_h field, float scalar);
blyt_result_t blyt_buf_field_add_scalar_i(blyt_buffer_h buf, blyt_field_h field, int32_t scalar);
blyt_result_t blyt_buf_field_mul_scalar_f(blyt_buffer_h buf, blyt_field_h field, float scalar);
blyt_result_t blyt_buf_field_mul_scalar_i(blyt_buffer_h buf, blyt_field_h field, int32_t scalar);
blyt_result_t blyt_buf_field_clamp_f(blyt_buffer_h buf, blyt_field_h field, float min, float max);
blyt_result_t blyt_buf_field_clamp_i(blyt_buffer_h buf, blyt_field_h field, int32_t min, int32_t max);
blyt_result_t blyt_buf_field_abs(blyt_buffer_h buf, blyt_field_h field);
blyt_result_t blyt_buf_field_negate(blyt_buffer_h buf, blyt_field_h field);

// Field-to-field ops (element-wise, active slots only)
blyt_result_t blyt_buf_field_add_field(blyt_buffer_h buf, blyt_field_h dst, blyt_field_h src);
blyt_result_t blyt_buf_field_sub_field(blyt_buffer_h buf, blyt_field_h dst, blyt_field_h src);
blyt_result_t blyt_buf_field_mul_field(blyt_buffer_h buf, blyt_field_h dst, blyt_field_h src);
blyt_result_t blyt_buf_field_min_field(blyt_buffer_h buf, blyt_field_h dst, blyt_field_h src);
blyt_result_t blyt_buf_field_max_field(blyt_buffer_h buf, blyt_field_h dst, blyt_field_h src);

// Scaled field add — most common physics pattern: pos += vel * dt
blyt_result_t blyt_buf_field_add_field_scaled(blyt_buffer_h buf,
    blyt_field_h dst, blyt_field_h src, float scalar);

// Predicate variants (active AND condition field true)
blyt_result_t blyt_buf_field_add_scalar_f_if(blyt_buffer_h buf,
    blyt_field_h dst, float scalar, blyt_field_h condition);
blyt_result_t blyt_buf_field_add_field_scaled_if(blyt_buffer_h buf,
    blyt_field_h dst, blyt_field_h src, float scalar, blyt_field_h condition);

// Reductions
blyt_result_t blyt_buf_field_sum_f(blyt_buffer_h buf, blyt_field_h field, float *out);
blyt_result_t blyt_buf_field_sum_i(blyt_buffer_h buf, blyt_field_h field, int32_t *out);
blyt_result_t blyt_buf_field_min_value_f(blyt_buffer_h buf, blyt_field_h field, float *out);
blyt_result_t blyt_buf_field_max_value_f(blyt_buffer_h buf, blyt_field_h field, float *out);
blyt_result_t blyt_buf_field_min_value_i(blyt_buffer_h buf, blyt_field_h field, int32_t *out);
blyt_result_t blyt_buf_field_max_value_i(blyt_buffer_h buf, blyt_field_h field, int32_t *out);

// Cross-buffer copy (field types must match)
blyt_result_t blyt_buf_field_copy(blyt_buffer_h dst_buf, blyt_field_h dst_field,
    blyt_buffer_h src_buf, blyt_field_h src_field);

// 2D vector operations
blyt_result_t blyt_buf_vec2_normalize(blyt_buffer_h buf, blyt_field_h x, blyt_field_h y);
blyt_result_t blyt_buf_vec2_set_magnitude(blyt_buffer_h buf, blyt_field_h x, blyt_field_h y,
    float magnitude);
blyt_result_t blyt_buf_vec2_clamp_magnitude(blyt_buffer_h buf, blyt_field_h x, blyt_field_h y,
    float max_magnitude);
blyt_result_t blyt_buf_vec2_dot(blyt_buffer_h buf,
    blyt_field_h ax, blyt_field_h ay, blyt_field_h bx, blyt_field_h by,
    blyt_field_h out_field);
blyt_result_t blyt_buf_vec2_distance_to_point(blyt_buffer_h buf,
    blyt_field_h x_field, blyt_field_h y_field,
    float px, float py, blyt_field_h out_field);
```

**All ops apply to active slots only by default.** `_all` variants for initialization (zero all cooldowns before activating slots).

**Typed variants in C.** `_f` and `_i` suffixes prevent silent truncation. Lua dispatches on field type automatically (dynamic typing).

**`set_all` for bulk initialization.** The only case where operating on inactive slots makes sense — setting default values before any slots are activated.

**Cross-buffer copy matches field types.** Type mismatch → `BLYT_ERR_BUFFER_WRONG_TYPE`. Dev mode enforces strictly; release mode documents as undefined behavior.

### Procedural Noise

```c
typedef enum {
    BLYT_NOISE_PERLIN,
    BLYT_NOISE_SIMPLEX,
    BLYT_NOISE_VALUE,
} blyt_noise_type_t;

float blyt_noise_2d(blyt_noise_type_t type, float x, float y, blyt_rng_h rng);
float blyt_noise_3d(blyt_noise_type_t type, float x, float y, float z, blyt_rng_h rng);

float blyt_noise_fbm_2d(blyt_noise_type_t type, float x, float y,
    blyt_rng_h rng, uint32_t octaves, float lacunarity, float gain);

blyt_result_t blyt_noise_fill_2d(blyt_noise_type_t type,
    float *out, uint32_t w, uint32_t h,
    float scale_x, float scale_y, float offset_x, float offset_y,
    blyt_rng_h rng);
```

**Uses cart's named RNG streams** rather than a raw seed. Determinism falls out automatically — noise seeded from tracked RNG stream, state saves/restores with rest of cart state. Cart uses `RNG.PARTICLES` or similar for noise to keep it separate from gameplay randomness.

**`blyt_noise_fill_2d` is the high-value function.** Generating noise maps for terrain, cloud cover, cave systems in one call. Single-sample functions for per-entity noise (wind variation, NPC wander).

---

## 23. Error Codes

```c
typedef enum {
    BLYT_OK = 0,

    // Generic
    BLYT_ERR_INVALID_HANDLE           = 1,
    BLYT_ERR_INVALID_ARGUMENT         = 2,
    BLYT_ERR_NOT_FOUND                = 3,
    BLYT_ERR_OUT_OF_MEMORY            = 4,
    BLYT_ERR_BUFFER_TOO_SMALL         = 5,
    BLYT_ERR_OVERFLOW                 = 6,
    BLYT_ERR_NOT_AVAILABLE            = 7,   // feature unavailable in current context
    BLYT_ERR_NOT_SUPPORTED            = 8,   // feature not supported on platform
    BLYT_ERR_TIMEOUT                  = 9,
    BLYT_ERR_VERSION_MISMATCH         = 10,

    // Cart (100s)
    BLYT_ERR_CART_NOT_LOADED          = 100,
    BLYT_ERR_CART_ALREADY_LOADED      = 101,
    BLYT_ERR_CART_VERSION_MISMATCH    = 102,
    BLYT_ERR_CART_API_TOO_NEW         = 103,
    BLYT_ERR_CART_INVALID             = 104,

    // Resource (200s)
    BLYT_ERR_RESOURCE_NOT_FOUND       = 200,
    BLYT_ERR_RESOURCE_NOT_LOADED      = 201,
    BLYT_ERR_RESOURCE_WRONG_TYPE      = 202,
    BLYT_ERR_RESOURCE_READ_ONLY       = 203,
    BLYT_ERR_RESOURCE_CORRUPT         = 204,

    // State buffer (300s)
    BLYT_ERR_BUFFER_FULL              = 300,
    BLYT_ERR_BUFFER_INVALID_FIELD     = 301,
    BLYT_ERR_BUFFER_WRONG_TYPE        = 302,
    BLYT_ERR_LAYOUT_NOT_FOUND         = 303,
    BLYT_ERR_LAYOUT_ALREADY_DECLARED  = 304,

    // Save (400s)
    BLYT_ERR_SAVE_NOT_FOUND           = 400,
    BLYT_ERR_SAVE_CORRUPT             = 401,
    BLYT_ERR_SAVE_VERSION_MISMATCH    = 402,
    BLYT_ERR_SAVE_QUOTA_EXCEEDED      = 403,
    BLYT_ERR_SAVE_IO                  = 404,

    // Audio (500s)
    BLYT_ERR_AUDIO_NO_FREE_VOICE      = 500,
    BLYT_ERR_AUDIO_FORMAT_UNSUPPORTED = 501,
    BLYT_ERR_AUDIO_DECODE             = 502,

    // Input (600s)
    BLYT_ERR_INPUT_NOT_AVAILABLE      = 600,
    BLYT_ERR_INPUT_INVALID_PLAYER     = 601,

    // Graphics (700s)
    BLYT_ERR_GFX_FRAMEBUFFER_LOCKED   = 700,
    BLYT_ERR_GFX_IMAGE_NOT_ACQUIRED   = 701,
    BLYT_ERR_GFX_FONT_MISSING_GLYPH   = 702,

    // Tilemap (800s)
    BLYT_ERR_TILEMAP_NOT_MUTABLE      = 800,
    BLYT_ERR_TILEMAP_OUT_OF_BOUNDS    = 801,

    // Localisation (900s)
    BLYT_ERR_LOC_KEY_NOT_FOUND        = 900,
    BLYT_ERR_LOC_LOCALE_NOT_FOUND     = 901,

    // Achievement (1000s)
    BLYT_ERR_ACHIEVEMENT_NOT_FOUND        = 1000,
    BLYT_ERR_ACHIEVEMENT_ALREADY_UNLOCKED = 1001,

    // Netplay (1100s)
    BLYT_ERR_NETPLAY_NOT_ACTIVE           = 1100,
    BLYT_ERR_NETPLAY_ALREADY_ACTIVE       = 1101,
    BLYT_ERR_NETPLAY_INCOMPATIBLE_CART    = 1102,

    // Speech (1200s)
    BLYT_ERR_SPEECH_NO_LIP_SYNC           = 1200,

} blyt_result_t;
```

**Grouped by hundred.** Leaves room for expansion without renumbering. New resource errors go in 200s; new audio errors in 500s.

**Specific over generic.** `BLYT_ERR_SAVE_QUOTA_EXCEEDED` rather than `BLYT_ERR_OVERFLOW` — cart code can branch meaningfully.

**Last error string always available:**
```c
const char *blyt_last_error(void);       // "save quota exceeded: 10MB limit reached"
blyt_result_t blyt_last_error_code(void);  // BLYT_ERR_SAVE_QUOTA_EXCEEDED
```

---

## 24. Deferred to v1.x or Later

- **Pathfinding** (A* over grids) — carts can implement using existing tilemap and spatial APIs
- **Particle systems** — rich primitive library covers most use cases; full particle API in v1.x
- **Inline native code in Lua carts** — architectural fit natural (unified ELF format); packer changes needed
- **Mid-session netplay joining** — libretro has long-standing bugs; deferred
- **Internet lobby integration** — v1 ships LAN-only netplay in custom frontend
- **Browser netplay** — needs WebRTC + signaling server; v2+
- **Cloud saves** — local only in v1
- **Service integration** (Steam, GOG) — distribution wrapper concern; v2+
- **Framework layer** (adventure game, JRPG, shmup frameworks) — community/ecosystem work
- **RTL text support** — months of engineering; v1 is LTR-only
- **Hardware-accelerated audio decode** — optimization; non-breaking when added
- **Screensaver integration** — natural v2+ once non-interactive cart library exists
- **Demo sub-tiers** (64K, 4K) — if community demand emerges
- **Bare-metal runtime** — Linux-on-SBC is right for v1
- **`ref<T>` state buffer reference types** — can add as opt-in sugar later
- **Multi-keyboard input** (two keyboards as separate players) — deferred unless demand
- **Split point type** — flat args throughout; optional SDK vec2 helper
