local frame = 0
function init()   frame = 0 end
function update() frame = frame + 1 end
function draw()
    console_print("frame " .. (frame - 1) .. "\n")
end
