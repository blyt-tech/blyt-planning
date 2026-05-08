fn main() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let out = std::env::var("OUT_DIR").unwrap();

    // Stage 4: invoke the stub packer to generate resources.rs, state.rs,
    // handlers.rs into $OUT_DIR.  The packer binary is built by the Makefile
    // before cargo build runs.
    //
    // Path is relative to the crate root, not the working directory at
    // build.rs execution time, so we use CARGO_MANIFEST_DIR (ADR-0088 risk
    // note in PLAN.md §Risk notes).
    let packer = format!("{}/../../harness/stub_packer_rust", manifest_dir);

    // Only invoke the packer if the binary exists (stages 1-3 use hardcoded
    // constants in lib.rs; stage 4 wires the packer).
    if std::path::Path::new(&packer).exists() {
        let status = std::process::Command::new(&packer)
            .args(["--lang", "rust", "--out", &out, "--cart",
                   &format!("{}/cart.build.yaml", manifest_dir)])
            .status()
            .expect("stub_packer_rust failed to run");
        assert!(status.success(), "stub_packer_rust exited non-zero");
        println!("cargo:rerun-if-changed={}/cart.build.yaml", manifest_dir);
    }

    println!("cargo:rerun-if-changed=build.rs");
}
