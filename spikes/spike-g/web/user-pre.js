// Spike G user-pre.js — same shape as spike-f/web/user-pre.js.
Module["noInitialRun"] = true;

Module["run_user"] = function (bench_name) {
  if (bench_name === undefined) {
    console.warn("bench name is undefined");
    return;
  }
  callMain([bench_name]);
};
