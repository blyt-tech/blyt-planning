local mylib = require("mylib")
local frame = 0
function init()   frame = 0 end
function update() frame = frame + 1 end
function draw()
    console_print("frame " .. (frame - 1) .. " mylib=" .. mylib.add(3, 4) .. "\n")
end
