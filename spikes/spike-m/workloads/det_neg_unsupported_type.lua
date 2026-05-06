-- Spike M Stage 6 — negative test: constrained-shape violation.
--
-- A managed coroutine whose seed contains a function value (not in
-- the constrained shape) — flatten must throw
-- BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE before any byte is written.

local body = function (ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        coroutine.yield()
    end
end

local seed = {
    step = 0,
    -- A function value — explicitly outside the constrained shape.
    bad_fn = function () return 42 end,
}

local ok, err = pcall(blyt32.coroutine.create, body, seed)
print(string.format("CREATE ok=%s", tostring(ok)))
if not ok then
    print("STDERR " .. tostring(err))
end

console.add_misc(ok and 1 or 0)
console.commit_frame()
