# System Call Entry & Exit in Luit: Complete Concept Guide

**How a user program crosses into the kernel — and back.** This lecture assumes everything from the earlier *Virtualizing Memory* guide (Sv39 paging, `satp`, `PTE_U`, the trampoline). Where a concept was already covered there, this guide references it rather than re-deriving it, and focuses on what's genuinely new: **CSRs, the calling convention, the trap-entry/exit assembly, and Luit's specific "switch the stack, not the page table" design.**

---

## PART 1 — Framing: What a System Call Actually Requires

### 1.1 The core problem
At the instant a user program executes a system call, **every piece of CPU state belongs to the user**: all 32 general-purpose registers, the program counter, the user stack, the user page table, and user-mode privilege. To safely run *kernel* C code, the machine must, in strict order:

1. Switch to supervisor mode.
2. Save the user's 32 registers and PC somewhere the kernel can reach.
3. Get onto a **kernel** stack (running kernel C on the user's stack would be unsafe/impossible in general).
4. Make the kernel's own code and data addressable.
5. Jump into kernel C.

And critically, this must be **transparent**: when the kernel is done, the user process resumes as if nothing happened — a trap it never asked for must be invisible to it.

### 1.2 One door for everything
System calls, faults (e.g., page faults — see the earlier guide's Part 6.11), and device interrupts **all enter through the same mechanism**. This is good for two reasons: it means only *one* isolation boundary needs to be gotten right (not three separate ones), and it's good for performance (one well-optimized path instead of three).

### 1.3 The three forces shaping every design choice
- **Isolation** — user code must not be able to corrupt or observe the transition itself.
- **Minimal, fast, flexible hardware** — RISC-V deliberately does the bare minimum in hardware and leaves policy to software (you'll see this concretely in Part 4, with `ecall`).
- **Luit's own constraints** — Luit boots via a device tree on real hardware (recall the earlier guide's Part 6.3 — `kvminit` mapping MMIO aliases discovered from the device tree), so the trap path is built around *discovered* hardware, not hardcoded assumptions.

---

## PART 2 — Prerequisite Machinery: CSRs and the Calling Convention

### 2.1 Supervisor registers (CSRs) involved in a trap
These are **Control and Status Registers** — special registers only readable/writable in supervisor mode. A trap is nothing more than disciplined use of this short list:

| CSR | Holds |
|---|---|
| `satp` | Physical address of the page-table root (already familiar from the earlier guide's Part 5.5) |
| `stvec` | Where the CPU jumps **on any trap** — in Luit, this points at `uservec` |
| `sepc` | The user's PC, saved **automatically by hardware** the instant `ecall` executes |
| `sscratch` | A scratch CSR with no fixed hardware meaning — Luit uses it to park `&p->tf` (a pointer to the process's trapframe) |
| `scause` | Why the trap happened (already introduced in the earlier guide's Part 5.4/6.11 for page faults; here it also reports "this was a system call") |
| `sstatus` | Mode and interrupt-enable bits |

### 2.2 Privileged instructions
- `csrr` / `csrw` / `csrrw` — read / write / **atomically** read-and-write a CSR in one instruction. The atomic swap form (`csrrw`) matters a lot in Part 5 — it lets `uservec` grab the trapframe pointer *and* stash the user's register value in the same instruction, with no window where either value is lost.
- `sret` — return to user mode (the mirror image of the privilege escalation `ecall` performs).

### 2.3 The RISC-V calling convention (ABI)
This is standard RISC-V software convention (not trap-specific), but the syscall mechanism leans on it heavily:

| Register(s) | ABI name | Role | Saved by |
|---|---|---|---|
| x1 | ra | return address | caller |
| x2 | sp | stack pointer | callee |
| x8/x9 | s0/fp, s1 | saved registers | callee |
| x10–11 | a0–a1 | arguments / return value | caller |
| x12–17 | a2–a7 | further arguments | caller |
| x18–27 | s2–s11 | saved registers | callee |
| x5–7, x28–31 | t0–t6 | temporaries | caller |

**The one row that matters most for this whole lecture: `a7` carries the system-call number, `a0`–`a5` carry the arguments, and `a0` carries the result back.** This single convention is *why* the user-side stub (Part 3) can be so short — the arguments are already sitting in the correct registers before the trap even happens, purely because the calling code obeyed the ABI when it called the stub.

---

## PART 3 — The Key Design Decision: Luit vs. xv6, Restated Precisely

Your earlier guide's Part 6.1 already covered this at the page-table level. This lecture adds the *trap-path* consequences, which is really the sharper way to see why the decision matters:

| | **xv6** | **Luit** |
|---|---|---|
| Kernel mapped in user page table? | **No.** | **Yes** — mapped into every user page table, Linux-style, but with **no `PTE_U`**, so user code still faults if it tries to touch it (same mechanism as the earlier guide's Part 6.2 — presence ≠ access). |
| Consequence for `stvec` | Must point at the **trampoline** — a page mapped at the *same virtual address* in every page table, because the code doing the `satp` switch has to remain valid before and after that switch (see the earlier guide's dedicated trampoline explanation). | `uservec`'s real kernel address is *already* reachable from the user's own page table (since the kernel is mapped everywhere) — so `stvec` points **straight at `uservec`**, no relay page needed. |
| What changes on trap entry | Page table (`satp`) **and** stack. | **Only the stack.** `satp` is never touched on entry. |
| Cost / benefit | A page-table switch + `sfence.vma` (recall the earlier guide's Part 5.5 — this fence is *mandatory* whenever `satp` changes) sits on the fast path of **every single syscall**. | Simpler, faster syscalls — no switch, no fence, on the fast path. Trade-off: kernel mappings live in every address space, which is exactly the shape of thing that motivated **KPTI** (Kernel Page-Table Isolation) in real production kernels after the Meltdown speculative-execution attack (Part 8). |

The kernel source itself states the rationale directly (`kernel/vm.c`): *"...avoid switching page tables on every trap (which is what xv6 does, and why xv6 needs a trampoline page)."*

**The one-sentence version:** xv6 needs a trampoline because it switches address spaces on every trap; Luit needs no trampoline because it never switches address spaces on a trap at all — the kernel was already there the whole time, just locked behind `PTE_U`.

---

## PART 4 — The Four-Step Path, Overview

```
1. User stub      user/usys.S       →  load a7, ecall
2. Vector         kernel/uservec.S  →  save regs, switch stack
3. Trap handler   kernel/trap.c     →  usertrap(): why did we trap?
4. Dispatch       kernel/syscall.c  →  syscall(): call the handler
```
Concretely: `user write() → usys.S → ecall → uservec → usertrap() → syscall() → sys_write() → ...and all the way back.` Each step below is one section of that chain.

---

## PART 5 — Step 1: The User Stub (`usys.S`)

### 5.1 Where it comes from
Nobody hand-writes these stubs. There's a single source of truth, `kernel/syscall.tbl`:
```
# num  name    handler
13     sleep   sys_sleep
16     write   sys_write
```
A tool, `tools/gensyscalls.py`, reads this table and **generates two things from one source**: the user-side stubs (`user/usys.S`) *and* the kernel-side dispatch table (used in Part 7). This is a deliberate anti-duplication measure — the syscall number `16` for `write` only has to be written down once, and both sides of the boundary stay in sync automatically.

### 5.2 The generated stub itself
```asm
.macro SYSCALL name, num
  .globl \name
\name: li a7, \num
       ecall
       ret
.endm
SYSCALL write, 16      # → li a7,16 ; ecall ; ret
```
Three instructions, full stop:
1. `li a7, 16` — load the syscall number into `a7` (recall Part 2.3 — this is *the* register the ABI reserves for exactly this purpose).
2. `ecall` — trap into the kernel (Part 6).
3. `ret` — return to the caller, once the kernel has come back and placed the result in `a0`.

Notice what's conspicuously **absent**: no register-saving code, no stack manipulation. That's because the caller of `write()` already obeyed the ABI (arguments already sit in `a0`–`a5`), and the *callee*-saved registers are the kernel's problem to preserve, not this stub's.

### 5.3 A real engineering detail: versioned ABI
The table header states plainly: **renumbering an existing syscall entry is an ABI break** — you must bump a version number in `docs/ABI.md`. Adding a *new* number is fine within a version. This is the standard discipline any long-lived, externally-consumed interface needs (imagine a compiled program somewhere still has `16` hardcoded for `write` — if the kernel silently renumbered it, that program would silently call the wrong syscall). The lecture notes this is an obligation xv6's own generator (`usys.pl`) doesn't enforce — a genuine hardening Luit adds on top of the teaching-kernel baseline.

---

## PART 6 — Step 1 (continued): What `ecall` Actually Does

This is one of the most important minimalist-design points in the whole lecture.

### 6.1 The full list of what hardware does, and nothing more
`ecall` performs **exactly two actions**:
1. Change to supervisor mode.
2. Jump to whatever address `stvec` holds (in Luit, `stvec = uservec`'s real address — no trampoline, per Part 3).

It also **latches the user's PC into `sepc`** automatically (so the kernel later knows where to resume). **That is the entire list.** `ecall` does **not**:
- save any general-purpose registers,
- switch stacks,
- touch the page table.

### 6.2 Why leave so much undone — on purpose
This is RISC-V's philosophy made explicit: **minimal hardware gives the OS maximum room to optimize.** If `ecall` insisted on switching the page table itself (the way some architectures' trap mechanisms bundle more behavior in), Luit couldn't make its "keep the same page table" optimization from Part 3 — the hardware would have already forced the switch. Because `ecall` does nothing beyond mode change + jump + save-PC, **Luit gets to decide** that no `satp` switch is needed here at all, and exploits exactly that freedom.

The lecture's own framing: *"The simplicity of ecall is a feature: the hardware sets policy nowhere, so the software sets it everywhere."* This is the RISC-V analogue of a very general systems idea — push mechanism into hardware, push policy into software (the same spirit as `walk()`'s `alloc` parameter from the earlier guide, letting *callers* decide policy while the mechanism stays fixed).

---

## PART 7 — Step 2: `uservec` — Saving State and Switching the Stack

### 7.1 The setup: `sscratch` holds the trapframe pointer
Before any user code ever runs, the kernel has already arranged for `sscratch` to hold `&p->tf` — a pointer to this process's **trapframe** (a dedicated per-process page, described in Part 7.3). This is the one piece of "kernel state reachable without a page-table switch" that makes everything else possible.

### 7.2 The assembly, step by step
```asm
uservec:
  csrrw a0, sscratch, a0     # a0 = &tf ; sscratch = user's original a0  (ATOMIC SWAP)
  sd ra, 40(a0)              # save ra, sp, gp, tp, t0..t6, s0..s11, a1..a7
  ... (31 registers total) ...
  sd t6, 280(a0)
  csrr t0, sscratch          # recover the user's original a0 (parked there by the swap)
  sd   t0, 112(a0)           # store it into tf->a0
  csrr t1, sepc              # the user's PC at the moment of ecall
  sd   t1, 24(a0)            # store it into tf->epc
  ld tp, 32(a0)              # load our hartid (which CPU core we're on)
  ld sp, 8(a0)               # ← SWITCH TO THE KERNEL STACK (the pivotal line)
  ld t1, 16(a0)              # tf->kernel_trap = &usertrap
  jr t1                      # jump into kernel C — no satp, no trampoline
```

**Why the atomic `csrrw` matters:** the very first instruction needs to use register `a0` as a working pointer to the trapframe — but `a0` currently holds a live *user* value that must not be lost. `csrrw a0, sscratch, a0` reads `sscratch` into `a0` **and** writes the old `a0` into `sscratch`, as one indivisible instruction. There's no gap where the old `a0` value is gone but the new one isn't ready — exactly the kind of atomicity you'd want at a boundary this sensitive.

**The line to notice most:** `ld sp, 8(a0)` — this is the *entire* stack switch. Compare to the design-decision table in Part 3: xv6's equivalent code here would *also* contain a `csrw satp, ...` followed by `sfence.vma`. Luit's version simply doesn't have those two instructions. **That absence is the whole design** — the lecture says this outright.

By the final `jr t1`, we've jumped into `usertrap()` (Part 8) — already running on the kernel stack, with all 31 user registers safely saved, but still using the **same page table** the user process was using the whole time.

### 7.3 The trapframe: where all that saved state actually lives
```c
struct trapframe {
  /*   0 */ uint64 kernel_satp;    // kernel page table
  /*   8 */ uint64 kernel_sp;      // top of kernel stack     ← ld sp,8
  /*  16 */ uint64 kernel_trap;    // &usertrap()
  /*  24 */ uint64 epc;            // saved user PC
  /*  32 */ uint64 kernel_hartid;
  /*  40 */ uint64 ra, sp, gp, tp;
  /*  72 */ uint64 t0, t1, t2;     /* 96 */ s0, s1;
  /* 112 */ uint64 a0, a1, a2, a3, a4, a5, a6, a7;   // a7 sits at offset 168
  /* 176 */ ... s2..s11 ...        /* 256 */ t3, t4, t5, t6;
};
```
This is a **per-process page** the kernel can always reach — `sscratch` pointing at it is precisely what lets `uservec` find it without needing any lookup through the (unswapped) page table.

Two details worth internalizing:
1. **The byte offsets exactly match xv6's layout.** This isn't required for correctness — it's a deliberate compatibility choice so that tooling, debugging muscle memory, and mental models built on xv6 transfer directly to Luit.
2. **`kernel_satp` is still stored at offset 0 — but `uservec` never reads it.** This looks odd at first (why keep a field you never use on entry?), but it's not dead weight: `usertrapret()` (Part 9) *writes* into it on the way out, purely for bookkeeping/consistency with the xv6-derived structure layout — it is genuinely unused for the actual page-table decision on this kernel, precisely because Luit's whole point is *not* switching page tables on entry.

---

## PART 8 — Step 3: `usertrap()` Decides What Happened

```c
void usertrap(void) {
  if (r_sstatus() & SSTATUS_SPP) panic("came from S-mode");
  w_stvec((uint64)kernelvec);        // kernel traps (if any occur now) go here instead
  struct proc *p = myproc();
  p->tf->epc = r_sepc();
  uint64 scause = r_scause();
  if (scause == 8) {                 // ecall: a system call
      if (p->killed) exit(-1);
      p->tf->epc += 4;               // resume AFTER the ecall instruction
      intr_on(); syscall();
  } else if (scause==15 || scause==13) {   // page fault hook
      // later labs (COW, mmap) resolve faults HERE
  } else { devintr(); ... }
  usertrapret();
}
```

### 8.1 The isolation check, line one
```c
if (r_sstatus() & SSTATUS_SPP) panic("came from S-mode");
```
`SSTATUS_SPP` records **which privilege mode the trap came from**. Since `usertrap()` should only ever be reached via the *user*-trap path, seeing SPP set (meaning the previous mode was supervisor, not user) means something has gone very wrong — either a kernel bug or an attempted exploit. The lecture is direct about this: *"The SSTATUS_SPP guard is isolation in one line: a trap that did not come from user mode is a bug or an attack, and we refuse it."* This directly extends the "never trust unchecked input" theme from the earlier guide's `copyin`/`copyout` discussion (Part 6.10) — here it's not a *pointer* being validated, but the trap's own provenance.

### 8.2 Redirecting `stvec` for the duration
```c
w_stvec((uint64)kernelvec);
```
While *handling* this user trap, `usertrap()` points `stvec` at a **different** handler (`kernelvec`) — so that if a *second* trap occurs while the kernel itself is running (e.g., a device interrupt arriving mid-syscall), it's routed to kernel-trap-handling code, not back into `uservec` (which assumes it's coming from a user context, with a valid `sscratch`, etc.). This is swapped back to `uservec` later, in `usertrapret()` (Part 9), right before returning to the user.

### 8.3 Why `epc += 4`
```c
p->tf->epc += 4;
```
`sepc` was latched to the address of the `ecall` instruction itself (Part 6.1). If the kernel returned there unmodified, the user process would just execute `ecall` again — an infinite loop. RISC-V instructions here are 4 bytes wide, so adding 4 advances the saved PC to point at the instruction **right after** `ecall` — which, per the stub in Part 5.2, is `ret`. This is exactly what makes the return-trip synthesis in Part 10 correct: "the user resumes one instruction past its `ecall`."

### 8.4 `scause` dispatch, and the shared decoder
```
scause == 8              → this was a syscall → call syscall() (Part 9)
scause == 15 or 13        → a page fault (store / load) → hook for later labs (COW, mmap)
otherwise                  → devintr() → a device interrupt
```
This single `scause` check is the concrete mechanism behind Part 1.2's claim that syscalls, faults, and interrupts "all enter through the same door" — they arrive via the identical `uservec`/`usertrap()` path and are only *distinguished* here, by reading `scause`.

Luit ships a small human-readable decoder for this value (`kernel/trap.c:68`), used in panic/kill messages:
```c
static const char *cause_str(uint64 c) {
  switch (c) {
  case 2:  return "illegal instruction";
  case 8:  return "ecall from user";        // ← our system call
  case 12: return "instruction page fault";
  case 13: return "load page fault";
  case 15: return "store page fault";        // ← COW / mmap land here
  default: return "unknown";
  }
}
```
The lecture flags *why this is worth learning now*: the very same `scause` values decoded here for diagnostics are the ones you'll be handling programmatically in later labs — e.g., a **store page fault (13/15)** is "just a bug" today (Part 6.11 in the earlier guide: current Luit just kills the offending process), but becomes the **trigger for copy-on-write** in a later lab. Seeing the vocabulary named early pays off later.

---

## PART 9 — Step 4: `syscall()` Dispatches on `a7`

```c
void syscall(void) {
  struct proc *p = myproc();
  uint64 num = p->tf->a7;                     // the number the stub loaded (Part 5.2)

  if (num > 0 && num < NELEM(syscalls) && syscalls[num])
      p->tf->a0 = syscalls[num]();            // result → a0
  else
      p->tf->a0 = -1;
}
```

### 9.1 Bounds-check before indexing — treat user input as hostile
```c
if (num > 0 && num < NELEM(syscalls) && syscalls[num])
```
This is the same discipline as `copyin`/`copyout` validating pointers (earlier guide, Part 6.10) and the `SSTATUS_SPP` check (Part 8.1): **the number in `a7` came from user code, and user code is untrusted.** A bad number is "hostile input, not a bug" — the correct response is to return an error (`-1` in `a0`) and let the process continue running, not to crash the kernel by indexing an array out of bounds.

### 9.2 The dispatch table
`syscalls[]` is exactly the kernel-side table `gensyscalls.py` generated from `syscall.tbl` (Part 5.1) — an array of function pointers where, e.g., index 16 holds `sys_write()` (implemented in `kernel/sysfile.c`). Calling `syscalls[num]()` is the moment the actual requested work (reading a file, writing output, sleeping, etc.) finally happens — everything before this point has been pure plumbing to get from user code to this one call, safely.

### 9.3 The one seam every syscall crosses
The kernel source comment on this function marks it explicitly as *the* central hook point: *"Lab 2 (trace) hooks in HERE: this is the one point every call crosses."* Structurally, this is the payoff of the whole table-driven design — if you want to intercept, log, or trace *every* system call regardless of which one it is, this is the single function to instrument, rather than needing a hook inside each of dozens of individual `sys_*` functions.

---

## PART 10 — The Return Trip: `usertrapret()` → `userret`

```c
void usertrapret(void) {                 // trap.c
  intr_off();
  w_stvec((uint64)uservec);              // next trap goes to uservec again
  p->tf->kernel_satp  = r_satp();        // refresh bookkeeping field (Part 7.3)
  p->tf->kernel_trap  = (uint64)usertrap;
  x = r_sstatus();
  x &= ~SSTATUS_SPP;                     // SPP=0 → sret will drop to U-mode
  x |= SSTATUS_SPIE;                     // re-enable interrupts once back in user mode
  w_sstatus(x);
  w_sepc(p->tf->epc);                    // the epc+4 value computed in usertrap() (Part 8.3)
  userret(p->tf);
}

userret:                                              // uservec.S
  ld ra,40(a0) ... ld t6,280(a0)                       // restore all 31 registers
  ld a0, 112(a0)                                       // finally, a0 = the return value
  sret                                                 // back to user, at epc
```

### 10.1 What `usertrapret()` sets up
- **Re-points `stvec` back at `uservec`** (undoing Part 8.2's temporary redirect to `kernelvec`) — the next trap this process causes should again go through the ordinary user-trap path.
- **Clears `SSTATUS_SPP`** — this is the bit `sret` reads to decide which privilege level to drop into. Clearing it means "go to user mode," mirroring Part 8.1's check of the *same* bit on entry (there, it was used to verify the trap *came from* user mode; here, it's used to *command* a return *to* user mode).
- **Sets `SSTATUS_SPIE`** — re-enables interrupts once execution resumes in user mode.
- **Writes `sepc`** with the already-adjusted return address from Part 8.3.

### 10.2 `userret`: the mirror image of `uservec`
This restores the exact 31 registers `uservec` saved, in the same order, from the same trapframe — and, tellingly, restores `a0` **last**, specifically so that the final value loaded there is the syscall's actual **return value** (which `syscall()` in Part 9 wrote into `tf->a0`) rather than whatever the general register-restore loop would otherwise put there.

### 10.3 `sret`: the actual privilege drop
`sret` reads the `SPP` bit (now 0, per Part 10.1) to determine it should switch to user mode, and jumps to whatever `sepc` holds (the `ecall+4` address). This is the precise mirror of `ecall`'s two actions from Part 6.1 — one instruction raised privilege and jumped via `stvec`; this one instruction lowers privilege and jumps via `sepc`.

**The user process resumes one instruction past its `ecall`, with the syscall's result already sitting in `a0` — exactly where the calling convention (Part 2.3) says a return value belongs.** From the user program's point of view, `write()` (Part 5.2's `ret` instruction now executes) simply... returned a value. Nothing about the trip through the kernel is visible — exactly the transparency requirement from Part 1.1.

---

## PART 11 — Synthesis: The Whole Path in One Trace

```
ENTRY:
  user write()  →  usys.S: li a7,16; ecall  →  uservec: save regs, sp←kstack
    →  usertrap(): scause==8  →  a7 →  syscall()  →  syscalls[16] → sys_write()

RETURN:
  sys_write() returns result in a0  →  usertrapret(): clear SPP, set sepc
    →  userret: restore regs, sret  →  user resumes at ecall+4, result in a0
```

**The two invariants that hold the entire mechanism together:**
1. **`a7` selects, `a0` returns** — the ABI (Part 2.3) never changes across the user/kernel boundary; it's the one shared contract both sides agree on without any negotiation at trap time.
2. **The page table is the same the entire time** — only the stack and privilege level change. This is *the* Luit simplification (Part 3), stated as plainly as the lecture can state it.

---

## PART 12 — Design Reflection: Can an Evil Program Abuse This Door?

This section is where the isolation goal from Part 1.3 gets tested against a deliberately adversarial mindset.

### 12.1 What the user genuinely controls, and how each is defended
| User controls | Kernel's defense |
|---|---|
| `a7` (syscall number) | `syscall()` bounds-checks it before indexing (Part 9.1) — an out-of-range number gets `-1`, not a crash. |
| Pointer arguments (`a0`–`a5`) | Every pointer argument must cross `copyin`/`copyout` (earlier guide, Part 6.10), which refuses anything **unmapped**, **kernel-owned**, or **non-`PTE_U`**. |
| The `ecall` instruction itself | Cannot be used to "forge" an arbitrary entry point — it *always* raises privilege via the fixed, kernel-controlled `stvec`, never anywhere else. |
| Trap provenance | The `SSTATUS_SPP` guard (Part 8.1) rejects any trap into `usertrap()` that didn't originate from user mode. |

### 12.2 The honest trade-off, named explicitly
Mapping the kernel into every address space without `PTE_U` (Part 3) is **safe against direct access** — the permission-bit check (earlier guide, Part 5.6) stops a normal load/store/fetch cold. But it is **exactly the memory shape** that real speculative-execution side-channel attacks like **Meltdown** exploited in production CPUs — where a CPU could, transiently and out-of-order, read data it wasn't architecturally permitted to, before the permission check "caught up." This class of vulnerability is precisely why production kernels (Linux, Windows) added **KPTI (Kernel Page-Table Isolation)** — deliberately going back to something closer to xv6's separate-page-tables-plus-trampoline design, at a real performance cost, specifically to remove the kernel from being mapped (and thus speculatively readable) in user-mode address spaces at all.

**Why Luit still makes the simpler choice anyway:** for a *teaching* kernel running on QEMU, the lecture argues this is the right call — it lets you see the entry/exit mechanism clearly, without the trampoline's extra machinery obscuring it, and it turns the trade-off itself into something you can name and discuss, rather than a subtlety buried a few abstraction layers down.

---

## Quick-Reference: New Vocabulary from This Lecture
| Term | One-line meaning |
|---|---|
| **CSR** | Control and Status Register — supervisor-only registers like `satp`, `stvec`, `sepc`, `sscratch`, `scause`, `sstatus` |
| **`ecall`** | The one instruction that raises privilege and jumps via `stvec`; does nothing else |
| **`sret`** | The mirror instruction: drops privilege (per `SPP`) and jumps via `sepc` |
| **`uservec`** | The assembly routine `stvec` points at; saves 31 registers, switches to the kernel stack, jumps into `usertrap()` |
| **Trapframe** | A per-process page holding saved user register state, reachable via `sscratch` without needing a page-table switch |
| **`usertrap()`** | The C function that inspects `scause` and decides: syscall? page fault? device interrupt? |
| **`syscall()`** | Bounds-checks `a7`, then dispatches through the generated function-pointer table |
| **`usertrapret()` / `userret`** | The return path: restore state, flip `SPP`, restore registers, `sret` |
| **KPTI** | Kernel Page-Table Isolation — the real-world defense against Meltdown-style attacks, at odds with Luit's simplification |

## How This Lecture Extends the Earlier VM Guide
- **`satp`, `sfence.vma`, `PTE_U`** (earlier guide, Part 5.5, 5.6, 6.2) are exactly what make Luit's "switch the stack, not the page table" trick both *possible* (kernel already mapped) and *safe* (still locked by `PTE_U`).
- **The trampoline** (earlier guide's dedicated explanation) is now motivated from the opposite direction: this lecture shows you the actual xv6-style entry code Luit is choosing *not* to write (`csrw satp` + `sfence.vma`), making concrete exactly which two instructions the trampoline exists to survive.
- **`copyin`/`copyout`** (earlier guide, Part 6.10) reappears here as the concrete mechanism protecting Part 12's "user controls pointer arguments" threat.
- **`scause`/`stval`-driven page faults** (earlier guide, Part 6.11) are now shown as one branch (`scause==13/15`) inside the *same* `usertrap()` dispatch that handles ordinary system calls — faults and syscalls are siblings in one mechanism, not separate subsystems.
