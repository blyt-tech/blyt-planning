local frame, persisted
function init() frame = 0; persisted = 0 end
function on_new_state()
    blyt.buf.alloc_slot(S.GLOBALS)
    local slot = blyt.buf.alloc_slot(S.CHARACTER)
    S.globals[0].player = blyt.buf.ref(S.CHARACTER, slot)
    S.character[slot].x = 160; S.character[slot].y = 120
end
function update()
    frame = frame + 1
    -- double computation whose result we persist (as int, since buffers are f32/i32)
    local d = 0.0
    for i = 1, frame do d = d + 1.0/(i*i) end   -- partial Basel sum (f64)
    persisted = math.floor(d * 1e9)              -- double-derived integer
    if frame % 10 == 0 then
        blyt.debug.print("f"..frame.." basel*1e9="..persisted.." (from f64 sum)")
    end
end
function draw() end
function on_save_state() S.globals[0].frame = frame; S.globals[0].basel = persisted end
function on_load_state(info) frame = S.globals[0].frame; persisted = S.globals[0].basel end
