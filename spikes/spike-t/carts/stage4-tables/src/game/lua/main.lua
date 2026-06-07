-- Spike T stage 4: config-in / result-table-out through the bridge,
-- in-place table mutation, and fixed-seed lua_next order parity.

function init()
    local m = require("spike")

    local cfg = {
        name = "blyt",
        scale = 3,
        items = { 1, 2, 3, 4 },
        flag = true,
        extra = "e",
    }
    local r = m.summarize(cfg)
    blyt32.debug.print("tbl_name:" .. r.name)
    blyt32.debug.print("tbl_total:" .. r.total)
    blyt32.debug.print("tbl_keys:" .. r.keys)
    blyt32.debug.print("tbl_n:" .. r.n)

    local t = {}
    m.fill(t)
    blyt32.debug.print("tbl_fill:" .. t.answer .. "," .. t[1])

    -- Error path still clean with tables on the exchange stack.
    local ok, err = pcall(m.summarize, { name = "x", scale = 1, items = 7 })
    blyt32.debug.print("tbl_err:" .. tostring(ok) .. "|" .. tostring(err))
end

function update()
    blyt.quit()
end

function draw() end
