// Spike F node smoke test driver.
//
// Loads lua.js, swaps the IIFE-local Module init for a globalThis hand-off
// so we can pre-set print/onAbort/onRuntimeInitialized hooks before main()
// runs.  Calls Module.run_user(bench) once the runtime is initialised.
//
// Mirrors spike-e/web/node_run.cjs; the document stub is harmless even
// though our host.c never touches it.

const path = require("path");
const fs   = require("fs");
const vm   = require("vm");

const bench = process.argv[2];
if (!bench) {
  console.error("usage: node node_run.cjs <bench>");
  process.exit(2);
}

let src = fs.readFileSync(path.join(__dirname, "lua.js"), "utf8");
src = src.replace(
  /^var Module=typeof Module!="undefined"\?Module:{};/m,
  'var Module = (typeof globalThis !== "undefined" && globalThis.__luaModule) ? globalThis.__luaModule : {};'
);

globalThis.document = {
  getElementById: () => ({ disabled: false }),
};

globalThis.__luaModule = {
  print:    (s) => process.stdout.write(s + "\n"),
  printErr: (s) => process.stderr.write(s + "\n"),
  onAbort:  (r) => { console.error("ABORT", r); process.exit(1); },
  onRuntimeInitialized: function () {
    try {
      this.run_user(bench);
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
const fn = vm.runInThisContext(wrapped, { filename: "lua.js" });
fn({}, require, { exports: {} }, path.join(__dirname, "lua.js"), __dirname);
