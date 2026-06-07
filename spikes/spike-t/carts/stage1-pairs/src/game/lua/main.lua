-- Spike T stage 1: pairs() iteration order must be identical on the rv32
-- and WASM paths.  Requires the fixed Lua hash seed (ADR-0130 / ADR-0066
-- amendment); with the stock ASLR+time seed this output differs per run.

local keys = {
    "alpha", "bravo", "charlie", "delta", "echo", "foxtrot",
    "golf", "hotel", "india", "juliet", "kilo", "lima",
    "mike", "november", "oscar", "papa",
}

local function dump(label, t)
    local out = label .. ":"
    for k, v in pairs(t) do
        out = out .. k .. "=" .. v .. ";"
    end
    blyt32.debug.print(out)
end

function init()
    -- Insertion history A: forward order.
    local t = {}
    for i = 1, #keys do
        t[keys[i]] = i
    end
    dump("pairs_fwd", t)

    -- Insertion history B: reverse order (different history, so possibly
    -- different order than A — but each must match across paths).
    local u = {}
    for i = #keys, 1, -1 do
        u[keys[i]] = i
    end
    dump("pairs_rev", u)

    -- Mixed integer + string keys.
    local m = {}
    for i = 1, 8 do
        m[i] = i * 10
        m[keys[i]] = i
    end
    dump("pairs_mixed", m)
end

function update()
    blyt.quit()
end

function draw() end
