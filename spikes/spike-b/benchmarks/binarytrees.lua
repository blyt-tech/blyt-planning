-- binarytrees.lua  (adapted from lua.org/benchmarks; sized for one frame)
-- GC-pressure microbenchmark.  Builds a tree, traverses, discards, repeats.

local function buildTree(depth)
    if depth == 0 then return { item = 0 } end
    return { item = 0, left = buildTree(depth - 1), right = buildTree(depth - 1) }
end

local function checkTree(node)
    if not node.left then return node.item end
    return node.item + checkTree(node.left) - checkTree(node.right)
end

local DEPTH = 10  -- 2^10 = 1024 nodes per tree; tune for the frame budget
local TREES = 8

local sum = 0
for i = 1, TREES do
    local t = buildTree(DEPTH)
    sum = sum + checkTree(t)
end
return sum
