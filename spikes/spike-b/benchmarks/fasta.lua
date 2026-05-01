-- fasta.lua (adapted; string + table operations)
-- Generates DNA-style sequences and writes them to a buffer (no IO).

local IM, IA, IC = 139968, 3877, 29573
local last = 42
local function gen_random(max)
    last = (last * IA + IC) % IM
    return max * last / IM
end

local function repeat_fasta(s, n)
    local len = #s
    local buf = {}
    local pos = 0
    while n > 0 do
        local chunk = math.min(60, n)
        local out = {}
        for i = 1, chunk do
            pos = pos + 1
            if pos > len then pos = 1 end
            out[i] = s:sub(pos, pos)
        end
        buf[#buf + 1] = table.concat(out)
        n = n - chunk
    end
    return table.concat(buf, "\n")
end

local function random_fasta(table_, n)
    local buf = {}
    local cps = {}
    local total = 0
    for _, e in ipairs(table_) do total = total + e[2]; cps[#cps + 1] = { e[1], total } end
    while n > 0 do
        local chunk = math.min(60, n)
        local out = {}
        for i = 1, chunk do
            local r = gen_random(1)
            for _, c in ipairs(cps) do
                if r < c[2] then out[i] = c[1]; break end
            end
        end
        buf[#buf + 1] = table.concat(out)
        n = n - chunk
    end
    return table.concat(buf, "\n")
end

local IUB = {
    {"a", 0.27}, {"c", 0.12}, {"g", 0.12}, {"t", 0.27},
    {"B", 0.02}, {"D", 0.02}, {"H", 0.02}, {"K", 0.02},
    {"M", 0.02}, {"N", 0.02}, {"R", 0.02}, {"S", 0.02},
    {"V", 0.02}, {"W", 0.02}, {"Y", 0.02},
}

local HOMOSAP = {{"a", 0.30}, {"c", 0.20}, {"g", 0.20}, {"t", 0.30}}

local N = 200  -- small problem size for the spike
local s1 = repeat_fasta("ACGT", 2 * N)
local s2 = random_fasta(IUB,    3 * N)
local s3 = random_fasta(HOMOSAP, 5 * N)
return #s1 + #s2 + #s3
