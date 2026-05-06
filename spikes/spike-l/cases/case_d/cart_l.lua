-- Spike L cart_l.lua — case_d wrapper that reacts visibly to the A button.
--
-- PLAN.md Stage 3 step 10: "Modify case d's Lua to react visibly to the A
-- button: holding A advances the frame counter at 2× rate (so a held button
-- changes on-screen output)." This file extends spike-i's main.lua without
-- forking it — main.lua remains the spike-i workload (frame counter +
-- mylib.add output via console_print).
--
-- The wrapper is loaded by the libretro adapter only — spike-i's run path
-- continues to use main.lua directly. The A-button accel logic depends on
-- a `console_button(0, 'A')` accessor exposed by libconsolelua under
-- spike-L's runtime build (analogous to console_print). The accessor is a
-- planned addition to libconsolelua noted in spike-l-results.md §open items.
--
-- For the synthetic-facade build of spike L (the floor case in this commit),
-- the A-button doubling logic lives in lib/blyt_facade.c blyt_runtime_update
-- where it uses BLYT_BUTTON_A directly — this file is only consumed once
-- the rv32emu-backed facade lands and the cart's Lua side participates in
-- input handling.

local mylib = require("mylib")
local frame = 0
local accel = 0

function init()
    frame = 0
    accel = 0
end

function update()
    frame = frame + 1
    if console_button and console_button(0, "A") then
        frame = frame + 1
        accel = accel + 1
    end
end

function draw()
    console_print("frame " .. (frame - 1)
                  .. " accel=" .. accel
                  .. " mylib=" .. mylib.add(3, 4) .. "\n")
end
