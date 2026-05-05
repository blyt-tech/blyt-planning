-- Stage 4 step 16: variant where step_label is removed entirely.
-- The DAP server should mark the breakpoint at step_label's old line as
-- verified=false (hollow gutter in VS Code) on re-binding.
local frame = 0

function init()
    frame = 0
end

function update()
    frame = frame + 1
end

function draw()
    console_print("frame " .. (frame - 1) .. "\n")
end
