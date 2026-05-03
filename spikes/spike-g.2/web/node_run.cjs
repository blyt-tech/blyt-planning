// Spike G.2 node driver.  Loads throttle_lua_<suffix>.js, runs one benchmark,
// and prints FRAME / SUMMARY lines.
//
// Usage: node node_run.cjs <bench> [<suffix>]
//   <suffix> defaults to "D0" (throttle_lua_D0.js — noop hook build).

const path = require("path");
const fs   = require("fs");
const vm   = require("vm");

const bench  = process.argv[2];
const suffix = process.argv[3] || "D0";
if (!bench) {
  console.error("usage: node node_run.cjs <bench> [<suffix>]");
  process.exit(2);
}

const jsFile = path.join(__dirname, `throttle_lua_${suffix}.js`);

let src = fs.readFileSync(jsFile, "utf8");
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
const fn = vm.runInThisContext(wrapped, { filename: jsFile });
fn({}, require, { exports: {} }, jsFile, __dirname);
