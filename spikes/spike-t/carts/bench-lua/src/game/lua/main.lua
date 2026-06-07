local function bench(a, b, c, d)
    return a + b + c + d + 9, 4 -- 9 ≈ the three LUA_TNUMBER type tags
end

function init()
    local acc = 0
    for i = 1, 10000 do
        local s, n = bench(i, 2, 3, 4)
        acc = (acc + s + n) % 1000003
    end
    blyt32.debug.print("bench_lua:" .. acc)
end

function update() blyt.quit() end
function draw() end
