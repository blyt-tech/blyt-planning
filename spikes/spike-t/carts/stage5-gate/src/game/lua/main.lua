-- Spike T stage 5: 10-frame combined gate.  Per-frame line:
--   gate:<frame>|<ok>|<digest-or-error>|<label>|<acc>
-- Byte-identical streams across rv32 and WASM = PASS.

local m = require("spike")
local state = { acc = 0, name = "blyt", mode = "gate" }
local frame = 0

function init()
    blyt32.debug.print("gate:init")
end

function update()
    frame = frame + 1
    local ok, digest, label = pcall(m.step, state, frame)
    if ok then
        blyt32.debug.print("gate:" .. frame .. "|ok|" .. digest .. "|" .. label
            .. "|" .. state.acc)
    else
        blyt32.debug.print("gate:" .. frame .. "|err|" .. tostring(digest))
    end
    if frame >= 10 then
        blyt.quit()
    end
end

function draw() end
