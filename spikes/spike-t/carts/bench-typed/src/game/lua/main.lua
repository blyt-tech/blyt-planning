function init()
    local m = require("spike")
    local acc = 0
    for i = 1, 10000 do
        acc = (acc + m.bench(i)) % 1000003
    end
    blyt32.debug.print("bench_typed:" .. acc)
end

function update() blyt.quit() end
function draw() end
