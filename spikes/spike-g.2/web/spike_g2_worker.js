// Spike G.2 web worker — runs the throttle_host WASM module against one bench.

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
    self.postMessage({ type: "done" });
  },
};

self.onmessage = (ev) => {
  const m = ev.data;
  if (m.type === "run") {
    benchName = m.bench;
    try {
      importScripts("throttle_lua.js");
    } catch (e) {
      self.postMessage({ type: "error", msg: "importScripts failed: " + (e && e.message || e) });
    }
  }
};
