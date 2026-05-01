// Spike F web worker — runs the Lua-direct-to-WASM module against a
// single benchmark .lua file (embedded into the WASM module's MEMFS).
//
// Pre-sets self.Module before importScripts so user-pre.js sees our
// hooks: noInitialRun=true is set by user-pre.js itself, run_user is
// installed by user-pre.js as `callMain([bench_name])`.
//
// Stdout (FRAME / SUMMARY) is forwarded to the main thread via
// postMessage.  Mirrors spike_e_worker.js so the host harness needed
// only minimal change.

let benchName = null;

self.Module = {
  print:    (s) => self.postMessage({ type: "stdout", line: s }),
  printErr: (s) => self.postMessage({ type: "stdout", line: s }),
  onAbort:  (r) => self.postMessage({ type: "error", msg: "abort: " + r }),
  onRuntimeInitialized: function () {
    self.postMessage({ type: "ready" });
    try {
      this.run_user(benchName);
    } catch (e) {
      const isExit = e && (e.name === "ExitStatus" ||
                           (e.context && e.context.name === "ExitStatus"));
      if (!isExit) {
        self.postMessage({ type: "error", msg: String(e && e.message || e) });
        return;
      }
    }
    // Note: same Emscripten flush ordering as spike-e — Chrome flushes
    // stdout post-task, so the SUMMARY line tends to arrive on the main
    // thread *after* this "done" message.  The harness gates on the
    // SUMMARY line, not on "done".
    self.postMessage({ type: "done" });
  },
};

self.onmessage = (ev) => {
  const m = ev.data;
  if (m.type === "run") {
    benchName = m.bench;
    try {
      importScripts("lua.js");
    } catch (e) {
      self.postMessage({ type: "error", msg: "importScripts failed: " + (e && e.message || e) });
    }
  }
};
