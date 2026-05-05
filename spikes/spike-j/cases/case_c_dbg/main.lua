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
