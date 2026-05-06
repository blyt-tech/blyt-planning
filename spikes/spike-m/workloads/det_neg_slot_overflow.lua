-- Spike M Stage 6 — negative test: slot table overflow.
--
-- Creates MAX+1 = 65 managed scripts in a row.  The 65th call must
-- error with the canonical `BLYT_ERR_SLOT_EXHAUSTED` string.  The
-- cart catches via pcall and prints the error to stdout with a
-- `STDERR ` prefix for harness comparison.

local body = function (ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        coroutine.yield()
    end
end

local count = 0
local err_msg
for i = 1, 100 do
    local ok, e = pcall(blyt32.coroutine.create, body, { step = 0 })
    if not ok then
        err_msg = e
        break
    end
    count = count + 1
end

print(string.format("ALLOC count=%d", count))
if err_msg then
    print("STDERR " .. tostring(err_msg))
end

-- Single trivial commit so the cart produces at least one DIGEST
-- line for the harness to grep.
console.add_misc(count)
console.commit_frame()
