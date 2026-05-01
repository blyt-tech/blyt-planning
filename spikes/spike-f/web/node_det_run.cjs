// Spike F determinism cross-check — Node driver for lua_det.js.
//
// Mirrors node_run.cjs but loads lua_det.js (the det_host.c build) instead
// of lua.js.  Runs a single det workload by name and lets the host print
// its own "=== <name> ===" header and DIGEST lines to stdout.
//
// Usage (from build/):
//   node node_det_run.cjs det_doom_tick
//   node node_det_run.cjs det_entity_update

const path = require("path");
const fs   = require("fs");
const vm   = require("vm");

const workload = process.argv[2];
if (!workload) {
  console.error("usage: node node_det_run.cjs <workload>");
  process.exit(2);
}

let src = fs.readFileSync(path.join(__dirname, "lua_det.js"), "utf8");
src = src.replace(
  /^var Module=typeof Module!="undefined"\?Module:{};/m,
  'var Module = (typeof globalThis !== "undefined" && globalThis.__luaDetModule) ? globalThis.__luaDetModule : {};'
);

// Emscripten's main.c in rv32emu touches document; det_host.c does not, but
// the stub is harmless and avoids any reference errors in the WASM glue.
globalThis.document = {
  getElementById: () => ({ disabled: false }),
};

globalThis.__luaDetModule = {
  print:    (s) => process.stdout.write(s + "\n"),
  printErr: (s) => process.stderr.write(s + "\n"),
  onAbort:  (r) => { console.error("ABORT", r); process.exit(1); },
  onRuntimeInitialized: function () {
    try {
      this.run_user(workload);
    } catch (e) {
      if (e && (e.name === "ExitStatus" ||
                (e.context && e.context.name === "ExitStatus"))) {
        process.exit(e.status ?? (e.context && e.context.status) ?? 0);
      }
      console.error("uncaught from run_user:", e);
      process.exit(1);
    }
  },
};

const wrapped = "(function (exports, require, module, __filename, __dirname) {\n" + src + "\n})";
const fn = vm.runInThisContext(wrapped, { filename: "lua_det.js" });
fn({}, require, { exports: {} }, path.join(__dirname, "lua_det.js"), __dirname);
