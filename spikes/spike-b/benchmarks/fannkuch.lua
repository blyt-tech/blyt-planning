-- fannkuch.lua  (adapted; tight loops + table indexing)
-- Counts max flips required to bring 1 to the front across all permutations.

local function fannkuch(n)
    local p, q, s = {}, {}, {}
    for i = 1, n do p[i] = i; q[i] = i; s[i] = i end
    local sign, maxflips, sum = 1, 0, 0
    while true do
        local q1 = p[1]
        if q1 ~= 1 then
            for i = 2, n do q[i] = p[i] end
            local flips = 1
            while true do
                local qq = q[q1]
                if qq == 1 then
                    if flips > maxflips then maxflips = flips end
                    if sign == 1 then sum = sum + flips else sum = sum - flips end
                    break
                end
                q[q1] = q1
                if q1 >= 4 then
                    local i, j = 2, q1 - 1
                    while i < j do
                        q[i], q[j] = q[j], q[i]
                        i = i + 1; j = j - 1
                    end
                end
                q1 = qq
                flips = flips + 1
            end
        end
        if sign == 1 then
            p[2], p[1] = p[1], p[2]
            sign = -1
        else
            p[2], p[3] = p[3], p[2]
            sign = 1
            local i = 3
            while i <= n do
                local sx = s[i]
                if sx ~= 1 then s[i] = sx - 1; break end
                if i == n then return sum, maxflips end
                s[i] = i
                local first = p[1]
                for k = 1, i do p[k] = p[k + 1] end
                p[i + 1] = first
                i = i + 1
            end
        end
    end
end

local N = 7  -- 7! = 5040 permutations; small enough for one frame
return fannkuch(N)
