# Page Faults and Lazy Virtual Memory: Complete Concept Guide

**One mechanism — the page fault — underlies lazy allocation, zero pages, guard pages, copy-on-write, and memory-mapped files.** This guide merges three related sources: the BrahmaputraOS textbook chapter (deep, OSTEP-linked), the CS3106L lab lecture slides (same material, code-first), and the course roadmap document (shows where this fits in the OSTEP syllabus). It assumes everything from the earlier **VM guide** (Sv39, `PTE_U`, `satp`, `sfence.vma`, `walk`/`mappages`) and the **Syscall Entry & Exit guide** (`scause`, `stval`, `sepc`, the trapframe, `usertrap()`'s dispatch) — this lecture is literally what happens inside the `scause == 13 || scause == 15` branch you already saw stubbed out there.

---

## PART 0 — Where This Sits in the Course (from the OSTEP/RISC-V/Luit map)

The course teaches every topic at **three levels**, always in this order:
```
OSTEP    → OS concepts: what problem is being solved, and why it matters
RISC-V   → Hardware mechanisms: privilege, traps, CSRs, Sv39, TLB
Luit     → Kernel implementation: where the OS uses those mechanisms
Lab      → Practice: modify, observe, debug, explain
```
For **page faults** specifically, that mapping is:

| OSTEP mechanism | Luit/RISC-V detail |
|---|---|
| A memory reference cannot be translated or permitted | `scause` — why the trap happened |
| Hardware traps into the kernel | `stval` — the faulting virtual address |
| Kernel page-fault handler chooses a policy | `sepc` (reached in Luit as `p->tf->epc`) — the faulting instruction |
| Repair mapping; retry the same instruction | `usertrap()`'s branch: lazy allocation, COW, mmap, or kill |

The source chapters this material draws from: **OSTEP Ch. 21** ("Swapping: Mechanisms" — general page-fault/demand-paging mechanism), **OSTEP Ch. 23** ("Complete Virtual Memory Systems" — demand zeroing and COW), and the **files-and-directories chapter's `mmap()` discussion**. The key reading rule stated in the roadmap: *learn the OS concept first, then compare implementations* — xv6 and Luit are both "correct implementations" of the same underlying theory, not competing truths.

---

## PART 1 — A Fault Is an Event Before It Is an Error

### 1.1 The mechanism, restated precisely
A memory reference passes through the MMU before reaching physical memory. The MMU must find **both** a translation and permission to do the requested operation — a load needs a readable mapping, a store needs a writable mapping, a fetch needs an executable mapping (all straight out of the earlier VM guide's Part 5.6 permission table). **If either check fails, the processor raises an exception and transfers control to the kernel.**

### 1.2 The critical distinction: *how* vs *why*
> **A page fault describes how control reached the kernel; it does not, by itself, tell the kernel why the access failed.**

The same hardware event — "store page fault" — can mean wildly different things:
- **Fatal**: the process dereferenced a wild pointer, or tried to write into program text.
- **Expected (lazy allocation)**: the process touched a heap page that `sbrk()` promised but the kernel hasn't physically allocated yet.
- **Expected (COW)**: the page *is* present and mapped, but the kernel deliberately cleared its write bit so the first writer traps.

The hardware genuinely cannot tell these apart — it has no concept of "COW" or "lazy heap." **It just reports: this kind of access, at this address, failed.** Distinguishing the cases is entirely the kernel's job, using its own metadata (address-space layout, PTE bits, VMA records). This is the same **mechanism vs. policy** separation the earlier VM guide's Part 2 and 4 already established for translation itself — here it's applied one level up, to *what a failed translation means*.

### 1.3 The full decision flow

```
user memory access
       │
       ▼
MMU translation + permission check
   ├── success → physical memory access → same instruction is tried again (trivial case)
   └── failure → page-fault exception
                     │
                     ▼
          kernel classifies fault using address-space state
              ├── recoverable → repair mapping and return
              └── invalid     → reject access and kill process
```
This is the skeleton every remaining section of this guide fills in — different ways of being "recoverable."

---

## PART 2 — The RISC-V Page-Fault Machinery

On any fault, Luit needs exactly **three facts**: what sort of access failed, which virtual address was involved, and where the process was executing. Three CSRs (already introduced structurally in the Syscall guide's Part 2.1) answer these:

| CSR | Answers |
|---|---|
| `scause` | What kind of fault? |
| `stval` | Which virtual address faulted? |
| `sepc` (reached via `p->tf->epc` per the trapframe from the Syscall guide's Part 7.3) | Where was the program executing? |
| `sstatus` | Which privilege mode did the trap come from? (For all cases here: user mode.) |

### 2.1 The three fault-causing `scause` values
| `scause` | Exception | Typical failed operation |
|---|---|---|
| 12 | Instruction page fault | instruction fetch |
| 13 | Load page fault | read/load |
| 15 | Store/AMO page fault | write/store or atomic memory operation |

(Recall from the Syscall guide's Part 8.4 that `scause == 8` is `ecall` — a *system call*, not a fault at all. These four values together are exactly the branches `usertrap()` dispatches on.)

A fault can also arise even when the **translation succeeds** but a **permission bit** rejects the access — e.g., a user write to a page with `W=0`. The `scause` value doesn't distinguish "no translation" from "translation exists but permission denied" — both surface identically as, say, a store page fault. The kernel must inspect the actual PTE to tell them apart (Part 2.2).

### 2.2 The Sv39 leaf PTE, revisited for fault purposes
```
 PPN(10..53) | RSW(8-9) | D(7) | A(6) | G(5) | U(4) | X(3) | W(2) | R(1) | V(0)
```
This is the same PTE layout from the earlier VM guide's Part 5.3 — but this lecture calls out one field that mattered less before: **RSW (bits 8–9), "reserved for software."** The MMU hardware **completely ignores** these bits — they exist purely for kernel bookkeeping. **Luit's COW lab uses one of these bits as `PTE_COW`.** This is the mechanism by which the kernel can tag "this read-only page is read-only *on purpose, for COW reasons*" versus "this is an ordinary read-only page" — a distinction the hardware has no way to express on its own.

**Two different "recoverable" PTE states, worth keeping visually distinct:**
- **Invalid leaf mapping** (`V=0`, or no leaf PTE exists at all) → used by **lazy allocation** (Part 4): no usable translation exists yet.
- **Valid, read-only mapping** (`V=1, R=1, W=0`, plus `PTE_COW` set) → used by **COW** (Part 7): the translation exists and reads are fine, but writes are intentionally blocked.

---

## PART 3 — Luit's Address Space, and Why the Fault Handler Needs to Know It

### 3.1 The concrete layout
```
USER_TOP = 0x80000000  (high addresses)
┌────────────────────────────────┐
│ read-only info page (~0x7ffff000) │  Lab 3 material
├────────────────────────────────┤
│ heap — grows UPWARD ↑            │  sbrk() raises p->sz
├────────────────────────────────┤
│ initial user stack (one page)     │
├────────────────────────────────┤
│ guard page — UNMAPPED             │  no valid PTE, by design
├────────────────────────────────┤
│ program text / data               │  from the executable
└────────────────────────────────┘  (low addresses)
```
Built during `exec()`: `guard = PGROUNDUP(sz)`; the one-page stack sits immediately above the guard; `p->sz` is initially set to the top of that stack, and every `sbrk()` call raises `p->sz` further.

**This is a genuinely different layout from xv6** (worth flagging explicitly, since students may carry over an xv6 mental model): xv6's heap/stack arrangement, direction, and position all differ. Always reason from *this* diagram for Luit questions, not from memory of a different course's kernel.

### 3.2 The rule this layout forces: PTE state alone is not enough
> "It is not correct to say **'the PTE is absent, therefore allocate a page.'**"

Three different kinds of addresses can all show up as "no valid mapping":
- The **guard page** — absent *by design*, and must **remain** absent forever (Part 3.4 explains why).
- A **random wild address** — absent because it's simply invalid, and must remain invalid.
- An **mmap page** that hasn't been faulted in yet — absent, but *should* eventually become valid, once accessed.

**The kernel must classify a fault using both the page-table state AND which region the address belongs to.** The rule is therefore not "if PTE_V is zero, allocate" but:

> "If the faulting address belongs to a region the process is entitled to use lazily, satisfy the fault; otherwise reject it."

This single sentence is the organizing principle for essentially the entire rest of this lecture.

### 3.3 Luit's kernel-mapping design, restated in fault-handling terms
Recall the design decision from the Syscall guide's Part 3: the whole kernel is mapped into **every** process's page table, without `PTE_U`. The lecture reconnects this to page faults specifically: **it does not change the meaning of a page fault**, but it does change the concrete path to and from the handler — no trampoline/trapframe-at-the-top-of-every-table needed (xv6-style), no `satp` switch on trap entry. The user context is saved and execution moves to the process's kernel stack, while the **same page-table root stays active the entire time** — exactly the invariant from the Syscall guide's Part 11 restated here in the page-fault context specifically.

### 3.4 Stack guard pages: a standalone protective use of "leave it unmapped"
This deserves its own note since it's the simplest possible instance of "absence is a policy, not an accident": the user stack has finite size, and overflowing it would otherwise silently corrupt whatever memory sits below (here, the heap or text/data, depending on layout). By leaving **one page immediately below the stack unmapped** (no valid PTE at all — not even an invalid-but-reserved one), a stack overflow **faults immediately** at the guard, producing a clean, diagnosable crash instead of silent memory corruption. Luit's `exec()` installs this guard page as a matter of course, before any lazy-allocation or COW machinery is even involved.

---

## PART 4 — Lazy Allocation: Promising Memory Before Allocating It

### 4.1 The problem with eager `sbrk()`
An eager `sbrk(n)` immediately allocates `n` bytes' worth of physical pages, zeroes them, and installs writable PTEs — every promised byte is ready before the syscall even returns. Simple, but wasteful: **a process can grow its heap substantially and touch only a fraction of the new range**, and all that eager work (allocation + zeroing) was for nothing.

### 4.2 The lazy alternative
```
after a lazy sbrk(2*PGSIZE):

  old heap    →  (already backed)
  new page A  →  physical page: none yet   ┐  p->sz includes A and B,
  new page B  →  physical page: none yet   ┘  but no valid leaf PTE exists for either
```
`sbrk()` only advances the process's **logical** end (`p->sz`) — it does **not** immediately allocate the corresponding physical pages. The first time the process actually touches one of those pages, the missing mapping triggers a load/store page fault. The handler checks whether the address lies inside memory `sbrk()` genuinely promised, then **allocates, zeroes, and maps** a page, and returns. **If the process never touches that page, the allocation never happens at all.**

**Key clarifying point:** increasing `p->sz` does not magically create PTEs. `p->sz` records the process's *logical entitlement*; the page table separately records which parts of that entitlement currently have a physical backing.

### 4.3 Schematic resolver code
```c
int
lazy_fault(struct proc *p, uint64 faultva)
{
    uint64 va = PGROUNDDOWN(faultva);

    if (va >= USER_TOP)
        return -1;
    if (!inside_promised_heap(p, va))
        return -1;

    char *mem = palloc();
    if (mem == 0)
        return -1;
    memset(mem, 0, PGSIZE);

    if (mappages(p->pagetable, va, PGSIZE,
                 (uint64)mem,
                 PTE_U | PTE_R | PTE_W) != 0) {
        pfree(mem);
        return -1;
    }

    sfence_vma();
    return 0;
}
```
Walking through it against Part 3.2's rule:
- `va >= USER_TOP` → reject immediately: never satisfy a fault in kernel territory.
- `!inside_promised_heap(p, va)` → reject unless this address is genuinely part of what `sbrk()` promised (this is the region-metadata check the whole lecture insists is mandatory, not optional).
- `palloc()` + `memset(..., 0, ...)` → get a fresh physical page and **zero it before exposing it to the process** — this is not optional politeness: without zeroing, a process could read **stale data left behind by a previous process** that used to own that physical frame — a real information-leak vulnerability class, not just a cosmetic nicety.
- `mappages(...)` (from the earlier VM guide's Part 6.6) installs the actual leaf PTE with `PTE_U | PTE_R | PTE_W`.
- `sfence_vma()` — mandatory, per the earlier VM guide's Part 5.5 rule: any time you write a fresh mapping, fence before letting execution rely on it.

### 4.4 Edge cases (from the lab-lecture slide, "the details that bite")
- A fault **below the guard page** (in the stack, or under it) is invalid — never grow into it.
- A fault **at or above `USER_TOP`** must *never* be satisfied — that's kernel territory, full stop.
- Satisfy the fault **only** if the address lies in memory `sbrk()` has promised as lazy heap — not the guard, not the stack, not any special mapping.
- If allocation genuinely fails (out of physical memory), **kill the process cleanly** rather than let the kernel get into an inconsistent state — the kernel itself must survive regardless of what any individual process does.

> "Real kernels are mostly these edge cases. Every detail matters; the happy path is the easy 20%."

### 4.5 How OSTEP frames the same idea (demand zeroing)
OSTEP Chapter 23 describes a VMS-style implementation where the OS marks a new page **inaccessible** and uses software-reserved PTE bits to remember "this inaccessible page is a demand-zero page." On first access, the trap handler recognizes that special state, allocates, zeros, and maps.

| | OSTEP VMS example | Simple Luit lazy exercise |
|---|---|---|
| Logical page exists? | yes | yes (heap extent has grown) |
| Physical page allocated? | no | no |
| How is deferred state recognized? | inaccessible PTE + OS-reserved PTE state | faulting VA lies inside the promised lazy-heap region |
| First access | traps | traps |
| Resolution | allocate, zero, map | allocate, zero, map |

**Same optimization, different encoding.** OSTEP's version stores the "this is deferred" fact *inside* the PTE (via reserved bits); Luit's simple exercise instead infers it from *external* address-space metadata (comparing the address against the known heap range). The lecture's own conclusion: **a page-table format does not dictate the entire virtual-memory design — the OS chooses what its own metadata means.** This is directly the same lesson as the RSW/`PTE_COW` point in Part 2.2: hardware gives you some free bits (or none at all), and it's entirely up to kernel design how (or whether) to use them.

---

## PART 5 — The Shared Zero Page

### 5.1 The optimization
A demand-zero page (BSS, a fresh `sbrk()` region) contains **no interesting data** before the first write — every byte is zero, always. Since many such pages across many processes are all "just zeros," instead of leaving each one individually absent (Part 4), the kernel can map **all of them to one single physical page** containing zeros, mapped **read-only**.

```
process A, VA x  ─┐
process B, VA y  ─┼──► ONE physical zero page   (all mappings readable, none writable)
process C, VA z  ─┘
```
Reads then complete **without allocating any private page at all** — a genuine memory saving beyond even lazy allocation, since many pages that are read but never written never need their own physical frame. **The first write faults**, and the writer receives a private, writable page at that point — exactly the copy-on-write mechanism, applied to a single universal shared page rather than to a page inherited from a specific parent process (Part 7). In the Luit lab sequence this scheme deliberately **reuses** the exact reference-counting and copy machinery built for COW — it isn't a separate mechanism, just COW with one page shared across unrelated processes instead of across a parent/child pair.

---

## PART 6 — Demand Paging: Bringing *Existing* Contents In on First Use

### 6.1 The distinction that must not be collapsed
> "Lazy allocation delays creating anonymous physical pages; demand paging delays bringing already-defined page contents into RAM."

- **Lazy allocation** (Part 4): there is **no old useful data** to retrieve — a fresh page should simply contain zeros.
- **Demand paging**: the page's contents **already exist somewhere outside RAM** — in an executable file, or swap space — and the fault causes those existing contents to be **read in**.

```
virtual page  →  not resident  →  fault  →  page-fault handler
                                              │
                                     allocate physical frame
                                              │
                                     read contents from backing store (file or swap)
```

### 6.2 A concrete application: demand-paged `exec()`
Rather than reading every code and data page before a process even begins running, the kernel can set up the logical executable mapping and **fetch each page as execution actually reaches it**. An instruction fetch to an unavailable code page then produces an **instruction page fault** (`scause = 12` — the one fault type this lecture hasn't used yet, since Parts 4/5/7/8 are all load/store faults); data accesses similarly fault on first use.

**Luit's baseline does not implement a full swap-backed VM system** — this is presented as a natural extension, not something you'll find fully built already. But the *mechanism* being taught (fault → classify → resolve → retry) is the exact same mechanism such a system would be built on.

---

## PART 7 — Copy-on-Write Fork

### 7.1 The problem: naive `fork()` is wasteful
A naive `fork()` copies the parent's entire user memory, page by page, into freshly allocated child pages — this correctly preserves the required semantics (parent and child start identical, later modifications stay private), but the cost can be large and is **often entirely wasted**, since a freshly forked child frequently calls `exec()` immediately afterward and discards the copied address space it never used.

### 7.2 The COW idea: change the *timing*, not the *semantics*
At `fork()` time, parent and child are made to point at the **same physical pages**. For every page that was **originally writable**, the kernel clears `PTE_W` and sets a new `PTE_COW` flag **in both** page tables. Reads remain perfectly legal (no fault). A **later write** hits the deliberately read-only mapping, raises a store page fault, and the kernel resolves it by creating a private writable copy for the writer only at that point.

```
immediately after COW fork():                    after the child writes:

 parent VA: R=1,W=0,COW=1  ─┐                      parent VA: R=1,W=0,COW=1 → page P, refcount=1
                             ├─► page P, refcount=2
 child  VA: R=1,W=0,COW=1  ─┘                      child  VA: W=1,COW=0     → page Q, refcount=1
```

### 7.3 The rule that must never be broken: only *writable* pages become COW
> This is a **correctness condition, not an optimization detail.**

Program text is normally `R=1, X=1, W=0` — it's read-only because the process is *not allowed* to modify it, for entirely separate reasons than COW-sharing efficiency. **If every read-only page were tagged COW indiscriminately**, a write attempt against program text would look, to the fault handler, exactly like a legitimate recoverable COW write — and the handler could end up **creating a private writable copy of what should be permanently protected code**. That would **silently weaken protection**, turning a security boundary into an accident of implementation.

So the conversion applies **only** to mappings whose *original* permissions included `PTE_W`. A page that was already read-only for its own reasons stays an ordinary read-only page, forever, and is **never** marked `PTE_COW`.

### 7.4 The fork-side transformation (conceptual)
```c
if (*pte & PTE_W) {
    uint64 flags = PTE_FLAGS(*pte);
    flags = (flags & ~PTE_W) | PTE_COW;

    *pte = PA2PTE(PTE2PA(*pte)) | flags;   // parent: read-only, COW
    // map the SAME physical address into the child, same flags
    // increment the physical-page reference count
}
```
**No physical page is copied here.** The "copy" that `fork()` semantically promises is represented entirely by **two page-table mappings pointing at one physical page, plus an incremented reference count** — the actual byte-for-byte copy is deferred until (and unless) a write genuinely forces it.

### 7.5 `cowfault()`: resolving a COW write
```c
int cowfault(pagetable_t pt, uint64 va) {
    if (va >= USER_TOP) return -1;

    pte_t *pte = walk(pt, PGROUNDDOWN(va), 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return -1;
    if (!(*pte & PTE_COW)) return -1;

    uint64 pa = PTE2PA(*pte);
    uint64 flags = (PTE_FLAGS(*pte) & ~PTE_COW) | PTE_W;

    if (palloc_ref_get((void*)pa) == 1) {
        *pte = PA2PTE(pa) | flags;          // last owner: just re-enable write
        sfence_vma();
        return 0;
    }

    char *mem = palloc();                    // still shared: make a private copy
    if (!mem) return -1;
    memmove(mem, (void*)pa, PGSIZE);

    *pte = PA2PTE((uint64)mem) | flags;
    sfence_vma();
    pfree((void*)pa);                        // release our reference to the old page
    return 0;
}
```
Two distinct code paths, both correct, driven by the **reference count**:
- **Last owner (`refcount == 1`)**: no other mapping shares this physical page anymore — copying it would achieve nothing. Just **clear `PTE_COW` and restore `PTE_W`** on the existing mapping. No allocation, no copy.
- **Still shared (`refcount > 1`)**: allocate a genuinely new page, `memmove` the old contents into it, repoint *this* process's PTE at the new page, and release one reference to the old page (`pfree`, which presumably decrements the refcount and only truly frees at zero).

**The reference count does double duty**: it tells the kernel whether the page is still shared (deciding which of the two branches to take), *and* it prevents a still-mapped physical page from being prematurely returned to the free-page allocator while some other process still legitimately points at it.

**`sfence_vma()` appears on every successful path that changes a live PTE** — but *not* on the early `-1` returns, since those paths never touch a PTE at all. This precisely follows the earlier VM guide's Part 5.5 rule, applied selectively: fence exactly when (and only when) you've actually changed a live mapping.

### 7.6 The kernel writes into user memory too: `copyout()` must be COW-aware
A **user** store to a COW page naturally reaches `cowfault()` because the CPU itself performs the failed store and traps. But the **kernel** also writes into user buffers — e.g., `read(fd, buf, n)` eventually copies bytes from the kernel into the user's `buf` via `copyout()` (recall the earlier VM guide's Part 6.10 and the Syscall guide's Part 12.1 — `copyout` was already flagged there as validating that the destination is writable).

**The problem:** if `buf` happens to sit on a page that's currently `PTE_W=0, PTE_COW=1` (because this process was just forked), a naive `copyout()` that simply rejects any non-writable destination would **break perfectly valid programs** immediately after a COW `fork()` — even though, semantically, the process absolutely should be allowed to have data copied into its own buffer.

**The fix — Luit's COW-aware `copyout()`:**
```c
if (!(*pte & PTE_W)) {
    if ((*pte & PTE_COW) && cowfault(pt, va0) == 0)
        pte = walk(pt, va0, 0);   // mapping may now point elsewhere — re-fetch it
    else
        return -1;
}
```
If the destination isn't writable, check whether it's specifically a COW page; if so, **call `cowfault()` proactively** (not waiting for a hardware trap — the kernel invokes the exact same resolver logic on its own initiative) to break the sharing, then **re-walk** the PTE, since `cowfault()` may have repointed it at an entirely different physical page.

**The invariant this preserves, stated generally:** *no writer — including the kernel acting on the process's behalf — may modify a physical page that is still logically shared under COW.* This is a genuinely important generalization: "fault handling" is not just "one branch inside `usertrap()`" — it's a *property that must hold* everywhere a write could occur, hardware-triggered or software-initiated.

---

## PART 8 — Memory-Mapped Files

### 8.1 The interface `mmap()` provides
Ordinary file I/O moves data through explicit syscalls: `read()` copies bytes from a file into memory, `write()` copies memory back to a file. `mmap()` instead creates a **standing correspondence** between a range of file offsets and a range of virtual addresses — once established, **ordinary loads and stores** access the file's bytes directly, with no explicit `read`/`write` call needed per access.

**The virtual-memory insight**: the kernel doesn't have to populate the *entire* mapped image at `mmap()` time — it can just record the relationship, and let the **first access to each page** fault it in from the file, exactly as with any other lazy mechanism in this lecture.

### 8.2 The VMA (virtual-memory area): metadata the page table alone can't hold
A page table can't describe an *unmapped* file-backed page, because — by definition — there's no leaf PTE for it yet. Luit therefore needs a separate, higher-level record per mapping, a **VMA**, holding at minimum:
- the virtual start address,
- the mapping length,
- the requested protections (readable / writable),
- the backing file,
- the starting offset within that file.

**When a fault occurs, the kernel searches for a VMA whose interval contains the faulting address.** That search is precisely how the kernel knows "this absent PTE means *load this file page*," rather than "this is an invalid pointer" — it's the exact same "classify using region metadata, not PTE state alone" principle from Part 3.2, applied to a third kind of region.

```
VMA: start=0x40000000, off=8192, file=F

 VA page 0x40000000  →  file offset 8192
 VA page 0x40001000  →  file offset 12288
 VA page 0x40002000  →  file offset 16384
```
For a faulting page-base address `va`, the backing-file offset is computed as:
```
file_offset = vma->off + (va - vma->start)
```
Two subtleties worth remembering: this calculation uses the **page-aligned** virtual address (not the raw faulting byte address), and it does **not** depend on the file descriptor's ordinary seek position at all — **the virtual address determines the needed file offset directly**, entirely independent of any prior `read`/`write`/`lseek` activity on that file descriptor.

### 8.3 `mmap_fault()`: faulting a file page into memory
```c
int mmap_fault(struct proc *p, uint64 va, uint64 scause) {
    va = PGROUNDDOWN(va);
    struct vma *v = /* VMA containing va */;
    if (!v) return -1;

    if (scause == 15 && !(v->prot & PROT_WRITE)) return -1;
    if (scause == 13 && !(v->prot & PROT_READ)) return -1;

    char *mem = palloc();
    if (!mem) return -1;
    memset(mem, 0, PGSIZE);

    ilock(v->f->ip);
    int n = readi(v->f->ip, 0, (uint64)mem,
                  v->off + (va - v->start), PGSIZE);
    iunlock(v->f->ip);
    if (n < 0) { pfree(mem); return -1; }

    int perm = PTE_U | PTE_R;
    if (v->prot & PROT_WRITE)
        perm |= PTE_W;

    if (mappages(p->pagetable, va, PGSIZE,
                 (uint64)mem, perm) != 0) {
        pfree(mem);
        return -1;
    }

    sfence_vma();
    return 0;
}
```
Reading this against everything established so far:
- **Fault-type-aware permission check** — a store fault against a read-only VMA (`scause==15` but no `PROT_WRITE`) is rejected outright; same for a load against a non-readable VMA. This is exactly the "permission check, not just translation" theme from Part 2.1, now applied to VMA-level permissions rather than raw PTE bits.
- **`memset(mem, 0, PGSIZE)` before reading file data** — this deliberately mirrors Part 4.3's zeroing rationale: if the file is shorter than a full page (near EOF), the *unfilled tail* of the page must read as zero, **not** as leftover garbage from whatever process previously owned that physical frame. Same information-leak concern as lazy allocation, applied here to file-backed memory instead of anonymous memory.
- **`readi(...)` at `v->off + (va - v->start)`** — the exact offset formula from Part 8.2, used directly as the read position.
- **Permission bits mirror the VMA's declared protections** — a mapping created with only `PROT_READ` never gets `PTE_W`, regardless of what the underlying file itself allows.
- **`sfence_vma()`**, again, immediately before returning success — the same unconditional-after-a-live-mapping-change rule as everywhere else in this lecture.

**This is demand paging (Part 6), concretely instantiated**: the logical mapping exists (the VMA), the backing content exists (in the file), and a physical page is populated with real content only when an access actually demands it.

---

## PART 9 — One Fault Hook, Several Resolvers

All the mechanisms above (lazy heap, COW, mmap) share **one entry point**: the `scause == 13 || scause == 15` branch inside `usertrap()`. The order resolvers are tried in is itself a **policy decision**, not an incidental detail:

```c
} else if (scause == 15 || scause == 13) {
    uint64 va = r_stval();

    if (scause == 15 && cowfault(p->pagetable, va) == 0) {
        /* COW write resolved */
    } else if (mmap_fault(p, va, scause) == 0) {
        /* file-backed page populated */
    } else if (lazy_fault_enabled && lazy_fault(p, va) == 0) {
        /* anonymous page allocated */
    } else {
        p->killed = 1;
    }
}
```
Notice `cowfault` is only ever tried for **stores** (`scause == 15`) — reads from a COW page never fault in the first place, since `R` remains set the whole time (Part 7.2), so there's no reason to even attempt COW resolution on a load fault.

```
                          page fault
                              │
              inspect scause, stval, PTE, regions
    ┌───────────┬──────────────┬──────────────┬────────────────┐
lazy heap     COW store      mmap VMA      guard/bad-VA/
allocate      copy or        read file      protection
zero page     restore W      page           violation
    │             │              │                 │
 return &      return &      return &          kill
  retry          retry         retry            process
```
**Why order is policy, not an implementation afterthought**: each resolver must be given the chance to claim an address *only if it's genuinely the right region*, or one resolver could accidentally "succeed" on an address that actually belongs to a different mechanism. This is precisely why the earlier emphasis on **address-space metadata and protection checks being central, not optional** (Part 3.2) matters so much here — a sloppy resolver that doesn't check its region tightly enough could silently mishandle another resolver's fault.

---

## PART 10 — Why the Same Instruction Can Succeed *After* the Fault

For every recoverable fault covered here, the kernel repairs whatever prevented the instruction from completing, then returns to user mode **without treating the instruction as already completed**. The saved user PC (`p->tf->epc`) still identifies the exact faulting instruction — so the load, store, or fetch **is attempted again**, and this time the (now-repaired) mapping lets it succeed.

**This retry property is what makes the entire mechanism invisible to ordinary user code.** A program never writes "if page missing, call the kernel" — it just does ordinary loads and stores; the page-table + exception machinery inserts the kernel silently, only when actually necessary.

### 10.1 A sharp, instructive contrast: system calls vs. recoverable faults
Recall from the Syscall guide's Part 8.3: for an `ecall`, the kernel **deliberately advances** the saved PC by 4, so that returning from a syscall resumes execution at the instruction *after* `ecall` (which is `ret`, per the stub in that guide's Part 5.2).

**For a recoverable page fault, doing the same thing would be a bug**: advancing the PC would **skip the very instruction that still needs to happen** — the load or store that faulted hasn't succeeded yet; it just failed once, for a reason the kernel has now fixed. So the handler leaves `epc` untouched and lets that **same** instruction execute again once control returns to user mode.

**One sentence to hold onto for a viva**: *a syscall's faulting instruction (`ecall`) has already done everything it was ever going to do — advance past it; a page-fault's faulting instruction hasn't done its job yet — don't advance past it, let it retry.*

---

## PART 11 — The TLB and `sfence.vma`, in the Page-Fault Context

This section sharpens the general rule from the earlier VM guide's Part 5.5 into two **concrete failure scenarios** specific to page-fault handling.

### 11.1 Scenario 1: forgetting to fence after tightening permissions (COW fork)
Suppose a parent page was writable, and the CPU has already **cached that writable translation in its TLB**. During COW `fork()` (Part 7.4), the kernel clears `PTE_W` in the *live* parent PTE in memory. **But if the processor keeps using the stale, still-writable TLB entry**, the parent could go on writing to the (now supposed to be shared, read-only) physical page **without ever faulting** — silently violating the entire COW invariant, since the child would then observe changes it was never supposed to see, and the "share until write" contract would be broken invisibly.

### 11.2 Scenario 2: forgetting to fence after loosening permissions (resolving COW)
Conversely, after `cowfault()` changes a PTE from read-only back to writable (Part 7.5's "last owner" branch), a **stale read-only TLB entry** can cause an entirely unnecessary *second* fault on the very next write — the mapping in memory is correct, but the cached translation the CPU is actually using is out of date.

```
   PTE in memory: W=1           cached →  TLB entry: W=1
        │
   kernel edits the PTE
        │  (without a fence, the OLD cached permission may still be used)
        ▼
   PTE in memory: W=0, COW=1  ──sfence.vma──►  invalidates the stale entry
```

### 11.3 The rule this lecture settles on
> "After changing a live mapping or its permissions, ensure stale translation state cannot be reused before returning to the affected user execution."

RISC-V actually offers more *precise* fence variants (one virtual address, one address-space ID, or broader sets — plus modern extensions that refine exactly when a fence is architecturally required for a given PTE transition). **Luit deliberately uses the simpler, conservative rule instead** — fence unconditionally after any live-PTE change — for teaching clarity, accepting a little extra flushing cost in exchange for never having to reason precisely about which specific transitions technically require a fence and which don't.

This directly explains why **every successful path** in `cowfault()` (Part 7.5) and `mmap_fault()` (Part 8.3) calls `sfence_vma()` right before returning — it isn't cargo-culted; each call sits exactly at the point where a live PTE has just been created or modified.

---

## PART 12 — What's Standard OSTEP, and What's Luit-Specific

| Topic | OSTEP treatment | Luit realization |
|---|---|---|
| Page fault | OS handles a legal non-resident page (or other fault) after hardware transfers control | `scause` 12/13/15, `stval`, saved user PC (`p->tf->epc`), PTE inspection |
| Demand zero | Defer allocation/zeroing; VMS example marks the page inaccessible and records OS meaning *in PTE state* | Optional lazy-allocation exercise: grow `p->sz` with no valid leaf mapping, infer legality from **heap-region metadata** instead |
| Demand paging | Bring an existing page image from backing store into RAM on reference | Same mechanism conceptually; demand-paged `exec()` and swap are extensions beyond the baseline |
| COW | Share pages read-only; copy only when a write traps | Lab uses `PTE_COW`, reference counts, `cowfault()`, and a COW-aware `copyout()` |
| `mmap()` | Virtual addresses correspond to byte offsets in a backing file | A **VMA** records the region; `mmap_fault()` computes the file offset and populates a page lazily |
| TLB coherence | The VM system must keep translation caches consistent with the page table | Luit uses `sfence_vma()` in every live-PTE-update path covered here |

**Two terminology points the chapter is emphatic about:**
1. **"Lazy mapping" is not one sharply-defined mechanism** — it's a broad umbrella phrase. Prefer naming the *specific* delayed work: lazy allocation, demand paging, COW, or lazy mmap population. Using the vague umbrella term in an answer where precision is expected (e.g., a viva) is itself a mistake worth avoiding.
2. **Demand paging and swapping are related, but not identical.** Demand paging answers *when* a page comes into RAM (on demand, i.e., at first reference). Page replacement / swapping answers a *different* pair of questions: what leaves RAM under memory pressure, and where the evicted contents are kept. This lecture is squarely about the first question; the second is explicitly flagged as coming later (OSTEP Ch. 22–23, per the roadmap document's syllabus map).

---

## PART 13 — Real-World Scaling (Linux)

Production kernels build **far more policy** on this exact same foundation. Linux maintains rich per-process VM metadata, supports both anonymous and file-backed mappings, demand-faults executable and data pages, uses COW for `fork()`, and links mapped files to the ordinary page cache used by regular file I/O. Beyond that baseline, it may: reclaim clean file-backed pages (their contents can simply be re-fetched from the file later), write back dirty pages before reclaiming them, reclaim anonymous pages under memory pressure (this is genuine swapping), use huge pages, migrate pages between memory tiers, and coordinate TLB invalidation across **multiple CPUs simultaneously**.

**The crucial point the chapter makes about scale:** the difficulty in production is **not** that the basic page-fault mechanism itself changes — it's that **many legal meanings can overlap** on a single fault (COW *and* a file mapping *and* NUMA placement *and* a userfault handler *and* a swapped-out page *and* protection keys *and* huge pages *and* a concurrent unmap from another CPU, all potentially relevant to the same address at once), and the kernel must classify correctly **while other CPUs may be actively modifying the same address space concurrently**. Reference counts, locks, per-mapping metadata, memory ordering, and cross-hart TLB shootdowns become just as important as the local act of installing one PTE.

**Why Luit's smaller vocabulary is pedagogically useful, not merely simpler:** every invariant taught here stays fully visible with nothing to obscure it — a missing mapping doesn't mean "allocate," the region must specifically authorize it; a read-only PTE doesn't mean "COW," software metadata (`PTE_COW`) must explicitly say so; a COW copy isn't "done" the moment bytes are copied — reference counts and TLB state must *also* be correct; a file page isn't identified by wherever the file descriptor's seek position happens to be — the VMA and the faulting virtual page identify it directly (Part 8.2). **These are not teaching-only simplifications — they are the same habits of precise state classification that production virtual-memory code genuinely requires**, just without the additional concurrency and overlapping-feature complexity layered on top.

---

## Quick-Reference: Vocabulary Introduced in This Lecture
| Term | One-line meaning |
|---|---|
| **VMA (virtual-memory area)** | Per-mapping metadata (start, length, protections, backing file, file offset) that lets the fault handler classify an absent mmap'd page |
| **`PTE_COW`** | A software-defined RSW bit meaning "this read-only page is read-only *for COW reasons*, not because it's inherently protected" |
| **Lazy allocation** | Deferring the *creation* of a fresh, zero-filled anonymous physical page until first touch |
| **Demand paging** | Deferring bringing *already-existing* page contents (from a file or swap) into RAM until first reference |
| **Shared zero page** | One physical all-zero page mapped read-only into many virtual addresses across many processes; COW machinery handles the first write to any of them |
| **Guard page** | An intentionally, permanently unmapped page placed below the stack so overflow faults cleanly instead of corrupting adjacent memory |
| **`cowfault()`** | Luit's COW-write resolver: restores `PTE_W` directly if this process is the last owner, otherwise copies the page privately |
| **`mmap_fault()`** | Luit's file-backed-page resolver: looks up the VMA, computes the file offset, reads the page in, zero-pads any tail past EOF, installs the mapping |
| **`lazy_fault()`** | Luit's optional anonymous-heap resolver: validates the address is inside the promised heap range, then allocates/zeroes/maps |

## How This Lecture Extends the Earlier Two Guides
- **`scause`, `stval`, `sepc`/`p->tf->epc`** (Syscall guide, Part 2.1 and 8.4) are the exact three facts Part 2 of this guide says every fault handler needs — this lecture is where those CSRs get used for something *other* than dispatching a syscall.
- **The `scause == 13 || scause == 15` branch** stubbed as "later Lab (COW), Lab (mmap) resolve faults HERE" in the Syscall guide's Part 8.4 is now filled in completely (Part 9 above).
- **`sfence_vma()`, `PTE_U`, `walk()`, `mappages()`, `PA2PTE`/`PTE2PA`** (VM guide, Parts 5.5, 6.2, 6.5, 6.6, and the dedicated PA2PTE explanation) are reused directly, unmodified, throughout every code listing in this guide — nothing here introduces a new hardware mechanism; it's entirely new **policy** built on the exact same mechanism.
- **`copyin`/`copyout`** (VM guide, Part 6.10; Syscall guide, Part 12.1) gets a genuinely new wrinkle here (Part 7.6): it isn't enough for `copyout()` to just check "is this writable?" — after COW, it must actively **resolve** a COW page itself, on the kernel's own initiative, rather than only reacting to hardware-triggered faults.
