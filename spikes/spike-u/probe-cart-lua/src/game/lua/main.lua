-- frame is deliberately plain Lua state (not a state buffer) to demonstrate
-- serialising static state in on_save_state/on_load_state.
local frame

function init()
    frame = 0
end

function on_new_state()
    blyt.buf.alloc_slot(S.GLOBALS)
    local slot = blyt.buf.alloc_slot(S.CHARACTER)
    S.globals[0].player = blyt.buf.ref(S.CHARACTER, slot)
    S.character[slot].x = 160
    S.character[slot].y = 120
    blyt.debug.print("init player pos: 160, 120")
end

function update()
    frame = frame + 1
    if frame % 10 == 0 then
        local player = S.globals[0].player
        if blyt.buf.ref_valid(S.CHARACTER, player) then
            local slot = blyt.buf.ref_slot(player)
            local x = (S.character[slot].x + 1) % 320
            local y = (S.character[slot].y + 1) % 240
            S.character[slot].x = x
            S.character[slot].y = y
            -- Distinguish value-correctness from printf-float formatting:
            local v_floor = math.floor(0.5 * 1000)       -- expect 500 (int)
            local third_floor = math.floor((1.0/3.0)*1e6) -- expect 333333 (int)
            local cmp = tostring(0.5 == 0.5) .. tostring(0.5 < 1.0) -- truetrue
            local s = 0.0
            for i = 1, frame do s = s + 1.0 / i end
            local harm_floor = math.floor(s * 1000)      -- int view of f64 sum
            local half = tostring(0.5)                   -- printf %g path
            blyt.debug.print("update frame " .. frame ..
                " vfloor=" .. v_floor .. " thirdfloor=" .. third_floor ..
                " cmp=" .. cmp .. " harmfloor=" .. harm_floor ..
                " half=" .. half)
        end
    end
end

function draw()
    if frame % 10 == 0 then
        local slot = blyt.buf.ref_slot(S.globals[0].player)
        blyt.debug.print("draw frame " .. frame ..
                         " player pos: " .. S.character[slot].x ..
                         ", " .. S.character[slot].y)
    end
end

function on_save_state()
    S.globals[0].frame = frame
end

function on_load_state(info)
    frame = S.globals[0].frame
end
