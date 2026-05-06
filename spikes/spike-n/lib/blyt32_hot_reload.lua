-- Spike N — blyt32.on_hot_reload_failed hook registry.
--
-- Provides the Lua-side surface for:
--   blyt32.on_hot_reload_failed = function(slot, reason) ... end
--
-- The hook is called by the C runtime (via hot_reload_diagnostic.c's
-- hot_reload_set_hook) when a slot cannot be migrated after reload.
-- Cart authors assign a function to this field; the runtime fires it
-- once per failed slot with the slot index and the diagnostic string.
--
-- Also provides:
--   blyt32.on_save_failed = function(reason) ... end
--
-- The save-failed hook fires when the pre-reload save (snapshot) fails,
-- e.g. because the constrained-shape flattener rejects a ctx table that
-- contains a function closure (BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE).
-- This is the "PASS-on-FLATTEN-ERROR" gate for l7.
--
-- Usage in a cart:
--   blyt32.on_hot_reload_failed = function(slot, reason)
--       print("reload failed for slot " .. slot .. ": " .. reason)
--   end
--
-- The assignment goes through the C binding console.set_on_hot_reload_failed
-- (registered in lua_det_bindings_n.c).  This module sets up the metatable
-- so that assigning to blyt32.on_hot_reload_failed calls the C binding.

if not blyt32 then blyt32 = {} end

-- Install __newindex so assignment to blyt32.on_hot_reload_failed
-- routes through the C binding.
local mt = getmetatable(blyt32) or {}
local _raw_newindex = mt.__newindex

mt.__newindex = function(t, k, v)
    if k == "on_hot_reload_failed" then
        if type(v) ~= "function" then
            error("blyt32.on_hot_reload_failed must be a function", 2)
        end
        console.set_on_hot_reload_failed(v)
        rawset(t, k, v)   -- also store for inspection
        return
    end
    if _raw_newindex then
        _raw_newindex(t, k, v)
    else
        rawset(t, k, v)
    end
end

setmetatable(blyt32, mt)

-- on_save_failed: called if the pre-reload ctx flatten fails.
-- Default: no-op.  Cart authors override.
if not blyt32.on_save_failed then
    blyt32.on_save_failed = function(reason) end
end
