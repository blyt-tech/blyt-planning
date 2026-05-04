// Spike I — emscripten pre-js. Defers main() until run_user is called with
// an explicit argv array, so the Node driver can pass `-L <libdir> <cart>`.
Module["noInitialRun"] = true;

Module["run_user"] = function (argv) {
  if (!argv || !argv.length) {
    console.warn("run_user: empty argv");
    return;
  }
  callMain(argv);
};
