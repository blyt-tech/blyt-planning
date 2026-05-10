local mylib = require("mylib")
local frame = 0
function init()   frame = 0 end
function update() frame = frame + 1 end
function draw()
    local add_result = mylib.fast_add(3.0, 4.0)
    local mul_result = mylib.fast_mul(3, 4)
    console_print("FRAME " .. (frame - 1) .. " add=" .. add_result .. " mul=" .. mul_result .. "\n")
end
