-- Spike M — `blyt32.coroutine` user-facing module.
--
-- Provides the proposed ADR-0012 amendment shape:
--   blyt32.coroutine.create(function(ctx) ... end, seed?)  -> handle
--   blyt32.coroutine.resume(handle)                         -> ok, ctx
--   blyt32.coroutine.destroy(handle)
--   blyt32.coroutine.status(handle)                         -> "suspended"|"running"|"dead"|"none"
--
-- The module also wraps stock `coroutine.create` so that transient
-- coroutines (i.e. those NOT created via `blyt32.coroutine.create`)
-- can be detected at save/restore boundaries.  The wrapper records
-- each transient in a weak-keyed registry; a managed coroutine never
-- appears there.  Spike M's transient negative test (Stage 5)
-- exercises this discrimination.
--
-- The runtime owns ctx serialization: on every resume, the wrapper
-- flattens `ctx` into the slot's blob bytes via
-- `console.script_write_blob`.  On a load run, the slot's bytes are
-- already restored from the buffer; the wrapper reads them via
-- `console.script_read_blob` and uses them as `ctx` (overriding the
-- caller's seed).  Slot indices are stable across save/restore.

local M = {}

-- ── slot bookkeeping (Lua-side) ──────────────────────────────────────────
-- slots[slot_index] = { body=fn, lua_co=co, ctx=table }.
-- The C-side `region_persistent_scripts` mirrors slot occupancy + bytes.
local slots = {}

-- ── transient registry (for the boundary-crossing negative test) ─────────
-- Weak keys so completed coroutines drop out of the registry naturally.
local _live = setmetatable({}, { __mode = "k" })
local _raw_create = coroutine.create
local _raw_resume = coroutine.resume
local _raw_status = coroutine.status

-- Wrap coroutine.create so every transient enters _live.  Managed
-- coroutines are created via _raw_create directly (below) — they
-- never enter _live, which is exactly the discrimination the spike
-- validates.
coroutine.create = function (fn)
    local co = _raw_create(fn)
    _live[co] = true
    return co
end

-- ── managed-coroutine API ────────────────────────────────────────────────

local function flatten_and_persist(slot, ctx)
    local bytes, err = console.lua_table_flatten(ctx)
    if not bytes then
        error("BLYT_ERR_FLATTEN: " .. tostring(err), 2)
    end
    if not console.script_write_blob(slot, bytes) then
        error("BLYT_ERR_SLOT_BLOB_OVERFLOW", 2)
    end
end

function M.create(body, seed)
    if type(body) ~= "function" then
        error("blyt32.coroutine.create: body must be a function", 2)
    end
    seed = seed or {}
    if type(seed) ~= "table" then
        error("blyt32.coroutine.create: seed must be a table or nil", 2)
    end
    local slot = console.script_alloc()
    if slot < 0 then
        error("BLYT_ERR_SLOT_EXHAUSTED", 2)
    end

    -- On a load run, the slot's bytes were just restored from the
    -- buffer.  Use them as the deserialized ctx, overriding seed.
    -- An empty string means the slot was occupied at save time but
    -- the saved coroutine completed or was destroyed before save —
    -- fall through to the seed in that case.
    local ctx = seed
    if console.is_load_resume() then
        local bytes = console.script_read_blob(slot)
        if bytes and #bytes > 0 then
            local restored, err = console.lua_table_unflatten(bytes)
            if restored == nil then
                error("BLYT_ERR_UNFLATTEN: " .. tostring(err), 2)
            end
            ctx = restored
        end
    end

    -- Use the raw create so the managed coroutine never enters _live.
    local lua_co = _raw_create(body)
    slots[slot] = { body = body, lua_co = lua_co, ctx = ctx }

    -- Persist the initial ctx so a save *before* any resume still has
    -- bytes in the slot blob.  Without this, slot bytes would be all
    -- zero and unflatten on load would produce a different ctx than
    -- seed — masking serializer bugs.
    flatten_and_persist(slot, ctx)
    return slot
end

function M.resume(slot)
    local rec = slots[slot]
    if rec == nil then
        return false, nil
    end
    if _raw_status(rec.lua_co) == "dead" then
        slots[slot] = nil
        console.script_free(slot)
        return false, nil
    end
    -- First resume passes ctx as the body's first argument; subsequent
    -- resumes don't need to re-pass it because the body holds a
    -- reference and mutates it in place.  Pass it every time anyway —
    -- harmless after the first resume (yield's return value isn't used
    -- by spike workloads) and keeps the load-side first-resume path
    -- consistent with the steady-state path.
    local ok, err = _raw_resume(rec.lua_co, rec.ctx)
    if _raw_status(rec.lua_co) == "dead" then
        -- Body returned (or errored).  Auto-reclaim the slot.
        slots[slot] = nil
        console.script_free(slot)
        if not ok then return false, err end
        return true, rec.ctx
    end
    -- Body yielded — flatten ctx for the next save.
    flatten_and_persist(slot, rec.ctx)
    return ok, rec.ctx
end

function M.destroy(slot)
    local rec = slots[slot]
    if rec == nil then return end
    slots[slot] = nil
    console.script_free(slot)
end

function M.status(slot)
    local rec = slots[slot]
    if rec == nil then return "none" end
    return _raw_status(rec.lua_co)
end

-- Walk the slot table (used by the cart's load logic when it wants to
-- enumerate which scripts were active at save time, e.g. for the
-- destroy/replace test).
function M.active_slots()
    local out = {}
    for s = 0, console.max_persistent_scripts() - 1 do
        if console.script_is_active(s) then out[#out + 1] = s end
    end
    return out
end

-- The save/restore-boundary check: every coroutine in _live that is
-- still suspended at save/restore time must be invalidated.  Carts
-- call this from their save-side hook; the spike's negative test
-- catches a transient that survives a save without being invalidated.
function M.invalidate_transients()
    for co in pairs(_live) do
        if _raw_status(co) == "suspended" then
            -- Mark in the registry: when next resumed, the resume
            -- wrapper (below) refuses with the ADR-0012 error.
            _live[co] = "boundary-crossed"
        end
    end
end

-- Wrap coroutine.resume so a boundary-crossed transient throws
-- the canonical error string on next resume.  This must run in the
-- *load* side, where _live has been reseeded by `invalidate_transients`.
coroutine.resume = function (co, ...)
    if _live[co] == "boundary-crossed" then
        error("RuntimeError: coroutine crossed a save/restore boundary.\n" ..
              "Use blyt32.coroutine.create() if this coroutine needs to survive saves.", 2)
    end
    return _raw_resume(co, ...)
end

-- Install the namespace.
blyt32 = blyt32 or {}
blyt32.coroutine = M

return M
