# ADR-0070: Text input — single-player blocking model; multiplayer name entry is runtime-provided

## Status
Accepted

## Context

Name entry, chat, and similar text-input flows require platform-native text
input (on-screen keyboard on mobile/console, physical keyboard on desktop).
The cart cannot directly poll for text; it must request input and wait for
the platform to supply it.

An earlier draft proposed a `BLYT_TEXT_INPUT_ALL_PLAYERS` mode for soliciting
text from every connected player simultaneously (lobby name entry in local
multiplayer). The implementation surface is large — N concurrent platform
keyboards, N independent submit/cancel flows, per-device focus management,
display real estate for N prompts — and the only motivating use case is
collecting player names. There is no reason cart code needs to own that
flow; the runtime can collect names itself before handing the cart a
multiplayer session that already has names attached.

## Decision

**v1 ships a single-player blocking text-input model. Multiplayer name
entry is runtime-provided, triggered by a manifest declaration.**

### Single-player text input (cart-facing API)

```c
blyt_result_t blyt_text_input_begin(const char *prompt, const char *initial,
                                int32_t max_len);

// Called on completion (next update after input is dismissed):
const char *blyt_text_input_result(void);  // NULL if cancelled
```

**Flow:**
1. Cart calls `blyt_text_input_begin()` during `update()`.
2. Runtime finishes the current frame (calls `draw()` once more so the cart
   can show a "waiting for input" UI), then suspends cart `update()`.
3. Runtime raises the platform text input UI (on-screen keyboard or native
   text field).
4. When the player submits or cancels, the runtime resumes cart `update()`.
5. Cart calls `blyt_text_input_result()` to retrieve the string (or `NULL`
   for cancel).

This solicits exactly one player — the local player driving the session.
There is no concurrent / all-players mode in v1.

### Multiplayer name entry (runtime-provided)

A cart that supports multiplayer declares so in `cart.info.yaml`:

```yaml
# cart.info.yaml
max_players: 4
# names_required: true     # default true when max_players > 1
```

When the runtime is preparing a multiplayer session (LAN-discovered netplay,
local hot-seat with multiple controllers, etc.), it prompts each joining
player for a name in its own UI before handing the session to the cart.
By the time `init()` runs, every player slot has a name available via:

```c
const char *blyt_player_name(uint32_t player_index);
```

**Default behaviour: pre-fill with the last entered name.** The runtime
stores the most recently submitted name as a per-device preference and
presents it as the pre-filled value on every subsequent name prompt on
that device. Returning players hit a single confirm to keep their name;
new players or players changing names overtype it. This applies to every
device the prompt is shown on — a host machine has its own stored name,
each remote netplay client has its own stored name, and on local hot-seat
the device's stored name is offered to whichever player is being
solicited at that moment. The cart never sees the keyboard flow; it just
reads names via `blyt_player_name()`.

### Netplay during a session

`blyt_text_input_begin()` returns `BLYT_ERR_NOT_AVAILABLE` during a netplay
session. Mid-session text entry would require synchronising platform
keyboard state across rollback, which v1 does not support. Lobby-phase
name entry is handled by the runtime as above.

### Future work

Concurrent multi-player text input may return as a future API once a
genuine cart use case beyond name entry exists (e.g., simultaneous chat,
collaborative naming flows). The single-player API would extend rather
than break: a future `blyt_text_input_begin_player(player_index, ...)` could
sit alongside the current `blyt_text_input_begin()` without disturbing
existing carts.

## Consequences

- v1 cart authors deal with one text-input flow and one player at a time —
  matches the common case (single-player name entry, password entry,
  filename for screenshot).
- Multiplayer name collection becomes a runtime concern, not a cart concern;
  every multiplayer cart gets a consistent player-naming UX without
  implementing it.
- Persisting names as a per-device preference removes the friction of
  re-entering names every session.
- The implementation surface for `BLYT_TEXT_INPUT_ALL_PLAYERS` (N concurrent
  platform keyboards, focus management, layout) is removed from v1; we
  buy back the flexibility to redesign it once we have real cart needs.
- `BLYT_ERR_NOT_AVAILABLE` during netplay remains a clear error that carts
  should handle gracefully (log and no-op in release; warn in dev).
