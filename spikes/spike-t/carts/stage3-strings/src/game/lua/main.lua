-- Spike T stage 3: string round-trip (small + >4 KiB retry + embedded NUL)
-- and the bridged error model (catchable, coroutine resumable, 1000x).

local function checksum(s)
    local sum = 0
    for i = 1, #s do
        sum = (sum + string.byte(s, i) * i) % 1000003
    end
    return sum
end

function init()
    local m = require("spike")

    -- Small string round-trip.
    local up, n = m.echo_upper("hello, Blyt!")
    blyt32.debug.print("str_small:" .. up .. "|" .. n)

    -- >4 KiB string: forces the guest arena TOLSTRING retry path.
    local big = string.rep("abcXYZ09-", 600) -- 5400 bytes
    local upbig, bign = m.echo_upper(big)
    blyt32.debug.print("str_big:" .. bign .. "|" .. checksum(upbig))

    -- Embedded NULs survive pushlstring (length-delimited).
    local z = m.nul_roundtrip()
    blyt32.debug.print("str_nul:" .. #z .. "|" .. string.byte(z, 1) .. ","
        .. string.byte(z, 2) .. "," .. string.byte(z, 3) .. ","
        .. string.byte(z, 4) .. "," .. string.byte(z, 5))

    -- Bridged errors are catchable; the coroutine stays healthy.
    local ok, err = pcall(m.fail, "once")
    blyt32.debug.print("err_caught:" .. tostring(ok) .. "|" .. tostring(err))

    -- 1000 consecutive error calls: guest registers/stack must not drift
    -- (the host restores the begin-call snapshot on every error).
    local count = 0
    for i = 1, 1000 do
        local ok2 = pcall(m.fail, "again")
        if not ok2 then
            count = count + 1
        end
    end
    blyt32.debug.print("err_repeat:" .. count)

    -- The bridge still works after all those unwinds.
    local again = m.echo_upper("still alive")
    blyt32.debug.print("str_after_errors:" .. again)
end

function update()
    blyt.quit()
end

function draw() end
