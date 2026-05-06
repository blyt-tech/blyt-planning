-- Spike M Stage 5 — transient-coroutine boundary-cross negative test.
--
-- A transient coroutine (created via stock `coroutine.create`, NOT
-- `blyt32.coroutine.create`) that yields once and is suspended when
-- the save snapshot is taken.  On a load resume the cart re-creates
-- the transient from scratch (Lua state is fresh) and manually marks
-- it as boundary-crossed via `blyt32.coroutine.mark_boundary_crossed`
-- to model the runtime behavior production would auto-handle via a
-- wrapper-managed transient ID list saved alongside cart state.
--
-- The cart's resume of `trans` at frame 10 is wrapped in `pcall`; the
-- wrapper's resume hook throws the ADR-0012 canonical error string,
-- pcall catches it, and the cart prints the prefix-tagged error to
-- stdout for harness comparison across hosts.
--
-- Per-frame digest fold: console.add_misc(0) every frame except 10,
-- where the resume is attempted.  At frame 10 the resume must fail
-- on a load run that crossed the boundary; the cart records the
-- failure as add_misc(2), which is distinct from the success
-- (add_misc(1)) and the no-attempt (add_misc(0)) cases.

local trans = coroutine.create(function ()
    coroutine.yield()
    return "after_yield"
end)
coroutine.resume(trans)  -- prime: yields once.

-- On a load resume, the freshly-created `trans` corresponds to the
-- one that was suspended at save time.  Mark it boundary-crossed
-- so the next `coroutine.resume(trans)` throws.
if console.is_load_resume() then
    blyt32.coroutine.mark_boundary_crossed(trans)
end

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    if f == 10 then
        local ok, err = pcall(coroutine.resume, trans)
        if ok then
            -- Resume returned normally — same-host straight-through
            -- and load-from-S<5 paths take this branch.  `coroutine.resume`
            -- itself returns (status, value); pcall wraps that, so
            -- err is the inner ok and the third value is the inner result.
            console.add_misc(1)
        else
            -- Resume threw — load resume past the save boundary.
            -- Print the error to stdout with a stable prefix so the
            -- harness can grep / diff it across hosts.
            print("STDERR " .. tostring(err))
            console.add_misc(2)
        end
    else
        console.add_misc(0)
    end
    console.commit_frame()
end
