// Spike E web worker — runs the rv32emu WASM module against a single cart.
//
// The worker imports rv32emu.js via importScripts.  We pre-set self.Module
// before importing so that the IIFE-local `var Module = typeof Module !=
// "undefined" ? Module : {};` picks up our hooks.  user-pre.js (baked into
// rv32emu.js) then sets noInitialRun=true and exposes Module.run_user;
// our onRuntimeInitialized hook calls run_user(cart) once init completes.
//
// Stdout from the cart (FRAME / SUMMARY lines) is forwarded to the main
// thread one line at a time via postMessage.

// rv32emu's main.c calls EM_JS helpers that touch document.getElementById
// (disable_run_button, etc).  Workers have no document — stub it.
self.document = {
  getElementById: () => ({ disabled: false }),
};

let cartName = null;

self.Module = {
  print:    (s) => self.postMessage({ type: "stdout", line: s }),
  printErr: (s) => self.postMessage({ type: "stdout", line: s }),
  onAbort:  (r) => self.postMessage({ type: "error", msg: "abort: " + r }),
  onRuntimeInitialized: function () {
    self.postMessage({ type: "ready" });
    try {
      this.run_user(cartName);
    } catch (e) {
      // ExitStatus thrown via quit_ when the cart's main returns is normal
      // completion; rethrowing was the cause.  Other exceptions are real.
      const isExit = e && (e.name === "ExitStatus" ||
                           (e.context && e.context.name === "ExitStatus"));
      if (!isExit) {
        self.postMessage({ type: "error", msg: String(e && e.message || e) });
        return;
      }
    }
    // We post "done" so the harness can show "worker finished", but we do
    // NOT use it as the cart-finished signal — Emscripten flushes stdout
    // post-task on Chrome, so the trailing FRAME / SUMMARY messages arrive
    // *after* this one.  The harness publishes results from SUMMARY.
    self.postMessage({ type: "done" });
  },
};

self.onmessage = (ev) => {
  const m = ev.data;
  if (m.type === "run") {
    cartName = m.cart;
    try {
      importScripts("rv32emu.js");
    } catch (e) {
      self.postMessage({ type: "error", msg: "importScripts failed: " + (e && e.message || e) });
    }
  }
};
