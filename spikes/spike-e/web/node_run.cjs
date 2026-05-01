// Spike-E node smoke test driver.
//
// Loads rv32emu.js, swaps the IIFE-local Module init for a globalThis hand-off
// so we can pre-set print/onAbort hooks, then calls Module.run_user(cart) once
// the runtime is initialised.
//
// rv32emu's main.c calls a couple of EM_JS functions that touch document
// (disable_run_button, etc) — we stub document so those no-op in Node.

const path = require("path");
const fs   = require("fs");
const vm   = require("vm");

const cart = process.argv[2];
if (!cart) {
  console.error("usage: node node_run.cjs <cart.elf>");
  process.exit(2);
}

let src = fs.readFileSync(path.join(__dirname, "rv32emu.js"), "utf8");
src = src.replace(
  /^var Module=typeof Module!="undefined"\?Module:{};/,
  'var Module = (typeof globalThis !== "undefined" && globalThis.__rvModule) ? globalThis.__rvModule : {};'
);

globalThis.document = {
  getElementById: () => ({ disabled: false }),
};

globalThis.__rvModule = {
  print:    (s) => process.stdout.write(s + "\n"),
  printErr: (s) => process.stderr.write(s + "\n"),
  onAbort:  (r) => { console.error("ABORT", r); process.exit(1); },
  onRuntimeInitialized: function () {
    try {
      this.run_user(cart);
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
