// Spike I Stage 4 Node driver — runs rv32emu.wasm with multi-arg argv so
// the patched fc32_dynload sees `-L /spike-i/lib /spike-i/cases/case_X/cart_X`.
//
// Usage: node node_run.cjs <cart_path> [<libdir>]
//   default libdir is /spike-i/lib

const path = require("path");
const fs   = require("fs");
const vm   = require("vm");

const cart = process.argv[2];
const libDir = process.argv[3] || "/spike-i/lib";
if (!cart) {
  console.error("usage: node node_run.cjs <cart_path> [<libdir>]");
  process.exit(2);
}

let src = fs.readFileSync(path.join(__dirname, "rv32emu.js"), "utf8");
src = src.replace(
  /^var Module=typeof Module!="undefined"\?Module:{};/,
  'var Module = (typeof globalThis !== "undefined" && globalThis.__rvModule) ? globalThis.__rvModule : {};'
);

// rv32emu's main.c calls EM_JS helpers that touch document — stub for Node.
globalThis.document = { getElementById: () => ({ disabled: false }) };

globalThis.__rvModule = {
  print:    (s) => process.stdout.write(s + "\n"),
  printErr: (s) => process.stderr.write(s + "\n"),
  onAbort:  (r) => { console.error("ABORT", r); process.exit(1); },
  onRuntimeInitialized: function () {
    try {
      this.run_user(["-L", libDir, cart]);
    } catch (e) {
      if (e && (e.name === "ExitStatus" || (e.context && e.context.name === "ExitStatus"))) {
        process.exit(e.status ?? (e.context && e.context.status) ?? 0);
      }
      console.error("uncaught from run_user:", e);
      process.exit(1);
    }
  },
};

const wrapped = "(function (exports, require, module, __filename, __dirname) {\n" + src + "\n})";
const fn = vm.runInThisContext(wrapped, { filename: "rv32emu.js" });
fn({}, require, { exports: {} }, path.join(__dirname, "rv32emu.js"), __dirname);
