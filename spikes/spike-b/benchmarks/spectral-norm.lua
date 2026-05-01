-- spectral-norm.lua (adapted; float inner products, sqrt at end)

local function A(i, j) return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1) end

local function Av(n, x, y)
    for i = 0, n - 1 do
        local a = 0
        for j = 0, n - 1 do a = a + A(i, j) * x[j] end
        y[i] = a
    end
end

local function Atv(n, x, y)
    for i = 0, n - 1 do
        local a = 0
        for j = 0, n - 1 do a = a + A(j, i) * x[j] end
        y[i] = a
    end
end

local function AtAv(n, x, y, t)
    Av(n, x, t)
    Atv(n, t, y)
end

local N = 64
local u, v, t = {}, {}, {}
for i = 0, N - 1 do u[i] = 1 end
for _ = 1, 5 do
    AtAv(N, u, v, t)
    AtAv(N, v, u, t)
end
local vBv, vv = 0, 0
for i = 0, N - 1 do
    vBv = vBv + u[i] * v[i]
    vv  = vv  + v[i] * v[i]
end
return math.sqrt(vBv / vv)
