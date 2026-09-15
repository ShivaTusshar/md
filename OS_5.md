# Virtualizing Memory: Complete Concept Guide

This guide walks through **why** virtual memory exists, **how** it's built up mechanism by mechanism (base/bounds → segmentation problems → paging), how **free space** is managed underneath it, and finally how it's all realized concretely in **RISC-V Sv39** and the **Luit** kernel (with contrasts to xv6). The order below follows the logical dependency chain, not the order the PDFs were uploaded in.

---

## PART 1 — The Problem: Why Virtualize Memory At All?

### 1.1 The historical starting point
Early machines had **no memory abstraction**: the OS sat at low physical addresses, one program occupied the rest of physical memory, and that program's addresses *were* physical addresses. This was simple but didn't scale once people wanted **multiprogramming** (several processes ready to run, OS switches between them to keep the CPU busy during I/O) and then **time-sharing** (many interactive users concurrently).

### 1.2 The three goals of virtual memory
1. **Transparency** — the running program shouldn't notice it's been virtualized. It behaves as though it owns physical memory outright.
2. **Efficiency** — virtualization shouldn't cost much in time (extra instructions per memory access) or space (extra memory used for translation bookkeeping). This requires **hardware support** (registers, later TLBs and page-table walkers).
3. **Protection / Isolation** — one process must not be able to read or write another process's memory, or the OS's memory. This is what lets a buggy or malicious process fail without taking the whole machine down.

### 1.3 The address space abstraction
Every process gets its own **address space**: a view of memory starting at address 0, containing (conceptually) three main regions, laid out by address (low → high):
- **Code** — sits at the *low-address* end (e.g., addresses 0–1KB in a toy 16KB example), fixed in place since it's static and never grows.
- **Heap** — dynamically allocated memory (`malloc`/`new`); starts right after the code and grows **toward higher addresses** as more is allocated.
- *(an unused gap in between, which shrinks as either side grows into it)*
- **Stack** — local variables, function-call bookkeeping, return addresses; starts at the *high-address* end (e.g., 16KB) and grows **toward lower addresses** with each nested call.

Putting heap and stack at opposite ends of the address range and growing them toward each other lets both expand without having to fix a boundary between them ahead of time. (Careful: some diagrams of this layout are drawn with high addresses at the *top* of the picture, others with high addresses at the *bottom* — the picture orientation is just a drawing choice. The fact that never changes is: **heap grows toward increasing addresses, stack grows toward decreasing addresses.**) **Every address a user program prints or manipulates is a virtual address** — only the OS + hardware together know the real physical location.

The core question the rest of these notes answer:
> **How does the OS build the illusion of a private address space for every process, on top of one shared physical memory?**

---

## PART 2 — Mechanism #1: Base and Bounds (Dynamic Relocation)

This is the simplest possible hardware mechanism for address translation, and it's the ancestor of everything that follows.

### 2.1 The idea
Two hardware registers per CPU, part of the **MMU (memory management unit)**:
- **Base register** — physical address where this process's address space starts.
- **Bounds (limit) register** — the size of the address space (or, in an alternate convention, the physical end address).

Every memory reference the CPU makes is translated:
```
physical address = virtual address + base
```
and checked:
```
if virtual address >= bounds (or < 0): raise exception (protection fault)
```

### 2.2 Why it works
The program is *compiled and written as if it starts at address 0*. The OS decides, when the process is loaded, where in physical memory it will actually live, and sets `base` accordingly. The process never has to know.

### 2.3 What hardware must provide
- **Privileged/kernel mode vs. user mode** — a mode bit so that only the OS can modify base/bounds.
- **The base and bounds registers themselves.**
- **Translation + bounds-check circuitry** on every memory access.
- **Privileged instructions** to update base/bounds (used by the OS on context switch).
- **Exception-raising hardware** for out-of-bounds accesses or attempts to touch privileged registers from user mode.
- A way to register **exception/trap handlers** so the CPU knows what code to jump to when something goes wrong.

### 2.4 What the OS must do
- **On process creation:** find a free slot of physical memory big enough (tracked via a **free list**), mark it used.
- **On process termination:** return that memory to the free list.
- **On context switch:** save the outgoing process's base/bounds into its process control block (PCB); load the incoming process's base/bounds into the hardware registers.
- **Exception handling:** typically, terminate the offending process.

Because this all happens at run time and the OS can even relocate a *stopped* process by copying its memory and updating its saved base register, this is called **dynamic relocation** (as opposed to old-school **static relocation**, where a software *loader* rewrote every address in the binary once, at load time — which offered no real protection, since the process could still compute and use an out-of-bounds address).

### 2.5 The fatal flaw: internal fragmentation
Base-and-bounds treats the *entire* address space as one contiguous physical chunk. If the heap and stack are small, the large gap between them is allocated physical memory doing nothing — wasted. This waste, occurring *inside* an allocated unit, is called **internal fragmentation**. It's the motivation for the next idea: **segmentation** (splitting code/heap/stack into independently-relocated variable-sized chunks) — and, when segmentation's own problems emerge (see Part 3), **paging**.

---

## PART 3 — Free-Space Management (the general problem, independent of VM)

This matters both for a user-level `malloc` library managing the heap, and for an OS managing physical memory when using variable-sized chunks (e.g., segmentation). It does **not** matter for paging, because paging uses fixed-size units (see Part 4) — this is precisely paging's big advantage.

### 3.1 External vs internal fragmentation
- **External fragmentation**: total free memory is enough to satisfy a request, but it's split into pieces individually too small. E.g., 20 bytes free total, split into two 10-byte chunks — a 15-byte request fails even though 20 bytes are free.
- **Internal fragmentation**: memory *inside* an allocated unit goes unused (e.g., handing out a fixed 16KB block when only 5KB was requested).

### 3.2 Splitting and coalescing
- **Splitting**: when a request is smaller than any single free chunk, the allocator cuts a free chunk into two — one piece satisfies the request, the remainder stays on the free list.
- **Coalescing**: when memory is freed, the allocator checks whether the freed chunk is physically adjacent to other free chunks, and if so, merges them into one larger chunk. Without coalescing, a fully-free heap can look artificially fragmented into many small chunks.

### 3.3 Tracking allocation sizes: the header
Since `free(ptr)` takes no size argument, allocators stash a small **header** just before the returned pointer, containing at least the **size** of the allocated region (often also a **magic number** for corruption checking). `free()` does `header_t *hptr = (header_t*)ptr - 1;` to find it. Consequently, when a user asks for N bytes, the library actually searches for a free chunk of size **N + header size**.

### 3.4 Building the free list *inside* free space
The allocator can't call `malloc()` on itself to get list-node memory, so it embeds the list nodes directly inside the free chunks themselves (a `size`/`next` struct written into the first bytes of each free region). This is a neat "bootstrapping" trick worth remembering.

### 3.5 Growing the heap
When out of space, an allocator can just fail, or ask the OS for more (e.g., via `sbrk`), which maps new physical pages into the process's address space.

### 3.6 Allocation policies (searching the free list)
| Policy | Rule | Pro | Con |
|---|---|---|---|
| **Best fit** | Search everything; return smallest chunk that still fits | Minimizes wasted space per allocation | Full list search = slow; leaves many tiny leftover fragments |
| **Worst fit** | Search everything; return the largest chunk | Tries to keep leftover chunks big | Also a full search; empirically fragments badly *and* is slow |
| **First fit** | Return the first chunk found that's big enough | Fast — no exhaustive search | Can "pollute" the front of the list with small leftover fragments; ordering the list (e.g., by address) helps |
| **Next fit** | Like first fit, but resumes searching from where the last search left off | Similar speed to first fit, spreads out fragmentation more evenly | — |

**Worked example** (free list of sizes 10, 30, 20; request size 15):
- Best fit → picks the 20-chunk (smallest that fits) → leaves 10, 30, **5**
- Worst fit → picks the 30-chunk (largest) → leaves 10, **15**, 20
- First fit → picks the first fitting chunk in list order (here, same as worst fit's target since 10 doesn't fit) → leaves 10, **15**, 20, but with less search cost

### 3.7 Beyond the basics
- **Segregated lists**: keep separate free lists per popular size class, so common-size requests are O(1) with no fragmentation risk between classes. The Solaris **slab allocator** (Bonwick) extends this by keeping freed objects *pre-initialized*, saving repeated init/destroy cost for kernel objects like locks and inodes.
- **Buddy allocation**: think of free space as one big power-of-two block; a request recursively splits the block in half until the smallest block that still fits is found. Freeing is elegant: a block's "buddy" (address differs by exactly one bit) is checked, and if it's also free, they coalesce back together, recursively. Downside: internal fragmentation, since only power-of-two sizes are given out.
- Real-world allocators (e.g., **Hoard**, **jemalloc**) add multiprocessor scalability using more advanced data structures (trees, per-thread pools) instead of plain lists.

**Key link back to paging**: paging sidesteps *all* of this because it only ever allocates fixed-size pages — free-space management degenerates to "keep a list of free fixed-size slots," trivial compared to variable-sized allocation.

---

## PART 4 — Mechanism #2: Paging

### 4.1 Why paging instead of segmentation
Segmentation still deals in *variable-sized* pieces (code segment, heap segment, stack segment), so it inherits **external fragmentation** problems. Paging's fix: chop the address space into **fixed-size pages**, and physical memory into equal-size **page frames**. Any free frame can hold any page — free-space management collapses to a simple free list of same-size slots (Part 3.6 problems mostly vanish).

### 4.2 Vocabulary
| Term | Meaning |
|---|---|
| **Virtual page** | A fixed-size chunk of a process's virtual address space |
| **Page frame** | A fixed-size slot of physical memory |
| **PTE (page-table entry)** | One entry mapping a virtual page → a physical frame, plus permission/status bits |
| **Page table** | Per-process data structure holding all of that process's PTEs |

### 4.3 Splitting a virtual address: VPN + offset
A virtual address is split into a **Virtual Page Number (VPN)** and a **page offset**. The offset is *never translated* — it just selects the byte within whichever page frame the VPN maps to.

**Tiny worked example** (64-byte address space, 16-byte pages → 4 pages, so 2-bit VPN + 4-bit offset):
- Virtual address 21 = `010101₂` → VPN = `01₂` = 1, offset = `0101₂` = 5
- Page table says VPN 1 → PFN 7 (`111₂`)
- Physical address = `111` + `0101` = `1110101₂` = **117**

The general recipe: `PhysAddr = (PTE.PFN << offset_bits) | offset`.

### 4.4 Where page tables live, and why they're big
Page tables are stored in memory (usually OS-managed physical memory), not in special on-chip registers, because they can be **huge**. Classic example: 32-bit address space, 4KB pages → 20-bit VPN → 2²⁰ (~1 million) possible entries per process. At 4 bytes/PTE that's 4MB *per process* — 400MB for 100 processes just for translation metadata. This "**page tables are too big**" problem is exactly what motivates the multi-level tree structure covered in Part 5.

### 4.5 What's inside a PTE
Typical fields (concrete x86 example bits shown for reference):
- **Valid bit** — is this a real mapping? Marking unused regions of a sparse address space invalid means you never have to allocate physical frames for them.
- **Protection bits** — readable / writable / executable.
- **Present bit** — is the page actually in physical memory, or has it been swapped to disk? (On x86 there's no separate valid bit — a single Present bit does double duty; if it's 0, the OS's other structures determine whether that's "swapped out" or "illegal.")
- **Dirty bit** — has the page been modified since being loaded (relevant for swapping — a clean page can just be dropped, a dirty one must be written back).
- **Reference/accessed bit** — has this page been touched recently (used by page-replacement policies).
- **PFN / PPN** — the actual physical frame number.

### 4.6 Paging's own two problems
1. **Speed**: every single memory reference now costs *two* memory accesses — one to fetch the PTE from the page table, one to fetch/store the actual data. This can roughly double memory-access latency if unaddressed (the fix, hardware **TLB caching**, is used throughout the RISC-V material in Part 5).
2. **Space**: as shown above (4.4), a full linear (array-indexed-by-VPN) page table per process is wasteful, especially since most address spaces are **sparse** (big unused gaps between code/heap/stack). The fix: multi-level ("tree") page tables that only allocate lower levels for the parts of the address space actually in use — exactly the RISC-V Sv39 approach in Part 5.

### 4.7 The full lookup algorithm (conceptually, before multi-level trees)
```
VPN      = (VirtualAddress & VPN_MASK) >> SHIFT
PTEAddr  = PageTableBaseRegister + VPN * sizeof(PTE)
PTE      = AccessMemory(PTEAddr)
if not PTE.Valid:            raise SEGMENTATION_FAULT
if not CanAccess(PTE.bits):  raise PROTECTION_FAULT
offset   = VirtualAddress & OFFSET_MASK
PhysAddr = (PTE.PFN << SHIFT) | offset
```
This is the software/hardware dance that Part 5's RISC-V walk hardware performs natively.

---

## PART 5 — RISC-V Sv39: A Concrete, Real Multi-Level Page Table

Sv39 is RISC-V's answer to "page tables are too big": a **three-level tree**, where a missing branch simply means "no mapping" — so sparse regions of the address space cost nothing.

### 5.1 The address split
A 39-bit virtual address (Sv39 = "39-bit virtual addressing") is divided as:

| Bits | Field | Purpose |
|---|---|---|
| 38..30 | VPN[2] (9 bits) | Index into the Level-2 (root) table |
| 29..21 | VPN[1] (9 bits) | Index into the Level-1 table |
| 20..12 | VPN[0] (9 bits) | Index into the Level-0 (leaf) table |
| 11..0 | offset (12 bits) | Byte offset within the final 4KiB page (never translated) |

Each 9-bit index selects 1 of 512 entries; each page-table page is exactly one 4KiB page holding 512 × 8-byte entries. Page size on RISC-V (and in Luit) is **4 KiB**.

### 5.2 Why a tree beats one giant array
A naive linear table indexed by full VPN would need 2²⁷ entries × 8 bytes ≈ **1 GiB per process** — absurd, since real address spaces are sparse. The tree instead allocates a Level-1 table only where a Level-2 entry is actually used, and a Level-0 (leaf) table only where a Level-1 entry is actually used. **Missing branch = no mapping**, at zero memory cost.

### 5.3 Page-table entry (PTE) format in Sv39
```
PPN(53..10) | RSW(9..8) | D(7) | A(6) | G(5) | U(4) | X(3) | W(2) | R(1) | V(0)
```
- **V** — valid.
- **R/W/X** — read/write/execute permission.
- **U** — accessible from **U**ser mode. (This is the single bit Luit uses to hide the kernel from user code, see Part 6.)
- **PPN** — physical page number.
- Interior (non-leaf) PTEs set only **V** (they just point deeper into the tree); **leaf** PTEs carry the real permission bits. *Common bug*: setting R/W/X on an interior PTE makes hardware treat it as a leaf prematurely.

### 5.4 The walk: what happens on a memory reference
```
CPU issues VA → TLB lookup
  TLB hit  → translation returned immediately
  TLB miss → satp gives the root table → walk L2 → L1 → L0 → form PA
```
If at any level the required PTE is invalid, or the requested access violates the PTE's permission bits, the CPU raises a **page fault**, recording the offending address in **stval** and the fault type in **scause**.

### 5.5 `satp` and `sfence.vma`
`satp` (Supervisor Address Translation and Protection register) tells the hardware which page table is currently active:
```
MODE(63..60=Sv39) | ASID(59..44) | PPN of root table (43..0)
```
Changing `satp` doesn't automatically discard old cached translations — the **TLB** may still hold stale entries from before the switch. `sfence.vma` is the instruction that flushes them. **Forgetting `sfence.vma` after changing `satp` or after modifying mappings is one of the classic, maddening, intermittent bugs** in OS kernels — the system "usually" works because the TLB happens to still hold correct translations, until it doesn't.

### 5.6 Permissions as protection
| Access attempted | PTE bits needed | Result |
|---|---|---|
| user read | V + R + U | allowed |
| user write | V + W + U | allowed |
| user execute | V + X + U | allowed |
| user reads kernel page | V + R but **U = 0** | page fault |
| write to read-only text | V + R + X but **W = 0** | page fault |

### 5.7 Worked hand example
Given VA `0x3ABC`, PTE PPN `0x12345`, flags V/R/W/U all set:
- offset = `0xABC` (low 12 bits)
- VPN[0] = 3
- Physical address = `0x12345ABC`
- User write allowed (W and U both set)

---

## PART 6 — The Luit Kernel: How xv6 Ideas Get Realized in Practice

Luit is a teaching kernel built on Sv39 hardware, and the lecture repeatedly contrasts it with **xv6** to highlight a real design choice in kernel architecture.

### 6.1 The core design decision: is the kernel mapped in every process's page table?

| | **xv6** | **Luit** |
|---|---|---|
| Kernel mapping in user page table? | No (separate kernel page table); only a tiny **trampoline**/trapframe page shared at the same VA in both tables | Yes — kernel occupies the high half of *every* process's page table |
| Trap-entry page-table switch? | Yes — `satp` must change on trap entry/exit | No switch needed; the kernel is already mapped |
| Trampoline required? | Yes | No |
| Kernel protected from user code? | By simply not existing in the user table | By clearing **PTE_U** on kernel mappings (they exist, but are inaccessible from user mode) |
| Trade-off | Stronger structural separation, but needs the tricky trampoline mechanism to survive the moment `satp` itself changes | Simpler & faster trap entry (no `satp` switch, no trampoline) — but caps how much virtual address space is left for the user half, since the kernel eats the top half of every address space |

**Why xv6 needs a trampoline at all**: the instant you write a new value into `satp`, the *next* instruction fetch uses the new page table. If that next instruction (the code that finishes the trap-entry sequence) isn't mapped at the *same virtual address* in both the old and new page table, the CPU would fault immediately after switching. The trampoline is a single page of code mapped at an identical VA in every page table, precisely to survive that moment.

**Luit's alternative**: never switch `satp` on trap entry in the first place, because the kernel's mappings are already present (just inaccessible to user mode via PTE_U=0). This removes the whole trampoline problem, at the cost of the kernel/user split eating into the usable user virtual address range (`MAXVA = 2^38`, with the split at `0x80000000`).

### 6.2 The Luit address space layout
```
MAXVA = 2^38 (top)
┌───────────────────────────────┐
│ Kernel region (PTE_U clear)    │  MMIO aliases, kernel text/data/stacks
├───────────── 0x80000000 ───────┤
│ User region (PTE_U set)        │  program text/data, heap, user stack
└───────────────────────────────┘  (0)
```
Every process's page table has **identical upper (kernel) entries**, borrowed from a shared kernel page table, and a **unique lower (user) region**. Isolation between processes is *not* achieved by hiding the kernel — it's achieved purely by the **PTE_U bit**.

### 6.3 Boot sequence: building the kernel's own page table
```
kvminit():
    map MMIO aliases for discovered devices (from the device tree)
    map kernel text:      R | X, not W      (protects code from accidental/malicious writes)
    map kernel data/RAM:  R | W, not X      (data is never executable)

kvminithart():
    w_satp(MAKE_SATP(kernel_pagetable))
    sfence_vma()
```
Note the **W^X** discipline enforced here: a page is either writable *or* executable, never both — this is a standard security hardening technique (prevents, e.g., injecting code into a writable buffer and then executing it).

Paging only becomes "live" the instant `satp` is written *and* the fence executes — before that, addresses are physical.

### 6.4 The key functions in Luit's `vm.c` (and what each one owns)

| Function | Role |
|---|---|
| `kvminit` | Build the kernel's page table at boot |
| `kvminithart` | Install a page table into `satp` (used both at boot, and — if per-hart — per core) |
| `walk` | Descend the Sv39 tree for a given VA, optionally allocating missing interior tables |
| `mappages` | Install a leaf PTE (calls `walk` with `alloc=1`, then writes `PA2PTE(pa) \| perm \| V`) |
| `uvmunmap` | Remove mappings, optionally freeing the underlying physical page |
| `uvmcreate` | Allocate a fresh root page-table page for a *new process*, then copy in the shared kernel's upper entries |
| `uvmfree` | Free a process's *user-owned* physical pages and page-table pages — **never** the borrowed kernel upper entries |
| `copyin` / `copyout` | Validate a user-supplied pointer page-by-page before the kernel touches it |

### 6.5 `walk`, annotated
```c
pte_t *walk(pagetable_t pt, uint64 va, int alloc) {
  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pt[PX(level, va)];      // PX() extracts the 9-bit index for this level
    if (*pte & PTE_V)
      pt = (pagetable_t) PTE2PA(*pte);    // descend into existing next-level table
    else {
      if (!alloc) return 0;               // lookup-only mode: fail cleanly
      pt = palloc();                      // allocate a new interior page-table page
      memset(pt, 0, PGSIZE);
      *pte = PA2PTE(pt) | PTE_V;          // link it in (interior PTE: only V is set)
    }
  }
  return &pt[PX(0, va)];                  // return address of the level-0 (leaf) PTE slot
}
```
Crucially: `walk` never allocates a *data* page — it only ever creates interior page-table pages. Filling in the actual leaf mapping (to a real data page) is `mappages`'s job.

**Worked example**: walking VA `0x3000` in an empty table with `alloc=1` → VPN[2]=0, VPN[1]=0, VPN[0]=3, offset=0. Level 2 entry 0 missing → allocate a Level-1 table. Level 1 entry 0 missing → allocate a Level-0 table. Return the address of Level-0 entry 3. **Two page-table pages allocated; no data page allocated by `walk` itself.**

### 6.6 `mappages` / `uvmunmap`
```
mappages(pt, va, size, pa, perm):
    walk() allocates any missing interior tables
    leaf PTE = PA2PTE(pa) | perm | V

uvmunmap(pt, va, n, do_free):
    clear the leaf PTE
    optionally free the underlying physical page
```
Luit deliberately **refuses to remap** an already-live leaf PTE — silently overwriting one would lose track of who owns that physical page, a correctness/ownership hazard.

### 6.7 Creating/destroying a process address space — the ownership invariant
```
uvmcreate():
    root = palloc(); zero it;
    copy the kernel's shared upper entries into root;
    return root;

uvmfree():
    free all user-owned physical pages
    free all user-owned page-table pages
    do NOT free the borrowed kernel upper entries
```
This "who owns this page or page-table page?" question is, per the lecture, one of the two questions every mapping/unmapping operation must answer correctly (the other being "which VA maps to which PA with which permissions?"). **Freeing borrowed kernel entries would corrupt every other process's page table**, since they all share those same physical page-table pages for the kernel region.

### 6.8 Physical page allocation: `palloc` / `pfree`
Luit's physical-page freelist is a classic instance of the "**embed the list inside free space itself**" trick from Part 3.4:
```c
struct run { struct run *next; };   // the "next" pointer lives inside the free page itself
```
`palloc()` pops the head of the freelist; `pfree()` pushes onto it. No separate allocation metadata is needed. At boot, the allocator must additionally reserve the kernel's own image and the device-tree blob so they aren't handed out as "free."

### 6.9 `sbrk` and `exec`
- **`sbrk`** grows/shrinks the user heap: allocates physical pages, maps them **eagerly** (not lazily/on-demand), and must **zero** new pages before user code can see them (otherwise a process could read another process's leftover data — a real security bug class).
- **`exec`** builds a brand-new address space from an ELF binary: sets PTE permissions to match ELF section flags (never make non-executable data executable — another W^X-style discipline), places a **guard page** below the user stack (an unmapped page that turns stack overflow into a clean fault instead of silent corruption), and only **commits** the new image once it's fully built — so a failed `exec` leaves the old, still-working image intact.

### 6.10 Trusting (or not trusting) user pointers: `copyin`/`copyout`
A syscall argument that's "a pointer" is just a **number** from the kernel's point of view — it must be validated before use:
```
User pointer → walkaddr() → reject if:
    va >= USER_TOP
    PTE missing or invalid
    PTE_U is clear         (this address belongs to the kernel, not the user!)
    destination page not writable (for copyout)
→ otherwise, kernel copies data page-by-page
```
**The principle**: the kernel must never trust a raw user-supplied address, even though the trap mechanism got it safely into supervisor mode. This is exactly the address-translation + protection-bit machinery from Part 5 being reused for a *software* purpose (syscall argument validation), not just hardware memory access.

### 6.11 What happens on a page fault (today, in Luit)
```
User load/store/fetch → MMU checks PTE → fault → scause + stval set → kernel trap handler runs
```
Two fault flavors:
- **Invalid page fault** — no valid mapping exists at all.
- **Protection fault** — a mapping exists, but the specific access (e.g., write) isn't permitted by the PTE's bits.

Currently, Luit's response to a fault is simply to **report/kill the offending process** — it does not (yet) do **demand paging** (allocating a physical page lazily, the first time it's touched) or **copy-on-write** (sharing a page read-only between processes until one of them writes, then giving it a private copy) — both are flagged as natural "next steps" beyond what's covered here.

---

## PART 7 — Putting It All Together: A Full Address-Translation Trace

Combining Parts 4–6, here's what happens for **one instruction that writes to memory**, `movl $0, (%edi,%eax,4)` (i.e., `array[i] = 0`), assuming Sv39-style paging is active:

1. **Instruction fetch**: CPU wants to fetch the instruction itself from its own VA.
   - TLB checked first; on a miss, hardware/software walks the 3-level tree using that VA's VPN[2]/VPN[1]/VPN[0] to find the leaf PTE, checks V/R/X and U bits, forms the physical address, fetches the instruction.
2. **Instruction executes**, computing the target virtual address of `array[i]`.
3. A second, independent translation happens for that data address: TLB check, possible walk, permission check (must have W set, and U set since this is user-mode code), physical address formed.
4. The **store** actually happens at the physical address; the value 0 is written.
5. If any PTE along the way had **V=0**, or the required permission bit was missing, a **page fault** is raised instead, with `scause`/`stval` populated, and control transfers to the kernel's trap handler.

Every one of the OSTEP-chapter ideas (VPN/offset split, permission bits, TLB caching to avoid a full walk on every access, sparse trees to keep the table small) and every one of the Luit/RISC-V specifics (satp, walk(), PTE_U enforcing the kernel/user split, sfence.vma keeping the TLB honest after a mapping changes) are just this loop, made concrete.

---

## Quick-Reference: Common Bugs (and why they matter)
- **Forgetting `sfence.vma`** after changing `satp` or editing a mapping → stale TLB entries → intermittent, hard-to-reproduce bugs.
- **Putting R/W/X on an interior PTE** → hardware treats it as a leaf one level too early → wrong/garbage translations.
- **Mapping kernel text as writable** → breaks W^X, opens a code-injection path.
- **Setting PTE_U on a kernel mapping** → user code can now read/write/execute kernel memory — total protection failure.
- **Freeing "borrowed" kernel page-table entries in `uvmfree`** → corrupts every other process's page table, since they share those physical pages.

## Quick-Reference: Every Mapping Answers Three Questions, Every Free Answers One
- Mapping: **which VA → which PA → with which permissions?**
- Freeing: **who owns this page (or page-table page)?**

---

## How the Five Documents Connect
- *vm-intro.pdf* → the **why** (address spaces, goals: transparency/efficiency/protection).
- *vm-mechanism.pdf* → the **first, simplest mechanism** (base & bounds / dynamic relocation) and its fatal flaw (internal fragmentation) that motivates paging.
- *vm-freespace.pdf* → a **side-quest** on managing variable-sized free space (relevant to `malloc` and to segmentation, and useful contrast to paging's much simpler fixed-size free lists — plus it directly explains the free-list-embedded-in-free-space trick Luit's `palloc` reuses).
- *vm-paging.pdf* → **paging itself**: VPN/offset, PTE contents, the "too big / too slow" problems that multi-level tables and TLBs solve.
- *CS3105_Page_Tables_Luit_RISCV.pdf* → the **concrete realization**: Sv39's 3-level tree, real PTE bits, `satp`/`sfence.vma`, and the Luit kernel's specific engineering choices (kernel-mapped-everywhere vs. xv6's trampoline), down to actual C code (`walk`, `mappages`, `uvmcreate`/`uvmfree`).
