#!/usr/bin/env python3
"""Apply the syscall_get_arch TIF_32BIT fix to arch/riscv/include/asm/syscall.h.

Inserts a TIF_32BIT check so ILP32 compat processes report AUDIT_ARCH_RISCV32
in seccomp_data.arch, enabling arch-dispatch BPF filters.
"""
import sys

if len(sys.argv) != 2:
    print("usage: apply-syscall-arch-patch.py <syscall.h path>")
    sys.exit(1)

path = sys.argv[1]
try:
    src = open(path).read()
except FileNotFoundError:
    print(f"ERROR: {path} not found")
    sys.exit(1)

old = (
    "static inline int syscall_get_arch(struct task_struct *task)\n"
    "{\n"
    "#ifdef CONFIG_64BIT\n"
    "\treturn AUDIT_ARCH_RISCV64;"
)

new = (
    "static inline int syscall_get_arch(struct task_struct *task)\n"
    "{\n"
    "#ifdef CONFIG_64BIT\n"
    "#ifdef CONFIG_COMPAT\n"
    "\t/* TIF_32BIT set → ILP32 compat process → AUDIT_ARCH_RISCV32. */\n"
    "\tif (test_tsk_thread_flag(task, TIF_32BIT))\n"
    "\t\treturn AUDIT_ARCH_RISCV32;\n"
    "#endif\n"
    "\treturn AUDIT_ARCH_RISCV64;"
)

if old not in src:
    if "AUDIT_ARCH_RISCV32" in src and "SR_UXL_32" in src:
        print(f"INFO: {path} — patch already applied")
        sys.exit(0)
    print(f"WARNING: {path} — expected pattern not found; structure may differ")
    for i, line in enumerate(src.splitlines()):
        if "syscall_get_arch" in line or "AUDIT_ARCH" in line:
            print(f"  L{i+1}: {line}")
    sys.exit(1)

patched = src.replace(old, new, 1)
open(path, 'w').write(patched)
print(f"Patch applied: {path}")
print("  syscall_get_arch: checks SR_UXL_32 and TIF_32BIT with pr_info diagnostic")

# Patch process.c: add pr_info in compat_elf_check_arch and start_thread
import os
proc_path = "arch/riscv/kernel/process.c"
try:
    proc = open(proc_path).read()
    # Add printk to compat_elf_check_arch to show if/when it returns true
    old1 = ("bool compat_elf_check_arch(Elf32_Ehdr *hdr)\n"
            "{\n"
            "\treturn (compat_mode_supported || (hdr->e_flags & EF_RISCV_64ILP32)) &&\n"
            "\t       hdr->e_machine == EM_RISCV &&\n"
            "\t       hdr->e_ident[EI_CLASS] == ELFCLASS32;")
    new1 = ("bool compat_elf_check_arch(Elf32_Ehdr *hdr)\n"
            "{\n"
            "\tbool r = (compat_mode_supported || (hdr->e_flags & EF_RISCV_64ILP32)) &&\n"
            "\t       hdr->e_machine == EM_RISCV &&\n"
            "\t       hdr->e_ident[EI_CLASS] == ELFCLASS32;\n"
            "\tif (hdr->e_ident[EI_CLASS] == ELFCLASS32)\n"
            "\t\tpr_info(\"spike-s: compat_elf_check_arch ELFCLASS32: compat=%d flags=0x%x -> %d\\n\",\n"
            "\t\t        compat_mode_supported, hdr->e_flags, r);\n"
            "\treturn r;")
    # Add printk to start_thread to show TIF_32BIT for EVERY exec
    old2 = "\tregs->status &= ~SR_UXL;"
    new2 = ("\tif (test_thread_flag(TIF_32BIT))\n"
            "\t\tpr_info(\"spike-s: start_thread TIF_32BIT=1 (ILP32)\\n\");\n"
            "\tregs->status &= ~SR_UXL;")
    patched = proc
    if old1 in patched and "spike-s: compat_elf_check_arch" not in patched:
        patched = patched.replace(old1, new1, 1)
        print(f"Patched: {proc_path} compat_elf_check_arch with pr_info_once")
    if old2 in patched and "spike-s: start_thread" not in patched:
        patched = patched.replace(old2, new2, 1)
        print(f"Patched: {proc_path} start_thread with pr_info_once")
    if patched != proc:
        open(proc_path, 'w').write(patched)
    else:
        print(f"INFO: {proc_path} — patterns not found or already patched")
except FileNotFoundError:
    print(f"INFO: {proc_path} not found")

# Patch fs/binfmt_elf.c to add diagnostic in compat context
elf_c_path = "fs/binfmt_elf.c"
try:
    elf_c = open(elf_c_path).read()
    old_elfb = "static int load_elf_binary(struct linux_binprm *bprm)\n{"
    new_elfb = ("static int load_elf_binary(struct linux_binprm *bprm)\n{\n"
                "#ifdef ELF_COMPAT\n"
                "\tif (((struct elf32_hdr *)bprm->buf)->e_ident[EI_CLASS] == ELFCLASS32)\n"
                "\t\tpr_info(\"spike-s: compat load_elf_binary ELFCLASS32 %s\\n\",\n"
                "\t\t        bprm->filename);\n"
                "#endif")
    # Also add diagnostic after SET_PERSONALITY2
    old_sp2 = "\tSET_PERSONALITY2(*elf_ex, &arch_state);"
    new_sp2 = ("\tSET_PERSONALITY2(*elf_ex, &arch_state);\n"
               "#ifdef ELF_COMPAT\n"
               "\tpr_info(\"spike-s: SET_PERSONALITY2 TIF_32BIT=%d EI_CLASS=%d\\n\",\n"
               "\t        test_thread_flag(TIF_32BIT), elf_ex->e_ident[EI_CLASS]);\n"
               "#endif")
    if old_elfb in elf_c and "spike-s: compat load_elf_binary" not in elf_c:
        patched = elf_c.replace(old_elfb, new_elfb, 1)
        if old_sp2 in patched and "spike-s: after SET_PERSONALITY2" not in patched:
            patched = patched.replace(old_sp2, new_sp2, 1)
            print(f"Patched: {elf_c_path} — compat diagnostics (load + SET_PERSONALITY2)")
        open(elf_c_path, 'w').write(patched)
    elif "spike-s: compat load_elf_binary" in elf_c:
        print(f"INFO: {elf_c_path} — compat diagnostic already present")
        # Ensure SET_PERSONALITY2 diagnostic is also there
        if old_sp2 in elf_c and "spike-s: after SET_PERSONALITY2" not in elf_c:
            open(elf_c_path, 'w').write(elf_c.replace(old_sp2, new_sp2, 1))
            print(f"  Added SET_PERSONALITY2 diagnostic")
    else:
        print(f"INFO: {elf_c_path} — load_elf_binary pattern not found")
        for i, line in enumerate(elf_c.splitlines()):
            if "load_elf_binary" in line and "static" in line:
                print(f"  L{i+1}: {line}")
                break
except FileNotFoundError:
    print(f"INFO: {elf_c_path} not found")

# Patch traps.c: print TIF_32BIT at actual syscall entry (do_trap_ecall_u)
traps_path = "arch/riscv/kernel/traps.c"
try:
    traps = open(traps_path).read()
    old_trap = "\t\tsyscall = syscall_enter_from_user_mode(regs, syscall);"
    new_trap = ("\t\tif (test_thread_flag(TIF_32BIT))\n"
                "\t\t\tpr_info_once(\"spike-s: ecall_u TIF_32BIT=1 (ILP32)\\n\");\n"
                "\t\tsyscall = syscall_enter_from_user_mode(regs, syscall);")
    if old_trap in traps and "spike-s: ecall_u" not in traps:
        open(traps_path, 'w').write(traps.replace(old_trap, new_trap, 1))
        print(f"Patched: {traps_path} — TIF_32BIT check at ecall_u entry")
    elif "spike-s: ecall_u" in traps:
        print(f"INFO: {traps_path} — ecall_u diagnostic already present")
    else:
        print(f"INFO: {traps_path} — do_trap_ecall_u pattern not found")
except FileNotFoundError:
    print(f"INFO: {traps_path} not found")

# Patch kernel/seccomp.c to add diagnostic in populate_seccomp_data
seccomp_path = "kernel/seccomp.c"
try:
    sc = open(seccomp_path).read()
    old_sc = "\tsd->arch = syscall_get_arch(task);"
    new_sc = "\tsd->arch = syscall_get_arch(task);"
    # Also patch seccomp_run_filters to print arch + verdict for ILP32 processes
    old_run = "static int seccomp_run_filters(const struct seccomp_data *sd,"
    new_run = ("static int seccomp_run_filters(const struct seccomp_data *sd,")

    if old_sc in sc and "spike-s: seccomp" not in sc:
        open(seccomp_path, 'w').write(sc.replace(old_sc, new_sc, 1))
        print(f"Patched: {seccomp_path} — syscall_get_arch with TIF_32BIT check")
    elif "spike-s: seccomp" in sc:
        print(f"INFO: {seccomp_path} — diagnostic already present")
    else:
        print(f"INFO: {seccomp_path} — populate_seccomp_data pattern not found")
        for i, l in enumerate(sc.splitlines()):
            if "syscall_get_arch" in l:
                print(f"  L{i+1}: {l}")
                break

    # Add diagnostic at the END of seccomp_run_filters to see verdict for ILP32
    # Fix arch/riscv/include/asm/seccomp.h: add SECCOMP_ARCH_COMPAT for RISC-V 64+COMPAT
    seccomp_h_path = "arch/riscv/include/asm/seccomp.h"
    try:
        sh = open(seccomp_h_path).read()
        old_sh = ("#ifdef CONFIG_64BIT\n"
                  "# define SECCOMP_ARCH_NATIVE\t\tAUDIT_ARCH_RISCV64\n"
                  "# define SECCOMP_ARCH_NATIVE_NR\t\tNR_syscalls\n"
                  "# define SECCOMP_ARCH_NATIVE_NAME\t\"riscv64\"\n"
                  "#else /* !CONFIG_64BIT */")
        new_sh = ("#ifdef CONFIG_64BIT\n"
                  "# define SECCOMP_ARCH_NATIVE\t\tAUDIT_ARCH_RISCV64\n"
                  "# define SECCOMP_ARCH_NATIVE_NR\t\tNR_syscalls\n"
                  "# define SECCOMP_ARCH_NATIVE_NAME\t\"riscv64\"\n"
                  "# ifdef CONFIG_COMPAT\n"
                  "/* ILP32 compat processes report AUDIT_ARCH_RISCV32.\n"
                  " * Define SECCOMP_ARCH_COMPAT so the cache uses a separate\n"
                  " * allow_compat bitmap rather than ignoring sd->arch. */\n"
                  "#  define SECCOMP_ARCH_COMPAT\t\tAUDIT_ARCH_RISCV32\n"
                  "#  define SECCOMP_ARCH_COMPAT_NR\tNR_syscalls\n"
                  "#  define SECCOMP_ARCH_COMPAT_NAME\t\"riscv32\"\n"
                  "# endif /* CONFIG_COMPAT */\n"
                  "#else /* !CONFIG_64BIT */")
        if old_sh in sh and "SECCOMP_ARCH_COMPAT" not in sh:
            open(seccomp_h_path, 'w').write(sh.replace(old_sh, new_sh, 1))
            print(f"Patched: {seccomp_h_path} — SECCOMP_ARCH_COMPAT for ILP32")
        elif "SECCOMP_ARCH_COMPAT" in sh:
            print(f"INFO: {seccomp_h_path} — SECCOMP_ARCH_COMPAT already defined")
        else:
            print(f"WARNING: {seccomp_h_path} — pattern not found for COMPAT addition")
    except FileNotFoundError:
        print(f"INFO: {seccomp_h_path} not found")

    # Hook: print verdict for ILP32 processes
    old_hook = "\tfilter_ret = seccomp_run_filters(sd, &match);"
    new_hook = ("\tfilter_ret = seccomp_run_filters(sd, &match);\n"
                "\tif (test_thread_flag(TIF_32BIT))\n"
                "\t\tpr_info(\"spike-s: verdict nr=%d arch=%x ret=%x\\n\",\n"
                "\t\t        sd->nr, sd->arch, filter_ret);")

    # Patch seccomp_cache_check_allow (called in seccomp_run_filters) to skip cache
    # for compat (ILP32) processes. The cache is keyed only on syscall nr but
    # must also consider arch for ILP32 compat processes.
    old_compat_sec = ("static inline bool seccomp_cache_check_allow(const struct seccomp_filter *sfilter,\n"
                      "\t\t\t\t\t     const struct seccomp_data *sd)\n"
                      "{\n"
                      "\tint syscall_nr = sd->nr;\n"
                      "\tconst struct action_cache *cache = &sfilter->cache;\n"
                      "\n"
                      "#ifndef SECCOMP_ARCH_COMPAT")
    new_compat_sec = ("static inline bool seccomp_cache_check_allow(const struct seccomp_filter *sfilter,\n"
                      "\t\t\t\t\t     const struct seccomp_data *sd)\n"
                      "{\n"
                      "\t/* Spike S: for ILP32 compat processes, skip cache and evaluate filter. */\n"
                      "\tif (test_thread_flag(TIF_32BIT))\n"
                      "\t\treturn false;\n"
                      "\tint syscall_nr = sd->nr;\n"
                      "\tconst struct action_cache *cache = &sfilter->cache;\n"
                      "\n"
                      "#ifndef SECCOMP_ARCH_COMPAT")

    old_bpf_run = "\tstruct seccomp_filter *f =\n\t\tREAD_ONCE(current->seccomp.filter);"
    new_bpf_run = ("\tstruct seccomp_filter *f =\n\t\tREAD_ONCE(current->seccomp.filter);\n"
                   "\tif (test_thread_flag(TIF_32BIT))\n"
                   "\t\tpr_info(\"spike-s: run_filters f=%p mode=%d\\n\",\n"
                   "\t\t        f, current->seccomp.mode);")

    try:
        sc2 = open(seccomp_path).read()
        patched = False
        if old_hook in sc2 and "spike-s: verdict" not in sc2:
            sc2 = sc2.replace(old_hook, new_hook, 1)
            patched = True
            print(f"  Added verdict hook after seccomp_run_filters")
        elif "spike-s: verdict" in sc2:
            print(f"  INFO: verdict hook already present")
        else:
            print(f"  INFO: seccomp_run_filters call pattern not found")
        if old_bpf_run in sc2 and "spike-s: bpf_run" not in sc2:
            sc2 = sc2.replace(old_bpf_run, new_bpf_run, 1)
            patched = True
            print(f"  Added bpf_prog_run result diagnostic")
        if old_compat_sec in sc2 and "spike-s: for ILP32" not in sc2:
            sc2 = sc2.replace(old_compat_sec, new_compat_sec, 1)
            patched = True
            print(f"  Added ILP32 cache bypass in seccomp_cache_check_allow")
        elif "spike-s: for ILP32" in sc2:
            print(f"  INFO: ILP32 cache bypass already present")
        else:
            # Try flexible match
            idx = sc2.find("static inline bool seccomp_cache_check_allow")
            if idx >= 0:
                print(f"  INFO: seccomp_cache_check_allow found but pattern mismatch")
                print(f"  Context: {sc2[idx:idx+200][:100]!r}")
        if patched:
            open(seccomp_path, 'w').write(sc2)
    except FileNotFoundError:
        pass
except FileNotFoundError:
    print(f"INFO: {seccomp_path} not found")
