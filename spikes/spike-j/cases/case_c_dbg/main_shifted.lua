-- Stage 4 step c: same source as main.lua with five comment lines inserted
-- above step_label so its body line shifts. The DAP test asserts that
-- VS Code re-binds the breakpoint marker to the new line under
-- loadedSource(reason: "changed").
-- (Extra comment line for total 5.)
local frame = 0

local function step_label(n)
    local label = "tick=" .. n
    return label
end

function init()
    frame = 0
end

function update()
    frame = frame + 1
end

function draw()
    local label = step_label(frame - 1)
    console_print("frame " .. (frame - 1) .. " " .. label .. "\n")
end
