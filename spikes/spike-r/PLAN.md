# Spike R — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §R):** Can a
hand-written raw BPF seccomp filter correctly implement two-phase
enforcement for a fork/exec launcher, where the launcher is LP64
(`AUDIT_ARCH_RISCV64`) and the cart process after exec is ILP32
(`AUDIT_ARCH_RISCV32`), given that `libseccomp` has no
`SCMP_ARCH_RISCV32` constant?

**Status:** Not yet started.

---

## What we already know from Spike H

Spike H Stage 3 (Fedora 42, 2026-05-04) established:

- The seccomp-bpf KILL_PROCESS mechanism works correctly on the Fedora 42
  kernel (6.16.4). All four adversary probes (open, socket, execve,
  mprotect-exec) produced SIGSYS as expected.
- The `uname` allowed probe also produced SIGSYS — not because the filter
  blocked it explicitly, but because `libseccomp`'s LP64 filter sees all
  ILP32 syscalls as an unknown architecture and applies the default
  KILL_PROCESS action. Both the allowed and forbidden probes were killed
  by the same arch-mismatch path.
- The Spike H filter therefore gives the *right observable outcome*
  (forbidden probes killed) but for the wrong reason on the ILP32 side.
  A filter that explicitly handles `AUDIT_ARCH_RISCV32` would distinguish
  between allowed and forbidden ILP32 syscalls, making `uname` succeed.

The existing Spike H infrastructure — Fedora 42 QEMU environment,
`launcher.c`, `adversary.c`, `run-guest-tests.sh`, Makefile targets — is
reused directly. Spike R builds on top of it.

---

## Design question: single-filter arch-dispatch vs sequential phases

ADR-0116 describes a "two-phase" scheme: phase 1 loaded pre-exec (allows
`execve`), phase 2 loaded by the cart post-exec (removes `execve`, tighter
rules). This is a natural mental model, but seccomp's multi-filter
semantics create a constraint worth examining before committing to the
implementation.

**Multi-filter semantics (from `seccomp(2)`):** filters are evaluated LIFO
(most recently loaded first). If the most recent filter returns
`SECCOMP_RET_ALLOW`, evaluation continues to the previous filter.
The first non-ALLOW result along that chain takes precedence. Crucially,
a later-loaded filter can make things *more* restrictive (ALLOW → KILL)
but a later ALLOW cannot override an earlier KILL.

**The problem:** if phase 1 uses `SECCOMP_RET_KILL_PROCESS` as its default
action and contains no rule for `AUDIT_ARCH_RISCV32`, then all ILP32
syscalls are killed by phase 1. A phase 2 filter loaded by the now-ILP32
cart process that returns `SECCOMP_RET_ALLOW` for those syscalls cannot
override phase 1's KILL (ALLOW from phase 2 → continue to phase 1 →
KILL from phase 1 → result: KILL). Phase 2 can also never be loaded at
all if the `seccomp()` syscall itself is blocked before the cart gets to
run its startup code.

**Two practical alternatives:**

*Option A — Single arch-dispatch filter.* One filter, loaded before exec,
handles both archs:
```
check arch == RISCV64:  apply LP64 allowlist (including execve)
check arch == RISCV32:  apply ILP32 allowlist (no execve)
unknown arch:           KILL_PROCESS
```
After exec the process is ILP32; only the ILP32 branch of the same filter
applies. execve is not in the ILP32 rules. No "phase 2" is loaded. This
is simple and correct.

*Option B — Two filters, phase 1 explicitly allows all ILP32.* Phase 1
explicitly adds `SECCOMP_RET_ALLOW` rules for `AUDIT_ARCH_RISCV32` for
every syscall (i.e., a permissive ILP32 pass-through). Phase 2, loaded
by the cart, then restricts: for blocked ILP32 syscalls phase 2 returns
KILL, which wins over phase 1's ALLOW. The LP64 launcher process never
makes ILP32 syscalls, so the phase 1 pass-through is inert for it. After
exec, the LP64 rules in phase 1 no longer match (arch is RISCV32), so
only the ILP32 pass-through and phase 2's restrictions apply.

Option B preserves the two-phase structure but is more complex and relies
on the cart being trusted to load phase 2 before issuing any sensitive
syscall — a property that's hard to guarantee for hostile cart code.

**The spike's job** is to determine empirically which option is correct and
implement it. Stage 1 should verify the multi-filter semantics as the
first gate before committing to either option. Option A is the recommended
starting point; Option B should be implemented only if Option A is
insufficient for the stated goals.

---

## What we are NOT building

- The full production runtime. Spike R validates the seccomp mechanism
  and derives the allowlist. Wiring this into the runtime is an
  implementation task after the spike.
- The cgroups quota or namespace isolation path. Both are confirmed by
  Spike H and are not re-proven here.
- A libseccomp patch. The raw BPF approach makes libseccomp irrelevant
  for this purpose.
- Milk-V Duo hardware validation. Fedora 42 QEMU is the test environment;
  hardware confirms the real syscall set when the board is available.

---

## Approach

### Stage 0 — environment check

Spike R reuses the Spike H Fedora 42 QEMU environment. No new VM setup
is needed; the Makefile target from Spike H (`make qemu-test-fedora`)
and the SSH key/network configuration are the starting point.

1. Confirm the Spike H environment is still functional:
   ```
   make -C spikes/spike-h qemu-test-fedora
   # Stages 0–4 still pass (or identify any environment drift)
   ```

2. Confirm the `seccomp()` syscall number is reachable from the Fedora 42
   guest. On RISC-V Linux, `seccomp` is syscall 277. Verify:
   ```
   grep seccomp /usr/include/asm/unistd.h   # or asm-generic
   ```

---

### Stage 1 — raw BPF arch-dispatch filter

Build a minimal C program (`seccomp_raw_test.c`) that installs a
hand-written `struct sock_filter[]` filter using
`seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog)` and then probes the result.

**Define the arch constants:**
```c
/* Not in libseccomp; define manually. */
#define AUDIT_ARCH_RISCV64  0xC00000F3U   /* EM_RISCV | AUDIT_ARCH_64BIT | LE */
#define AUDIT_ARCH_RISCV32  0x400000F3U   /* EM_RISCV | LE (no 64-bit flag)   */
```

These come from `<linux/audit.h>` semantics:
- `AUDIT_ARCH_64BIT` = `0x80000000`
- `__AUDIT_ARCH_LE`  = `0x40000000`
- `EM_RISCV`         = `0x00F3`
- LP64: `0x80000000 | 0x40000000 | 0xF3` = `0xC00000F3`
- ILP32: `0x40000000 | 0xF3` = `0x400000F3`

**Filter layout (Option A, single arch-dispatch):**
```c
struct sock_filter filter[] = {
    /* Load seccomp_data.arch (offset 4) */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 4),

    /* If RISCV64 jump to LP64 allowlist, else fall through */
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, LP64_OFFSET, 0),

    /* If RISCV32 jump to ILP32 allowlist, else kill (unknown arch) */
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV32, ILP32_OFFSET, 0),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

    /* LP64 allowlist: load nr (offset 0), compare, allow or kill */
    /* [LP64_OFFSET]: */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execve, 0, 1), /* execve: allow */
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    /* ... more LP64 rules ... */
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

    /* ILP32 allowlist: load nr, compare, allow or kill */
    /* [ILP32_OFFSET]: */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),
    /* ... ILP32 rules ... */
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
};
```

Note: the `LP64_OFFSET` and `ILP32_OFFSET` values in the jump instructions
are relative forward-jump counts that must be computed from the actual
array indices. Use a helper macro or hand-count. A short helper that
generates the filter programmatically (rather than as a literal array)
is simpler and less error-prone — see `seccomp_bpf.h` patterns from
libseccomp or the kernel samples.

3. Build `seccomp_raw_test.c` as an RV64 binary (runs inside the Fedora 42
   guest). The test program:
   - Installs the arch-dispatch filter (with just `uname` in the ILP32
     allowlist and `execve` + `exit_group` in the LP64 allowlist).
   - Calls `uname()` from an ILP32 helper process (fork + execve to a
     minimal RV32 binary that calls uname).
   - Reports whether `uname` succeeds or produces SIGSYS.

4. Gate: `uname` from the ILP32 process returns 0 (allowed). `open` from
   the ILP32 process produces SIGSYS (blocked). This confirms that the
   raw BPF filter correctly differentiates allowed and forbidden ILP32
   syscalls — which Spike H's libseccomp filter could not.

5. **Verify multi-filter semantics empirically.** Install phase 1 (with
   KILL as ILP32 default) then install a phase 2 that ALLOWs a specific
   ILP32 syscall. Check whether the syscall is killed or allowed. This
   confirms the semantics described in the design section above and
   determines whether Option A or Option B is necessary.
   Record the result clearly in the findings.

---

### Stage 2 — launcher integration

Extend the Spike H `launcher.c` (or write `launcher_r.c` alongside it)
to use the raw BPF filter from Stage 1 instead of libseccomp.

6. Replace `spike_h_install_seccomp()` with `install_raw_bpf_filter()`.
   The new function builds the `struct sock_filter[]` array using the
   arch-dispatch layout from Stage 1, including both LP64 and ILP32
   rules, and loads it via `seccomp(SECCOMP_SET_MODE_FILTER, ...)`.

   If Stage 1 determines Option A (single filter) is sufficient, the
   launcher loads one filter before exec and nothing more is needed.

   If Option B is required (two filters), the launcher loads a permissive
   ILP32 phase 1, and the cart binary (a new `cart_startup.c` stub) loads
   the restrictive phase 2 at startup before calling any application code.

7. Run the Spike H adversary probes through the new launcher:
   ```
   ./launcher_r ./adversary
   ```
   All four forbidden probes (open, socket, execve, mprotect-exec) must
   produce SIGSYS. The `uname` probe must succeed (exit 0). This is the
   Stage 2 success gate — the same checks as Spike H Stage 3, but now
   with the ILP32 arch correctly handled.

8. Confirm that `execve` is blocked for the ILP32 cart process. After
   exec, the cart cannot call `execve`. The adversary already tests this;
   confirm the result is SIGSYS and not EACCES or similar.

---

### Stage 3 — production allowlist derivation

The Spike H allowlist was derived from a minimal static musl RV32 process
with stub console calls. The production allowlist must cover rv32emu
running a real cart (graphics, audio, input, state, Lua, Rust).

9. Run each of the four Spike I cart workload types through rv32emu under
   `strace` on the Fedora 42 guest:

   ```bash
   strace -f -o strace_native_c.txt   rv32emu ./spike-i-case-a.blyt
   strace -f -o strace_lua.txt        rv32emu ./spike-i-case-b.blyt
   strace -f -o strace_rust.txt       rv32emu ./spike-o-cart.blyt
   strace -f -o strace_lua_rust.txt   rv32emu ./spike-q-cart.blyt
   ```

   Run each for at least one full second of cart execution (or 60 frames
   at 60 fps). The strace captures the full syscall set made by rv32emu
   itself (LP64) and any ILP32 syscalls from the cart via the compat layer.

10. Collect and deduplicate:
    ```bash
    grep -h '^[a-z]' strace_*.txt | sed 's/(.*//' | sort -u > syscall_union.txt
    ```
    Separate LP64 and ILP32 syscalls (the compat layer syscalls will appear
    with distinct numbers; confirm via `strace -v` or by cross-referencing
    the syscall numbers with `<asm/unistd.h>` for both rv32 and rv64).

11. Cull to the minimum:
    - Remove any syscall that is only needed in the launcher phase (e.g.,
      `execve`, `unshare`, `pivot_root`).
    - Remove any syscall that a namespace or rlimit already prevents
      (e.g., `open` in an empty mount namespace, `socket` in an empty
      network namespace).
    - Keep everything else.

12. Write the result as `seccomp_allowlist.h` in `spikes/spike-r/`. The
    header defines two `struct sock_filter[]` constants:
    `SECCOMP_PHASE1_FILTER` (or the single arch-dispatch filter if Option A)
    and optionally `SECCOMP_PHASE2_FILTER`. Include a comment for each
    allowed syscall explaining why it is needed.

    This header is the primary deliverable of the spike. It replaces
    Spike H's `seccomp_filter.c` and is what ADR-0116's production
    implementation uses.

---

### Stage 4 — adversary re-verification with real workload

Run the complete adversary verification with a real rv32emu cart workload
running in the background, to confirm the production allowlist does not
block anything needed.

13. Start rv32emu with one of the Stage 3 cart workloads under the
    `launcher_r` harness (which now installs the Stage 3 allowlist). Run
    for at least 5 seconds of cart execution. The cart must complete
    normally — no unexpected SIGSYS during the run. Log the strace output
    to confirm no blocked syscall appears.

14. Re-run all four Spike H adversary probes against the Stage 3 filter.
    In the same environment:
    - `open` → SIGSYS
    - `socket` → SIGSYS
    - `execve` → SIGSYS
    - `mprotect-exec` → SIGSYS
    - `uname` → exit 0 (success, not SIGSYS)

    All five must pass.

15. Commit `seccomp_allowlist.h`. Write the results to
    `docs/design/spike-r-results.md`.

---

## Risk notes

- **BPF jump offsets.** Raw `struct sock_filter[]` jump instructions use
  relative forward-jump counts (`jt` and `jf` fields). The two-arch
  dispatch adds indirection that makes these error-prone to hand-count.
  Use a helper that constructs the filter programmatically (loop over
  an allowlist array and emit jump instructions by computing offsets),
  or use `#define` macros for each allowlist section with known sizes.
  Alternatively: write the filter as a C program that emits the
  `struct sock_filter[]` initialiser — compile, run, capture output.

- **seccomp multi-filter semantics.** The interaction between phase 1 and
  phase 2 filters is non-obvious (see design discussion above). Stage 1
  step 5 resolves this empirically before Stage 2 commits to an
  implementation. Do not assume either option is correct before testing.

- **`PR_SET_NO_NEW_PRIVS`.** `seccomp()` in filter mode requires that
  `PR_SET_NO_NEW_PRIVS` has been set (unless the calling process has
  `CAP_SYS_ADMIN`). The launcher must call
  `prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)` before installing any filter.
  The Spike H launcher already does this via libseccomp's internal call;
  the raw BPF path must do it explicitly.

- **ILP32 syscall numbers on RISCV32.** RV32 Linux uses the same
  syscall numbers as RV64 for most calls (via the compat layer), but some
  32-bit compat syscalls have distinct numbers (e.g., `mmap2` = 192,
  `clock_gettime64` = 403). The strace output from Stage 3 will show the
  actual numbers used; cross-check against
  `arch/riscv/include/uapi/asm/unistd.h` and
  `include/uapi/asm-generic/unistd.h` for both the compat and native
  variants.

- **rv32emu syscall set is implementation-specific.** The allowlist is
  derived from rv32emu's actual behaviour, not from what the cart binary
  needs. A future rv32emu update that adds a new syscall will require
  re-running Stage 3. The allowlist header should include a comment noting
  the rv32emu commit hash it was derived from.

- **`riscv_flush_icache` (258/259).** Spike H identified that musl RV32
  startup calls this. It must remain in the ILP32 allowlist. Both 258
  and 259 should be allowed (Spike H found both were used).

- **QEMU vs hardware syscall sets.** QEMU may make different host syscalls
  than the real Milk-V Duo environment (QEMU may issue syscalls via its
  own internal paths, not the RV32 cart's). The strace output in Stage 3
  captures the host-process syscalls made by rv32emu on Fedora 42; this
  is the correct set for the Fedora 42 / QEMU deployment. The Milk-V Duo
  hardware may have a slightly different set; this is a hardware follow-up.

---

## Deliverables

- `spikes/spike-r/seccomp_raw_test.c` — Stage 1: minimal arch-dispatch
  filter proof of concept; multi-filter semantics verification.
- `spikes/spike-r/launcher_r.c` — Stage 2: fork/exec harness using raw
  BPF instead of libseccomp; replaces `launcher.c`'s seccomp section.
- `spikes/spike-r/seccomp_allowlist.h` — Stage 3: production
  `struct sock_filter[]` for the LP64/ILP32 arch-dispatch filter, with
  per-syscall comments. This is the primary deliverable.
- `spikes/spike-r/derive_allowlist.sh` — helper script that runs strace
  over each cart workload and produces the union syscall list.
- `spikes/spike-r/Makefile` — builds `seccomp_raw_test` and `launcher_r`
  for RV64; targets for Stage 1 validation, Stage 3 strace runs,
  Stage 4 adversary verification. Delegates to Spike H's Makefile for
  QEMU environment setup.
- `spikes/spike-r/TASKS.md` — per-step checklist, updated as work proceeds.
- `docs/design/spike-r-results.md` — write-up: per-stage pass/fail,
  multi-filter semantics finding, the committed allowlist, open items.

---

## Open items deferred

- **Hardware syscall set.** Run Stage 3 strace on a Milk-V Duo once
  available to confirm the allowlist is complete on real silicon.
- **`SECCOMP_FILTER_FLAG_TSYNC`.** If the cart runner uses multiple
  threads in future (currently single-threaded on the interpreter path),
  phase 2 will need `SECCOMP_FILTER_FLAG_TSYNC` to cover them all.
  Not needed for v1.
- **`execve` arg filtering.** Phase 1 allows `execve` for the LP64
  launcher. An argument filter on `execve` (restricting which paths can
  be exec'd) would reduce the phase 1 surface further. Deferred; the
  mount namespace isolation makes filesystem access moot in practice.
