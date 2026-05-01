// Spike F user-pre.js — same shape as spike-e/rv32emu/assets/wasm/js/user-pre.js
// so the harness can hand off a cart name and we drive main() via callMain().
Module["noInitialRun"] = true;

Module["run_user"] = function (bench_name) {
  if (bench_name === undefined) {
    console.warn("bench name is undefined");
    return;
  }
  callMain([bench_name]);
};
