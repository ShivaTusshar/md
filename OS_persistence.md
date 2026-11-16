# Persistence I: I/O Devices, Disks, RAID, and the File API — Complete Concept Guide

This guide merges **eight sources** into one arc: the CS3106 decks (*I/O Devices, Files & Directories*; *Files and Directories* with its Luit part; *Hard Disk Drives*; *RAID*) and the four OSTEP chapters behind them (**Ch. 36** I/O Devices, **Ch. 37** Hard Disk Drives, **Ch. 38** RAID, **Ch. 39** Files and Directories).

The course has virtualized the CPU (processes, scheduling) and memory (address spaces, page tables). This is the **third pillar: persistence** — how the OS stores data that survives power loss. The motivating question from the first slide:

> A program calls `write(fd, buf, 4096)`. **Where do the 4096 bytes go?**

The path, which this guide fills in layer by layer:
```
user buffer → syscall → file layer → buffer cache → driver → device (DMA) → interrupt → wakeup
```

**Reading map**
| Part | Topic | Question it answers |
|---|---|---|
| A | I/O devices & drivers | How does the OS talk to hardware without wasting the CPU? |
| B | Hard disk drives | Why is random I/O so slow? How do we compute disk time? |
| C | Disk scheduling | In what order should pending requests be served? |
| D | RAID | How do we make storage bigger, faster, and more reliable? |
| E | Files & directories (the API) | What abstraction do programs see? |
| F | Luit and Linux side by side | What is implemented, what isn't, and why? |

---

# PART A — I/O Devices (OSTEP Ch. 36)

## A.1 Why I/O matters and why it's hard
A program with no input always behaves the same; a program with no output is useless. The crux: **how should I/O be integrated into systems, and how can it be made efficient?** Difficulties: device diversity, slow devices, CPU cost, and reliability. The OS must **hide device-specific detail while still exposing enough control for correctness and performance.**

## A.2 System architecture: a hierarchy of buses
CPU and memory sit closest because their bandwidth and latency demands are highest; high-performance devices (graphics, fast storage) sit on faster interconnects near the CPU; slow peripherals (disks, USB, keyboards) sit behind slower buses.

> **Why a hierarchy? Physics and cost:** the faster a bus, the shorter it must be (so little room to plug things in), and high-performance buses are expensive. Slow devices on a peripheral bus also allow *many* devices per bus.

Modern machines keep the idea but use point-to-point links (e.g., PCIe/NVMe); the OS still needs a **driver-level protocol per device class.**

## A.3 The canonical device: interface vs. internals
```
        OS sees →  [ Interface: status / command / data registers ]
                   [ Internals: firmware, controller CPU, memory, media ]
```
The OS usually talks only to the **interface.**

| Register | Role |
|---|---|
| **Status** | read by the OS: busy, ready, or error |
| **Command** | written by the OS: which operation to perform |
| **Data** | passes data to/from the device (a word at a time, or via a buffer) |

> A device protocol is just a **disciplined sequence of reads and writes to these registers.**

## A.4 The canonical protocol, and the cost of polling
```
while (STATUS == BUSY) ;            // 1. wait until ready
write data to DATA register          // 2. move data (programmed I/O)
write command to COMMAND register    // 3. issue the command
while (STATUS == BUSY) ;            // 4. wait until complete
```
**Polling** = the CPU repeatedly asks "done?" Simple, but a slow device means many wasted cycles (timeline: `1 1 1 1 1 p p p p p 1 1 1 1`, where `p` is the polling loop). Same "spinning wastes the CPU" lesson as spinlocks in your Locks guide.

## A.5 Interrupts: overlap I/O with computation
```
RUNNING → (issue I/O, cannot continue) → SLEEPING → scheduler runs another process
       → device finishes, raises interrupt → handler completes request, wakes sleeper → RUNNABLE
```
**Scheduling and I/O are not separate topics:** this is exactly the `RUNNING → SLEEPING → RUNNABLE` state machine and the overlap trick from your CPU Scheduling guide (Part 4), and `sleep`/`wakeup` from your Condition Variables guide.

### Interrupts are not always better than polling
| Situation | Often better | Reason |
|---|---|---|
| Very fast device | **Polling** | interrupt handling may cost more than waiting |
| Slow device | **Interrupts** | CPU runs other work meanwhile |
| Unknown / mixed latency | **Hybrid** | poll briefly, then block and use interrupts |
| Interrupt storm | Polling / batching | avoid livelock and handler overhead |

*Design lesson: a mechanism is useful only in the regime where its overhead is justified.*

**Interrupt coalescing:** the device waits briefly before interrupting, so several completions are reported with **one** interrupt. Benefit: lower CPU overhead. Cost: higher per-request latency. (The classic **throughput–latency** tradeoff.)

## A.6 Moving data: programmed I/O vs. DMA
- **PIO:** the CPU copies data word by word between memory and the device — timeline `1 1 1 1 1 c c c 2 2 2 2 1 1` (`c` = CPU copying). Necessary work, but not intellectually demanding, and it keeps the CPU on the data path.
- **DMA (direct memory access):** the OS **programs a DMA engine** — memory address, size, direction, device — and moves on. The DMA controller does the transfer, then **raises an interrupt** when done. *DMA turns "CPU copies data" into "CPU describes a transfer."* The CPU can run Process 2 meanwhile.

## A.7 How the OS reaches device registers
| Method | How it looks | Example |
|---|---|---|
| **Explicit I/O instructions** | special privileged instructions name a *port* | x86 `in` / `out` |
| **Memory-mapped I/O (MMIO)** | device registers appear at physical addresses; a load/store is routed to the device | RISC-V/QEMU (Luit) |

No great advantage either way (MMIO needs no new instructions); both are in use. **Protection rule: only privileged kernel code may program device registers** — otherwise any user program could read or write the disk and take over the machine. (Recall from your Page Faults and VM guides that Luit maps MMIO regions into the kernel page table without `PTE_U`.)

## A.8 The device driver: keeping the OS device-neutral
A file system shouldn't contain separate logic for every disk model. **Abstraction** again:
```
Application → POSIX API (open, read, write, close)
           → File system          (logical files and blocks)       ┐ (a "raw" interface also
           → Generic block layer   (routes block read/write)       ┘  exists for fsck, defrag)
           → Specific block interface → Device driver [SCSI, ATA, virtio, …] → Device
```
The driver knows the protocol; upper layers use a stable block abstraction, so **user programs can usually ignore the storage device.**

OSTEP's cautionary facts: encapsulation has a downside (SCSI's rich error reporting collapses into a generic `EIO` for the file system); and drivers are enormous — **over 70% of Linux kernel code is device drivers**, often written by non-kernel specialists, making them a leading cause of kernel crashes.

## A.9 Case study: a simple IDE driver (xv6, OSTEP Fig. 36.5–36.6)
**Registers** (accessed with `in`/`out`): Control (0x3F6), Command block (0x1F0–0x1F7: data, error, sector count, LBA low/mid/high, drive, command/status), a **Status** register (BUSY, READY, FAULT, SEEK, DRQ, CORR, IDDEX, ERROR) and an **Error** register (check when ERROR = 1).

**Protocol:**
1. Wait until the drive is READY and not BUSY (poll status).
2. Write sector count, LBA, and drive number to command registers.
3. Start the I/O: write READ or WRITE to the command register.
4. (Writes) wait for READY + DRQ, then write data to the data port.
5. Handle interrupts — one per sector in the simple case; batching allows one final interrupt.
6. After each operation read status; if ERROR, read the error register.

**The xv6 driver's four functions:** `ide_rw()` queues the request (or issues it directly) and **puts the caller to sleep**; `ide_start_request()` writes the registers; `ide_wait_ready()` polls until ready; `ide_intr()` reads data (for reads), **wakes the sleeper**, and starts the next queued request.

> **The teaching invariant: request queue → sleep → interrupt → wakeup.** Every driver in this course has this shape.

## A.10 Luit's path: virtio-blk instead of IDE
Luit runs on RISC-V/QEMU, so its disk is a **virtio-blk** device: **MMIO + virtqueue + DMA + PLIC interrupt** (versus x86 IDE's port I/O). Conceptual path (the slide stresses it is *schematic* — use exact names from `kernel/virtio_blk.c` when tracing source):
```c
hal_block_rw(blockno, buf, write) {
    acquire(&vblk.lock);
    build request header;
    build descriptors: hdr → data → status;
    publish descriptor in avail ring;
    fence(); kick device;
    while (!req.done)
        sleep(&req, &vblk.lock);
    release(&vblk.lock);
}
virtio_blk_intr() {
    read used ring;
    mark request done;
    wakeup(&req);
}
```
Read it with your earlier guides in hand:
- `acquire(&vblk.lock)` — shared driver state needs a lock (Locks guide).
- `while (!req.done) sleep(&req, &vblk.lock)` — the **predicate loop** around `sleep`, exactly the Mesa-semantics rule from the Condition Variables guide (and `sleep` releases the lock atomically, closing the lost-wakeup gap).
- `fence()` before kicking the device — an **ordering** barrier so the descriptor writes are visible before the device is told to look (the Locks guide's Part 9 ordering idea, applied to a device).
- The interrupt handler `wakeup(&req)`s the sleeper → `RUNNABLE`; the scheduler decides when it runs.

Summary path from process to completion: **Process → system call → file system/block layer → driver → device/DMA → interrupt handler → wakeup → runnable again.**

---

# PART B — Hard Disk Drives (OSTEP Ch. 37)

## B.1 The interface: an array of sectors
To the OS, a disk is **sectors 0 … n−1** (a sector ≈ 512 bytes in the model). Contract:
- Read/write sector *i*, or several consecutive sectors.
- **A single 512-byte write is atomic; larger writes can be torn** if power fails midway. (This is why crash consistency is hard — it returns in RAID-1's consistent-update problem and in later file-system chapters.)
- **The "unwritten contract":** nearby logical blocks are usually physically close and faster to access; **a long sequential transfer is the fastest common pattern**; a small random read pays mechanical cost again and again.

> **Design rule: keep related blocks together and transfer larger chunks.** Good layout → fewer seeks and rotations. File systems exploit this by placing related blocks near each other.

## B.2 Geometry
- **Platter**: circular magnetic surface, spun at a fixed **RPM** by the **spindle**.
- **Track**: a concentric circle on a surface; **sector**: a small addressable region on a track.
- **Head** reads/writes magnetic patterns; the **arm** positions the head over the track.
- A real drive stacks several platters on one spindle, one head per surface; a **cylinder** is the set of tracks at the same radius across all surfaces. *Moving the arm selects a cylinder; rotation selects the sector.*

## B.3 The three components of one request
```
T_I/O = T_seek + T_rotation + T_transfer          R_I/O = transfer size / T_I/O
```
- **Seek** — move the arm to the right track. It has four phases: **acceleration, coasting, deceleration, settling**; settling alone can take ≈ 0.5–2 ms, so even short seeks aren't free, and "nearer track" means "less time" but **not linearly**.
- **Rotation** — wait for the sector to come under the head. Average ≈ **½ × full rotation time** for random requests.
- **Transfer** — bytes pass under the head.

**Seek and rotation dominate small random I/O.**

(OSTEP's aside: the commonly cited **average seek ≈ ⅓ of the full seek** comes from averaging |x − y| over all pairs of tracks — an average *distance*, taken as a proxy for time.)

## B.4 Hardware tricks that protect performance
- **Track skew:** sector numbering is offset between adjacent tracks so that, after the arm finishes moving to the next track, the *next logical block* is just arriving — otherwise a perfectly aligned sector would already have rotated past, costing a nearly full rotation during a sequential read.
- **Multi-zoned drives:** outer tracks have more sectors than inner ones (each *zone* has a constant sectors-per-track).
- **Drive cache / track buffer:** small on-drive memory (typically ~8–16 MB) that can hold sectors recently read from a track. For writes there are two policies:
  - **Write-through:** acknowledge after data reaches the **surface**.
  - **Write-back:** acknowledge after data reaches **drive memory** — faster, but **dangerous if ordering matters and power fails.** *"A completed write may not always mean safely on media."* (This sets up `fsync` and crash consistency in Part E.)

## B.5 The I/O-time math (verified)
**Worked problem 1 — RPM to rotation time.** 10,000 RPM → 60,000 ms/min ÷ 10,000 = **6 ms** per rotation; average rotational delay **3 ms**. *Always convert units first.*

**Worked problem 2 — transfer time.** 512 KB at 100 MB/s: 512 KB = 0.5 MB → 0.5/100 = 0.005 s = **5 ms**.

**Two example disks (OSTEP Fig. 37.5):**
| Property | Cheetah 15K.5 | Barracuda |
|---|---|---|
| Capacity | 300 GB | 1 TB |
| RPM | 15,000 | 7,200 |
| Average seek | 4 ms | 9 ms |
| Max transfer | 125 MB/s | 105 MB/s |
| Interface | SCSI | SATA |

*(Performance disk = fast seek + fast rotation; capacity disk = bigger, cheaper, slower.)*

**Random 4 KB read, Cheetah:** 15,000 RPM → 250 rot/s → 4 ms per rotation → avg 2 ms; transfer 4 KB at 125 MB/s ≈ 0.03 ms.
`T ≈ 4 + 2 + 0.03 ≈ 6.03 ms` → `R ≈ 4 KB / 6 ms ≈ 0.66 MB/s`. **Almost all the time is seek + rotation, not data transfer.**

**Random 4 KB read, 7200 RPM (worked problem 3):** full rotation 60,000/7,200 = 8.33 ms → avg 4.17 ms; transfer 4 KB/105 MB/s ≈ 0.039 ms.
`T ≈ 9 + 4.17 + 0.039 ≈ 13.2 ms` → `R ≈ 0.30 MB/s`.

**Sequential 100 MB (worked problem 4):** one positioning ≈ 9 + 4.17 = 13.17 ms; transfer 100/105 ≈ 952 ms; total ≈ **965 ms** → ≈ **104 MB/s**.

**Random vs. sequential gap:**
| Workload | Cheetah | Barracuda |
|---|---|---|
| Random 4 KB reads | 0.66 MB/s | 0.31 MB/s |
| Sequential transfer | 125 MB/s | 105 MB/s |
| **Gap** | **≈ 190×** | **≈ 340×** |

> **Random I/O repeatedly pays the positioning cost; sequential I/O pays it once, then streams.** This is *why file systems care so much about locality* — and why your Luit labs' block-layout decisions matter.

(I re-ran these numbers while preparing the guide. Tiny differences from the slides — e.g., 0.65 vs. 0.66 MB/s — are just rounding of 4 KB.)

---

# PART C — Disk Scheduling

## C.1 Why it differs from CPU scheduling
| CPU scheduling | Disk scheduling |
|---|---|
| Burst length usually **unknown** (SJF needs prediction) | Request **location is known**, so cost can be **estimated** |
| Preemption at timer interrupts | Reordering requests reduces seek + rotation |

**Goal: approximate SJF for disk I/O** — you *can* estimate each request's cost, so a shortest-job-first idea is actually feasible here (the "no oracle" problem from your CPU Scheduling guide doesn't bite).

## C.2 SSTF and its starvation problem
**SSTF (shortest seek time first):** pick the pending request on the closest track (if geometry is hidden, nearest *logical block first* approximates it). Good when seek dominates. **Problem: starvation** — a steady stream of nearby requests can keep a far-away request waiting indefinitely. (Same fairness problem as SJF/MLFQ, with disk location as the "cost.")

## C.3 SCAN (elevator) and variants
- **SCAN:** sweep one direction serving requests in order, **at the end reverse** and sweep back. A request behind the head waits for the next sweep. Does not reverse just because a nearby request appears.
- **LOOK:** reverse at the **last pending request**, not the physical end.
- **C-SCAN (circular SCAN):** sweep one direction, **jump back** to the start, sweep again in the *same* direction. Gives **more uniform waiting time**. (Correction the slide stresses: C-SCAN does **not** serve requests on the way back.)
- **F-SCAN:** freeze the request set during a sweep; new arrivals wait for a later sweep — prevents late nearby arrivals from endlessly delaying far requests.

## C.4 Worked scheduling problem (all numbers verified by script)
Requests: **98, 183, 37, 122, 14, 124, 65, 67**; head starts at **53**; cylinders 0–199; initial direction **up**.

| Policy | Service order | Total movement |
|---|---|---|
| **FCFS** | 53→98→183→37→122→14→124→65→67 | **640** |
| **SSTF** | 53→65→67→37→14→98→122→124→183 | **236** |
| **LOOK (up)** | 53→65→67→98→122→124→183→37→14 | **299** |
| **SCAN (up)** | 53→…→183→**199**→37→14 | **331** |
| **C-SCAN (up)** | 53→…→183→**199**→**0**→14→37 | **382** |

Checks: FCFS = 45+85+146+85+108+110+59+2 = 640. LOOK = (183−53)+(183−37)+(37−14) = 130+146+23 = 299. SCAN = (199−53)+(199−14) = 146+185 = 331. **C-SCAN's 382 counts the return jump (199) plus the final sweep up to 37:** 146+199+37 = 382 — a counting convention; some texts don't count the return, so state your convention.

SSTF wins on raw movement but **can starve**; SCAN-family trades some movement for bounded waiting; C-SCAN trades more movement for uniform waiting.

## C.5 SPTF / SATF: include rotation
SSTF only asks "shortest *seek*?" **SPTF/SATF** asks "shortest **total positioning time** = seek + rotational wait?" — closer to true SJF.

**Worked SATF decision** (12 sectors per rotation, 6 ms rotation → 0.5 ms/sector):
| Request | Seek | Rotational wait | Total |
|---|---|---|---|
| A (closer track) | 1.0 ms | 9 × 0.5 = 4.5 ms | **5.5 ms** |
| B (farther track) | 2.5 ms | 1 × 0.5 = 0.5 ms | **3.0 ms** |

SSTF picks A; **SATF picks B.** *"Nearest track" is not always "soonest completion."* Which wins depends on the relative cost of seeking vs. rotating (OSTEP's **Livny's Law**: "it depends" — but don't overuse it).

## C.6 Where scheduling happens today; merging and waiting
- **Older view:** the OS keeps a queue and picks each next request; the disk is passive.
- **Modern view:** the **drive/controller** knows head position and layout, so it can do accurate SPTF internally; the OS sends a **window** of requests and still **merges** adjacent ones (blocks 33 and 34 → one two-block request).
- **Work-conserving** policy: issue immediately whenever work exists. **Non-work-conserving** (e.g., *anticipatory scheduling*): sometimes **wait briefly**, because a better nearby request may arrive. Tradeoff: throughput vs. latency (the same shape as interrupt coalescing).

## C.7 Luit/QEMU: what carries over
Under QEMU, **virtio-blk hides mechanical geometry** — the driver sees virtqueues and DMA, not an arm and platter, and the device may reorder internally. What survives: block-device thinking, **locality and sequential access preferences, the buffer cache** (reuse), and **batching/merging**. *Use the HDD as the mental performance model; use virtio-blk as the actual Luit device interface.*

---

# PART D — RAID (OSTEP Ch. 38)

## D.1 Motivation and interface
We want storage that is **faster**, **larger**, and **more reliable** (RAID: Patterson, Gibson, Katz, 1988 — use many disks in concert). To the file system, a RAID is **just a linear array of blocks**; internally it is "a small computer" (microcontroller + firmware, DRAM buffer, often NVRAM, sometimes parity logic). **Each logical I/O becomes one or more physical I/Os** (e.g., a mirrored write = two physical writes).

> **Transparency enables deployment:** because RAID presented the same interface as a single SCSI disk, administrators could swap it in with **no change** to host, OS, or applications. *New functionality that needs no changes to the rest of the system is far easier to deploy.*

## D.2 Fault model: fail-stop
A disk is either **working** (all blocks readable/writable) or **failed** (permanently lost), and the controller **detects failure immediately**. *Not modeled yet:* silent corruption, and **latent sector errors** (one unreadable block on an otherwise working disk). A RAID can only tolerate faults it was designed for, so the fault model comes first.

## D.3 How to evaluate a RAID
Given **N** disks of **B** blocks each, **S** = single-disk sequential bandwidth, **R** = single-disk random bandwidth, **T** = latency of one request to one disk:
- **Capacity:** how many blocks clients can use.
- **Reliability:** how many disk failures tolerated (fail-stop).
- **Performance:** depends on workload — **sequential** (large contiguous, e.g. 1 MB; disk mostly transfers; bandwidth S) vs. **random** (small scattered, e.g. DBMS transactions; disk mostly seeks; bandwidth R). In general **S ≫ R**.

**Computing S and R** (disk: seek 7 ms, rotation 3 ms, 50 MB/s; sequential request 10 MB, random 10 KB):
```
S = 10 MB / (7 + 3 + 200) ms = 10 MB / 210 ms = 47.62 MB/s
R = 10 KB / (7 + 3 + 0.195) ms = 10 KB / 10.195 ms ≈ 0.981 MB/s     (S/R ≈ 50)
```
(OSTEP rounds 10 KB to 0.01 MB here; using 10/1024 MB exactly gives ≈ 0.958 MB/s. The conclusion — S/R is about 50 — is unchanged.)

## D.4 RAID-0: striping
Blocks placed round-robin; **one row = a stripe**. No redundancy — "not really RAID," but the **upper bound** on capacity and performance.

**Chunk size** = blocks placed on one disk before moving on (e.g., 2 blocks × 4 KB = 8 KB chunk; stripe of 4 chunks = 32 KB).
- **Small chunks** → a file spreads over many disks → more parallelism *per file*, but positioning time is the **max over all those disks**.
- **Large chunks** → a small file sits on one disk → one positioning time; throughput then relies on many concurrent requests. Real arrays often use ~64 KB.

**The mapping problem** (integer division; chunk = 1 block): `Disk = A % N`, `Offset = A / N`. Example **A = 14, N = 4 → Disk 2, Offset 3**.
General chunk size **C**: `chunk = A / C`, `Disk = chunk % N`, `Offset = (chunk / N)·C + A % C`. Check: **A = 14, C = 2** → chunk 7 → **Disk 3, Offset (7/4)·2 + 0 = 2**. ✓.

**RAID-0 analysis:** capacity **N·B**; reliability **0** (any failure loses data); latency **T**; sequential **N·S**; random **N·R**.

## D.5 RAID-1: mirroring
Every block has **two copies** on different disks; data is striped across mirror pairs (**RAID-10** = stripe of mirrors; **RAID-01** = mirror of stripes). Read: either copy. Write: **both** copies, issued in parallel.

**The consistent-update problem (aside):** a logical write must update disk 0 *and* disk 1. If power fails after one write completes, the copies **disagree** (new/old). We want atomicity — both or neither. **Fix: write-ahead logging** — log what you're about to do, replay after a crash. Logging to disk on every write is too slow, so RAID hardware logs to a small **non-volatile RAM**. (Directly tied to B.1: only a 512-byte sector write is atomic.)

**RAID-1 analysis:**
| Metric | Value | Why |
|---|---|---|
| Capacity | **(N·B)/2** | two copies |
| Reliability | **1 for certain** (up to N/2 with luck) | losing both disks of a pair loses data (e.g., disks 0 & 1 → blocks 0, 2, 4, 6) |
| Read latency | T | one copy |
| Write latency | ≈ T (slightly more) | wait for the slower of two parallel writes |
| Sequential write | **(N/2)·S** | two physical writes per logical |
| Sequential read | **(N/2)·S** | each disk delivers useful data only every other block (the other copy's blocks rotate past unused) |
| Random read | **N·R** | spread over all copies |
| Random write | **(N/2)·R** | two physical writes per logical |

*Why sequential **read** gets only half the bandwidth:* reading blocks 0–7 spread over all four disks, each disk skips every other block as it rotates, so each delivers useful data at half its peak rate.

## D.6 RAID-4: saving space with parity
One **parity block per stripe** on a dedicated parity disk. Parity function: **XOR** — survives the loss of any one block. Invariant: **every row, including parity, has an even number of 1s.** To recover a lost column, XOR the survivors (e.g., survivors 0, 0, 1 and parity 0 → XOR = 1 = the missing bit). For real blocks, XOR bit by bit (a 4 KB block = 4096 × 8 = 32,768 bit-wise XORs).
Space cost: **one disk** instead of half.

**Analysis:** capacity **(N−1)·B**; reliability **exactly 1**; sequential read **(N−1)·S** (parity disk idle); **full-stripe write** (blocks 0–3 together → compute parity → write all five disks in parallel) → sequential write **(N−1)·S**; random read **(N−1)·R**.

### The small-write problem
Overwriting one block makes its parity stale. Two ways to update:
| | Steps | I/Os |
|---|---|---|
| **Additive parity** | read the *other* data blocks, XOR with the new block → new parity; write new block + new parity | reads grow with disk count |
| **Subtractive parity** | read old data **C_old** and old parity **P_old**; `P_new = (C_old ⊕ C_new) ⊕ P_old`; write C_new and P_new | **always 4** (2 reads + 2 writes) |

Intuition for subtractive: if `C_new = C_old`, the parity bit doesn't change; if different, **flip** the parity bit.

*Slide's "think" question — my derivation:* with **N total disks** (N−1 data + 1 parity), additive parity reads **N−2** other data blocks and writes 2; subtractive does 2 reads and 2 writes. Additive needs fewer I/Os only when N−2 < 2, i.e. **N = 3** (they tie at N = 4; subtractive wins beyond).

**The bottleneck:** every small write costs the parity disk **2 I/Os** (read + write), and **all parity blocks live on one disk**, so concurrent small writes serialize there: random-write throughput of the whole array is **R/2 and doesn't improve as you add disks.** Latency: read T; write ≈ 2T (two reads in parallel, then two writes in parallel).

## D.7 RAID-5: rotating parity
Same as RAID-4, except **the parity block of each stripe sits on a different disk** (P0 on disk 4, P1 on disk 3, P2 on disk 2, …; "left-symmetric" placement). Parity updates spread over all disks — **no single parity disk.**

**Analysis:** capacity **(N−1)·B**, tolerates **1** failure, sequential **(N−1)·S**, latency T (read) / 2T (write) — same as RAID-4. **Better:** random read **N·R** (every disk holds data), random write **(N/4)·R** (writes to different blocks touch different disks and proceed in parallel; the factor 4 is the 4 physical I/Os per small write). **RAID-5 has almost replaced RAID-4**; RAID-4 survives only where every write is large and the simpler design helps.

## D.8 Comparison (N = 4; S = 47.62, R = 0.981 MB/s — all re-computed)
| Level | Usable capacity | Failures tolerated | Seq. read | Seq. write | Rand. read | Rand. write |
|---|---|---|---|---|---|---|
| RAID-0 | 4 disks | 0 | 190.5 | 190.5 | 3.92 | 3.92 |
| RAID-1 | 2 disks | 1 (2 if lucky) | 95.2 | 95.2 | 3.92 | 1.96 |
| RAID-4 | 3 disks | 1 | 142.9 | 142.9 | 2.94 | 0.49 |
| RAID-5 | 3 disks | 1 | 142.9 | 142.9 | 3.92 | 0.98 |

**Which should I use?**
| You mainly want… | Choose | You pay with |
|---|---|---|
| Performance only | RAID-0 | any failure loses data |
| Random I/O performance + reliability | RAID-1 | half the capacity |
| Capacity + reliability | RAID-5 | small-write performance |
| Sequential I/O only, max capacity | RAID-5 | (small writes don't occur) |

The analysis is simplified (a mirrored write really pays the worse of two seeks; a parity update's write needs only rotation after its read has positioned the head). **Picking a level and its parameters (chunk size, disk count) remains "more art than science."**

**Beyond this chapter:** RAID-2/3 (original taxonomy), RAID-6 (tolerates more than one failure), hot spares and degraded/reconstruction performance, latent sector errors and corruption (data-integrity chapter), and software RAID (cheaper, but must solve consistent update itself).

---

# PART E — Files and Directories: The UNIX API (OSTEP Ch. 39)

## E.1 The third virtualization
CPU → processes; memory → address spaces; **storage → files and directories** (a tree of named, persistent objects). Unlike memory, storage must **survive power loss and reboot.** Crux: *how should the OS manage a persistent device — what are the APIs and the important implementation aspects?*

## E.2 Two abstractions and three names
- **File:** a linear array of bytes; its low-level name is the **inode number (i-number)**. The OS doesn't interpret the bytes.
- **Directory:** also has an i-number; its contents are **(human-readable name, i-number) pairs.** A directory is *a file whose data is a list of (name, inode) pairs.*
- **Pathname:** names from the root `/` down (`/foo/bar.txt`). The same filename may occur in different directories (`/foo/bar.txt` vs. `/bar/foo/bar.txt`).

**Tip: think carefully about naming.** In UNIX almost everything is named through the file system — files, devices (`/dev/sda`), pipes, even processes (`/proc/1234/`) — giving one uniform, modular scheme.

| Name | Used by | Good for |
|---|---|---|
| **inode number** | the file system | unique, fixed-size, fast to index |
| **path** | people and programs | memorable, hierarchical |
| **file descriptor** | one process | cheap repeated access; tracks an offset |

## E.3 Designing the API in three attempts
| | Attempt 1 | Attempt 2 | Attempt 3 |
|---|---|---|---|
| Name in each call | inode number | path | **file descriptor** |
| Easy for people | no | yes | yes (path only at `open`) |
| Hierarchy | no | yes | yes |
| Path traversals | none | **every call** | **once, at `open`** |
| Offset kept by | unclear (two processes seek one inode: one offset or two?) | the caller | **the descriptor** |

Attempt 2's cost: reading a 1 MB file 4 KB at a time would walk the same path **256 times**. **Attempt 3 is the UNIX API:** `open()` walks the path *once*, stores the inode in an in-memory descriptor object that tracks the offset, and `read`/`write` use the descriptor.

**Traversal** (example: read `/hello`): read the root inode, read its data to find `hello → inode 0`, read inode 0, read its data. **Every directory on the path adds reads** — `/a/b/c/d` costs more than `/hello`.

## E.4 `open()` and file descriptors
```c
int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
```
`O_CREAT` create if missing; `O_WRONLY` write only; `O_TRUNC` truncate an existing file to 0; the last argument is the permission mode. (Aside: `creat("foo")` = `open` with `O_CREAT|O_WRONLY|O_TRUNC`; Ken Thompson, asked what he'd change about UNIX: *"I'd spell creat with an e."*)

A **file descriptor is a small integer, private to a process** — an index into a per-process array (`struct file *ofile[NOFILE]` in xv6). It's a **capability**: holding it lets you read/write that file; think of it as a pointer to an object of type "file" whose methods are `read`/`write`.

**Standard descriptors:** 0 = stdin, 1 = stdout, 2 = stderr — which is why a program's first `open()` usually returns **3**.

**`strace cat foo`** shows the whole thing: `open("foo", O_RDONLY) = 3`, `read(3, "hello\n", 4096) = 6`, `write(1, "hello\n", 6) = 6`, `read(3, "", 4096) = 0`, `close(3) = 0`. **A `read` returning 0 means end-of-file, not an error.** (Tip: use `strace` and similar tools.)

## E.5 The open-file table: fd → entry → inode
```
per-process fd table  →  system-wide open file table  →  inode / file object
   (index = fd)            (ref count, offset, mode)       (persistent metadata → data blocks)
```
xv6's version:
```c
struct file { int ref; char readable; char writable; struct inode *ip; uint off; };
```
The **descriptor** indexes a per-process array; the **open file table entry** stores the **current offset** and access mode (and a reference count); the **inode** stores persistent metadata and points to the data blocks. **Correct mental model: fd → open-file state → underlying file.**

### Offsets
- Starts at 0 on `open`; a read/write of N bytes adds N; reading past the end returns 0.
| Call | Returns | Offset |
|---|---|---|
| `fd = open("file", O_RDONLY)` | 3 | 0 |
| `read(fd, buf, 100)` | 100 | 100 |
| `read(fd, buf, 100)` | 100 | 200 |
| `read(fd, buf, 100)` | 100 | 300 |
| `read(fd, buf, 100)` | **0** | 300 |
| `close(fd)` | 0 | — |

- **Opening the same file twice** gives **two table entries → independent offsets** (reading 100 bytes through fd1 leaves fd2 at 0).
- **`lseek(fd, off, whence)`** (`SEEK_SET`: = off; `SEEK_CUR`: += off; `SEEK_END`: = size + off) **does not perform a disk seek** — it only changes a variable in OS memory; a *later* read/write may cause a real seek, depending on file system and cache state.

## E.6 Sharing entries: `fork()` and `dup()`
- **`fork()`** — parent and child **share the open-file-table entry**, so they share the offset. OSTEP's `fork-seek` example: the child does `lseek(fd, 10, SEEK_SET)`; after `wait`, the parent's `lseek(fd, 0, SEEK_CUR)` prints **10**.
- **`dup(fd)`** — a new descriptor for the **same** open-file-table entry; both descriptors **share one offset**. **`dup2(old, new)`** picks the new number — the basis of **shell redirection**: `dup2(fd, 1)` makes stdout point at `out.txt`, so `printf` goes to the file. (This is what your shell lab's redirection and pipes relied on.)

**Practice (slide) — answers:**
```c
int fd1 = open("file.txt", O_RDONLY); // 3   (own table entry A)
int fd2 = open("file.txt", O_RDONLY); // 4   (own table entry B)
int fd3 = dup(fd2);                   // 5   (shares entry B with fd2)
read(fd1, buf, 10);  read(fd2, buf, 10);  read(fd3, buf, 10);  lseek(fd1, 100, SEEK_SET);
```
There are **2 open-file-table entries** (A for fd1; B shared by fd2 and fd3). **fd1's offset = 100** (read 10, then `lseek` to 100). **fd2 and fd3 share entry B:** `read(fd2,10)` → 10, then `read(fd3,10)` → 20, so **both report 20**.

## E.7 Durability: `fsync()` and crash-safe update
`write()` usually just tells the file system "store this *eventually*." The FS **buffers writes in memory** (say 5–30 s) for batching, better scheduling, and because writes soon overwritten never reach disk — at the **risk** that a crash before the flush loses data.
- **`fsync(fd)`** forces the file's dirty data to persistent storage and returns when done (databases need this for recovery). **Subtle:** for a *newly created* file, durable naming may also require **`fsync()` on the containing directory**, or the name itself may be lost.

**`rename()` and atomic file update:** `mv foo bar` calls `rename("foo","bar")` — changes the name, not the data, even across directories — and is **atomic with respect to crashes:** afterward the file has the old name or the new one, never a half-renamed state. The editor recipe:
```c
fd = open("foo.txt.tmp", O_WRONLY|O_CREAT|O_TRUNC, 0644);
write(fd, new_contents, size);
fsync(fd);                       // force the new version to disk
close(fd);
rename("foo.txt.tmp", "foo.txt");  // atomically swap it in
```
After a crash you see the **complete old file or the complete new file, never a mixture.** (This is the file-system cousin of the "atomicity" wish from your Concurrency Intro guide.)

**Aside — `mmap()` and persistent memory:** `mmap` maps file byte offsets onto virtual addresses so a program uses ordinary loads/stores on an in-memory image; with `MAP_SHARED` changes reach the file. Same danger: a crash mid-update can leave the file inconsistent. (This is the same `mmap` whose fault handler you studied in the Page Faults guide.)

## E.8 Metadata, `stat()`, and `unlink()`
`stat` shows size, blocks, inode number, **link count**, owner, permissions, timestamps — all stored in the **inode** (also: block locations); active inodes are cached in memory.

**Why is deleting called `unlink()`?** Creating a file does *two* things: (1) make an inode (size, block locations, …); (2) **link a human-readable name** to it in a directory. `rm` calls `unlink(name)` — there is **no syscall that deletes a file directly.** `unlink` removes **one name** and decrements the inode's **link count**; **at zero (and with no process holding it open), the FS frees the inode and its blocks.** *Deleting a file is just performing the last `unlink()` of it.*

## E.9 Directories and links
- **`mkdir`** creates a directory that already contains two entries: **`.`** (itself) and **`..`** (parent). Programs can't write a directory directly; the FS maintains it.
- **`opendir`/`readdir`/`closedir`** return entries (`d_name`, `d_ino`, …) — mostly name → inode; `ls -l` calls `stat()` on every entry to get the rest.
- **`rmdir`** works **only if the directory is empty** (just `.` and `..`) — strictness because one call could otherwise delete huge amounts of data. (Tip: be wary of powerful commands like `rm -rf *`.)

**Hard links** — `link(old, new)` / `ln`: a **new name for the same inode**; nothing is copied; both names are equal, neither is "the original."
| Action | Link count | Data reachable? |
|---|---|---|
| create `file` | 1 | yes |
| `ln file file2` | 2 | yes |
| `rm file` | 1 | yes, via `file2` |
| `rm file2` | 0 | no — inode and blocks freed |

**Symbolic links** — a file of a *third type* whose contents are a **pathname**; `ls -l` shows `l` and `file2 -> file`; size = path length; can **dangle** if the target is removed.
| | Hard link | Symbolic link |
|---|---|---|
| Refers to | an inode number | a path name |
| Separate file? | no, just another directory entry | yes (type "symbolic link") |
| Directories | **not allowed** (cycles) | allowed |
| Across file systems | **not allowed** | allowed |
| Target removed | file survives until last link goes | link **dangles** |

## E.10 Permissions, ACLs, TOCTTOU, mkfs and mount
**Permission bits** (`-rw-r--r--`: type, owner, group, other): **r = 4, w = 2, x = 1** (`chmod 600` → owner `rw-`, nobody else).
| Bit | Regular file | Directory |
|---|---|---|
| r | read contents | list entries |
| w | modify contents | create/delete entries (with x) |
| x | execute | traverse/search (`cd`) |

**ACLs** (e.g., AFS) name exactly who may do what, beyond owner/group/other. **Superuser (root)** can access every file — convenient, and a security risk if an attacker becomes root.

**TOCTTOU (Time Of Check To Time Of Use):** the world changes between a check and the action relying on it. Mail-server example: a root service (1) `lstat()`s the inbox to verify it's a regular file owned by the user; (2) the user quickly renames a symlink to `/etc/passwd` onto the inbox; (3) the server appends mail to `/etc/passwd`. Mitigations: fewer root services; **`O_NOFOLLOW`**; transactional file systems. *Names and directories are shared state — a race-condition bug from your Concurrency guides, now in the file system.*

**`mkfs` and `mount`:** `mkfs` writes an empty file system (starting with a root directory) onto a device; `mount -t ext3 /dev/sda1 /home/users` **pastes it onto the tree at a mount point** — one unified namespace regardless of device or file-system type (disk FS, `/proc`, `/sys`, `tmpfs`, AFS all in one tree).

## E.11 Key terms (OSTEP's own summary)
File = byte array with an i-number; directory = (name, i-number) pairs incl. `.` and `..`; `open` returns a per-process fd pointing to an open-file-table entry holding the offset; `fsync` forces data to disk; `rename` swaps names atomically; hard/symbolic links give multiple names; deleting = last `unlink`; permission bits and ACLs control sharing; `mkfs` + `mount` build the tree.

---

# PART F — Luit and Linux Side by Side

The Files deck's Part B asks you to find each idea in the Luit source. **Caveat on this part:** I have the slides and their code excerpts, not the Luit repository, so the "answers" below come from the excerpts and standard xv6-lineage design. Items marked **(verify in source)** are my reasoning, not confirmed facts.

## F.1 Where to look
| To find… | Luit | Linux |
|---|---|---|
| system calls | `kernel/syscall.tbl`, `docs/ABI.md` | `man 2 open`, `strace` |
| open, read, dup, link, mkdir… | `kernel/sysfile.c` | `strace` a command |
| open file table | `kernel/file.h`, `kernel/file.c` | `ls -l /proc/$$/fd` |
| inodes, directories, paths | `kernel/fs.h`, `kernel/fs.c` | `stat`, `ls -i` |
| limits | `kernel/param.h` | `ulimit -n` |
| on-disk format | `docs/LUITFS.md`, `mkfs/mkfs.c` | `man 5 ext4` |
| user programs | `user/*.c` | coreutils |

## F.2 Luit's structures (from the slides)
```c
struct file {                    // Luit open file table entry
    enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
    int ref;                     // refcount
    char readable, writable;
    struct pipe *pipe;           // FD_PIPE
    struct inode *ip;            // FD_INODE, FD_DEVICE
    uint32 off;                  // byte offset
    short major;                 // devsw index
};
```
Compared with OSTEP's xv6 `struct file`, Luit adds a **`type`** (so a descriptor can be a pipe, an inode, or a **device** — the "everything is a file" uniformity) and a **`major`** for device dispatch. (This matches the UNIX naming tip: devices and pipes ride the same fd interface.)

On disk: `struct dirent { uint32 inum; char name[DIRSIZ]; }` with **`DIRSIZ = 28`** and `inum = 0` meaning a free slot (so a directory entry is 32 bytes and names are ≤ 28 characters); `struct dinode { type, major, minor, nlink; uint32 size; uint32 addrs[NDIRECT + 1]; }`. **LuitFS v1** layout (1 KiB blocks): `boot | super | inode bitmap | data bitmap | inode table | data blocks`, built by `mkfs/mkfs fs.img $(UPROGS)` and attached as the QEMU virtio-blk drive (there is no `mount()`). **Buffer cache: `NBUF` = 30 blocks.** LuitFS v1 is documented as **persistent but not crash-consistent.**

## F.3 The Luit ABI v1 vs. Linux
| OSTEP / Linux | Luit ABI v1 | Difference |
|---|---|---|
| `open`/`read`/`write`/`close` | yes (syscalls 15, 5, 16, 21) | `open(path, omode)`: **no mode argument**; flag is **`O_CREATE`** (0x200), not `O_CREAT`; `O_TRUNC` = 0x400 |
| `lseek` | **no** | offset moves only via `read`/`write` |
| `dup` / `dup2` | `dup` (10) | `close` + `dup` gives the `dup2` effect |
| `fsync` / `rename` / `mmap` | **no** | arrive in **Lab 11** (write-ahead log) / **Lab 9** / **Lab 10** (file-backed, `MAP_SHARED`) |
| `stat`/`fstat` | `fstat` (8) | **5 fields:** dev, ino, type, nlink, size |
| `unlink` / `rmdir` | `unlink` (18) | also removes **empty directories** |
| `mkdir` / `readdir` | `mkdir` (20) | `read()` a directory to list it (that's how `ls` works) |
| `link` / `symlink` | `link` (19) / Lab 9 | same rules: **no directories, same device** |
| `chmod`, ACLs, `mount` | no | no users; one root FS built by `mkfs` |

## F.4 The slide's questions, with answers where the excerpts support them
1. **fds 0, 1, 2.** The slide's diagram shows `init` creating them: `p->ofile[]` entries 0–2 are `FD_DEVICE` with `major = console`, `ref = 3`. The kernel doesn't treat 0/1/2 specially; it's a user-level convention set up by `user/init.c`. So **the first `open()` in a new program returns 3** (lowest free descriptor — `sys_dup` also uses `fdalloc`, "lowest free"). On Linux, 0/1/2 point to `/dev/pts/N`.
2. **Offset location.** The offset lives in **`struct file`** (the open-file-table entry), **not the inode** — because two opens of the same file need independent offsets, while `fork`/`dup` need a *shared* one. `fileread` updates `f->off` **while holding the inode lock** (`ilock(f->ip) … f->off += r … iunlock`), so concurrent readers sharing an entry don't corrupt the offset (the Locks guide's lost-update race, again).
3. **No `lseek`; reaching byte 100.** A program must read and discard 100 bytes. (Adding `lseek` would mean a new row in `syscall.tbl`, a `sys_lseek` in `sysfile.c` that sets `f->off`, a user stub, and an ABI/doc update — **verify in source**.)
4. **No `dup2`; shell redirection `cmd > out`.** Close the target descriptor and then `open`/`dup` so the lowest free fd is the one you want (e.g., `close(1); open("out", …)`) — the `close + dup` effect in the table.
5. **Three counts (`f->ref`, `ip->ref`, `nlink`).** `f->ref`: how many descriptors share this open-file entry (`dup` does `f->ref++`). `ip->ref`: in-memory references to the cached inode. `nlink`: **on-disk directory entries** naming the inode. `fileclose` does `--f->ref`; at zero it frees the table slot and calls `pipeclose` or **`iput(ff.ip)`** (drop the inode reference). Since an open file holds an inode reference, **a program can keep reading an unlinked file** until it closes it, just like Linux — **(verify in source)**.
6. **`sys_link` ordering.** The slide's code increments `ip->nlink` (and `iupdate`) **before** adding the directory entry, with an `undo: nlink--` path on failure. My reasoning **(verify in source)**: if a crash or failure falls between the two steps, a count that is **too high** merely leaks an inode, whereas a count that is **too low** could let the file be freed while a name still points at it — so count first, then link.
7. **`mkdir` link counts.** From the excerpt (`dirlink(ip, ".", …); dirlink(ip, "..", dp->inum); dirlink(dp, name, …); dp->nlink++; /* ".." -> parent */`): **the parent's `nlink` increases by one** because the new directory's `..` points at it. Whether the new directory's own count also counts `.` is **not shown — verify in source.**
8. **`fsync` / `rename` / crash-safe save.** None exist in ABI v1, so **a Luit program cannot yet use the editor recipe.** For Lab 9's `rename`, a safe order **(my reasoning; verify)** is to **add the new directory entry before removing the old one**, so a crash can leave both names (harmless) but never neither.

## F.5 Linux-vs-Luit "try it" and the summary
Same commands, two systems: `echo hello > a; ln a b; ls -i a b; rm a; cat b; mkdir d; ls -a d`. **Predict the link counts first**, then explain each step with the three tables (fd table → open-file table → inode). Luit implements the **core** API; `lseek`, `fsync`, `rename`, symlinks and `mmap` arrive as exercises and labs; the next lecture (OSTEP Ch. 40) opens the file-system implementation on LuitFS.

---

# Part G — Connections, Mix-Ups, and Quick Reference

## G.1 How this connects to earlier guides
| Earlier guide | Connection |
|---|---|
| **CPU Scheduling** | I/O overlap (poll vs. interrupt, `RUNNING→SLEEPING→RUNNABLE`); disk scheduling is SJF with *known* costs; SCAN/starvation parallels MLFQ's fairness fix; non-work-conserving waits echo the throughput–latency tradeoff |
| **Condition Variables / Locks** | the driver's `sleep(&req, &vblk.lock)` loop is the predicate-under-lock pattern; `fence()` is the ordering idea; `ide_lock`/`vblk.lock` protect the request queue |
| **Syscall Entry/Exit** | `write(fd, buf, n)` enters via `ecall`; device interrupts arrive through the same trap machinery as timer interrupts |
| **VM / Page Faults** | MMIO regions are mapped without `PTE_U`; `mmap` is file-backed paging; the buffer cache plays the "caching" role at the block level |
| **Concurrency Intro / Bugs** | `rename` = file-system atomicity; TOCTTOU = a race on shared names; RAID-1's consistent update = the atomicity wish for disks |

## G.2 Common mix-ups
| Mix-up | Correction |
|---|---|
| "`write()` returned, so it's on disk" | Usually only in the buffer cache; **`fsync`** forces it (and a new file may need a **directory** `fsync` too) |
| "`lseek` moves the disk head" | It only changes an in-memory offset; a later I/O may seek |
| "The offset is stored in the inode" | It's in the **open-file-table entry** (per open, shared by `fork`/`dup`) |
| "`rm` deletes the file" | It **unlinks one name**; data is freed at link count 0 (and no open references) |
| "A hard link is a copy" | Same inode, new directory entry |
| "SSTF is best" | Best movement, but **starvation**; SCAN family bounds waiting |
| "C-SCAN serves requests on the way back" | No — it **jumps back** and sweeps the same direction |
| "Nearest track = soonest completion" | Not once **rotation** matters (SATF) |
| "RAID-1 reads get full bandwidth" | Sequential reads get **N/2·S** with the naive pattern |
| "RAID-4 and RAID-5 differ in capacity" | Same capacity and fault tolerance; RAID-5 **rotates parity** to remove the small-write bottleneck |
| "Polling is always worse" | For very fast devices it can beat interrupts |

## G.3 Formula sheet
```
T_I/O = T_seek + T_rotation + T_transfer          R_I/O = size / T_I/O
rotation time (ms) = 60,000 / RPM                 avg rotational delay = ½ × rotation time
transfer time = size / bandwidth                  (convert units first!)
RAID-0:  cap N·B   rel 0     seq N·S        rand N·R
RAID-1:  cap NB/2  rel 1     seq (N/2)·S    rand-read N·R   rand-write (N/2)·R
RAID-4:  cap (N−1)B rel 1    seq (N−1)·S    rand-read (N−1)·R   rand-write R/2
RAID-5:  cap (N−1)B rel 1    seq (N−1)·S    rand-read N·R       rand-write (N/4)·R
RAID-0 map (chunk C): chunk=A/C; disk=chunk%N; offset=(chunk/N)·C + A%C
subtractive parity: P_new = (C_old ⊕ C_new) ⊕ P_old     (always 2 reads + 2 writes)
```

## G.4 Vocabulary
| Term | One-line meaning |
|---|---|
| **MMIO / port I/O** | device registers as memory addresses / via special `in`/`out` instructions |
| **PIO / DMA** | CPU copies data / a DMA engine copies and interrupts on completion |
| **Interrupt coalescing** | batch several completions into one interrupt (↓ overhead, ↑ latency) |
| **Device driver** | code that encapsulates one device's protocol behind a stable block interface |
| **Sector / track / cylinder** | addressable unit / ring on a platter / same-radius tracks across surfaces |
| **Track skew** | offset sector numbering so sequential reads survive a track switch |
| **SSTF / SCAN / LOOK / C-SCAN / F-SCAN / SATF** | disk-scheduling policies (Part C) |
| **Work-conserving** | never idles the disk while requests wait |
| **Stripe / chunk** | row of blocks across disks / blocks placed on one disk before moving on |
| **Parity (XOR)** | redundancy that lets any one lost block be rebuilt |
| **inode / i-number** | file metadata record / its index |
| **File descriptor / open-file-table entry** | per-process handle / shared state with the offset |
| **Hard / symbolic link** | extra name for an inode / a file containing a path |
| **`fsync` / `rename`** | force data to disk / atomic name replacement |
| **TOCTTOU** | check-then-use race on names |
| **`mkfs` / `mount`** | create a file system / attach it into the tree |

## G.5 Board questions
- Why is a 4 KB random read ~190× slower than sequential bandwidth on the Cheetah disk? *(Positioning ≈ 6 ms vs. 0.03 ms of transfer.)*
- When does SATF beat SSTF? *(When the nearer track needs a long rotational wait — the 5.5 ms vs. 3.0 ms example.)*
- Why can't RAID-4's random-write throughput grow with more disks? *(All parity updates serialize on one parity disk: R/2.)*
- Two descriptors, one offset — when? *(After `dup`, or across `fork`; separate `open`s get separate offsets.)*
- Why is the editor save pattern crash-safe? *(Write temp, `fsync`, then atomic `rename`: old-or-new, never a mix.)*
- **Next lecture's board question (from the deck): if a file is a byte array, where are those bytes placed on disk?**


  # File System Implementation, FFS, and Crash Consistency — Complete Concept Guide

This guide merges two CS3106 decks that continue directly from your *Persistence I* guide:

- **File System Implementation** — OSTEP Ch. 40, **vsfs** (the Very Simple File System), plus Luit/Linux questions.
- **FFS, fsck and Journaling** — OSTEP Ch. 41 (**Locality and the Fast File System**) and Ch. 42 (**Crash Consistency: fsck and journaling**), plus Luit/Linux questions.

Last time the board question was *"if a file is a byte array, where are those bytes placed on disk?"* That is where this starts. The whole arc in three questions:

| Chapter | Question | Answer in one line |
|---|---|---|
| **Ch. 40 (vsfs)** | What is on the disk, and which structures does each call touch? | Superblock, bitmaps, inode table, data region; inodes find data through a multi-level index |
| **Ch. 41 (FFS)** | Where on the disk should each block go? | Treat the disk like a disk: groups, and place related things together |
| **Ch. 42 (crash consistency)** | What if power fails in the middle of an update? | Write a note first (journaling), or repair afterwards (fsck) |

A note on sources: I read both decks in full except the **seven journaling "walk-through" slides** (Ch. 42, slides 55–58, 61, 63, 65), whose text came out garbled from the PDF. I describe those from the protocol logic and the surrounding slides (flagged where it matters). The decks say the answers to the Luit/Linux questions are in the speaker notes, which I don't have, so those answers are my own reasoning and are marked **(verify in source)**.

---

# PART A — vsfs: Implementing a Simple File System (OSTEP Ch. 40)

## A.1 The way to think about it
> **A file system is pure software.** Unlike CPU and memory virtualization, we add no hardware features — but we still pay attention to device characteristics so it performs well. There is huge design freedom (from AFS to ZFS).

Every file system has two aspects:
1. **Data structures** — what on-disk structures organize the data and metadata? (vsfs: simple arrays of blocks. SGI XFS: tree-based structures.)
2. **Access methods** — how do `open()`, `read()`, `write()` map onto those structures? Which are read, which are written, how efficiently?

**Data structures + access methods = a good mental model of the file system.** The slide asks three questions to answer by the end: *What on-disk structures store data and metadata? What happens when a process opens a file? Which structures are touched by a read or write?*

## A.2 Overall organization: the vsfs layout
**Step 1 — blocks.** Use one block size (4 KB). The partition is blocks 0 … N−1. The tiny example disk: **N = 64 blocks = 256 KB**.

**Step 2 — regions.** Most of the disk should be user data, so the data region goes at the far end; the rest holds metadata:

| Block(s) | Symbol | Contents |
|---|---|---|
| 0 | **S** | **superblock** — describes this whole file system |
| 1 | **i** | **inode bitmap** — which inodes are free/in use |
| 2 | **d** | **data bitmap** — which data blocks are free/in use |
| 3–7 | **I** | **inode table** — 5 blocks of inodes |
| 8–63 | **D** | **data region** — 56 blocks of user data |

```
 S  i  d  I  I  I  I  I  D  D  D ... D
 0  1  2  3  4  5  6  7  8  9 10 ... 63
```
(*Metadata* = any information in the file system that is not pure user data.)

**How many files? (verified)** An inode is small (128 or 256 bytes; assume 256). One 4 KB block holds 4096/256 = **16 inodes**; five inode blocks hold 5 × 16 = **80 inodes = the maximum number of files.** A bigger disk gets a bigger inode table and so more files.

**Allocation structures — bitmaps.** Track free vs. allocated with either a *free list* (each free block points to the next) or a **bitmap**: one bit per object, 0 = free, 1 = in use. vsfs uses **two** bitmaps. Example inode bitmap `1 1 1 0 1 0 0 0 …` → inodes 0, 1, 2, 4 allocated; 3 and 5 onward free. (A whole 4 KB bitmap block is overkill — 4096 × 8 = 32K objects versus 80 inodes and 56 data blocks — kept for simplicity. This 32 K figure comes back in ext4, Part D.)

**The superblock** holds information about *this* file system: number of inodes (80) and data blocks (56), where the inode table begins (block 3), and a **magic number** identifying the file-system type. At mount time the OS **reads the superblock first**, initializes its parameters, attaches the volume to the tree — and from then on knows where every on-disk structure is.

## A.3 File organization: the inode
**inode** = *index node*, the historical UNIX term (and probably older, Multics). It holds one file's **metadata**: type and size, number of blocks, protection (owner, access), times (created/modified/accessed), and **where its data blocks are**. Each inode has an **i-number**, the file's low-level name; inodes live in an array and the i-number indexes into it. (Other systems call them dnodes, fnodes, …)

**From i-number to disk location — the formula (verified):**
```c
blk    = (inumber * sizeof(inode_t)) / blockSize;
sector = ((blk * blockSize) + inodeStartAddr) / sectorSize;
```
vsfs: `sizeof(inode_t)` = 256, `blockSize` = 4096, `inodeStartAddr` = 12288 (12 KB), `sectorSize` = 512 (disks aren't byte-addressable).

| i-number | `blk` | `sector` | Check |
|---|---|---|---|
| **32** | 32·256/4096 = **2** | (2·4096 + 12288)/512 = **40** | offset 8 KB + 12 KB = 20 KB; 20·1024/512 = sector 40 = start of iblock 2 (inodes 32–47) |
| **17** | 4352/4096 = **1** (integer division) | (4096 + 12288)/512 = **32** | |
| **79** | 20224/4096 = **4** | (16384 + 12288)/512 = **56** | the last inode |

*Given an i-number, the inode's location is pure arithmetic* — no search needed. (The Luit slide asks you to compare this with Luit's `IBLOCK(i, sb)` macro — Part D.)

## A.4 The multi-level index: how an inode finds its data
**Direct pointers.** Simplest: the inode holds disk addresses of data blocks. 12 direct pointers × 4 KB = **48 KB** — a bigger file is out of luck.

**Indirect pointer.** A special pointer to a block *of pointers*, allocated from the data region only when the file outgrows the direct pointers. A 4 KB block holds 4096/4 = **1024** 4-byte pointers.

**Double / triple indirect.** A pointer to a block of pointers to indirect blocks (and one more level for triple).

**How big can a file get? (verified)** 4 KB blocks, 4-byte pointers, 12 direct:
| Pointers in the inode | Data blocks reachable | Max file size |
|---|---|---|
| 12 direct | 12 | **48 KB** |
| + single indirect | 12 + 1024 = 1,036 | **4,144 KB (≈ 4 MB)** |
| + double indirect | 12 + 1024 + 1024² = 1,049,612 | **just over 4 GB** (4,198,448 KB ≈ 4.004 GiB) |
| + triple indirect | 1,074,791,436 | **≈ 4 TB** |

ext2's **15 pointers** are exactly this: 12 direct + 1 single + 1 double + 1 triple. The structure is an **imbalanced tree** — the **multi-level index**.

**Why imbalanced? Optimize for the common case.** Decades of measurements (Agrawal et al., FAST '07) find the same "truths": **most files are small** (so 12 direct pointers reach 48 KB with no indirect block at all), while **most bytes live in large files** (so the indirect levels handle those). *The inode is just a data structure: if workloads or technology change, be willing to change its design.*

**Practice — which pointer reaches offset X?** (file block = offset / 4096, rounded down, counting from 0; verified)
- Offset **40,000** → block 9 → **direct pointer 9**.
- Offset **100,000** → block 24 → 24 − 12 = 12 → **entry 12 of the single indirect block**.
- Offset **5,000,000** → block 1220 → 1220 − 1036 = 184 → **double indirect: entry 0, then entry 184 of that indirect block.** Disk reads if only the inode is in memory: double-indirect block + indirect block + data block = **3**.

**Alternatives to pointers:**
- **Extents:** a disk pointer **plus a length in blocks**. A 6-block file in blocks 20–23 and 40–41 = two extents `(20,4) (40,2)` instead of 6 pointers. Pointers = most flexible but lots of metadata; extents = less flexible but compact, good when files are contiguous. **Multi-level index:** ext2, ext3, WAFL, original UNIX FS. **Extents:** SGI XFS, Linux ext4.
- **Linked allocation / FAT:** the inode keeps one pointer to the file's first block, and each block points to the next. Bad for random access (walk the chain to reach the last block). Fix: keep the `next` pointers in an in-memory table indexed by block address — the **File Allocation Table** of classic Windows FAT. No inodes: directory entries hold the metadata and first block, so **hard links are impossible**.

## A.5 Directory organization
A directory is **a special type of file**: its inode has type *directory*, and its entries live in **data blocks** of the data region — so vsfs's on-disk layout doesn't change. Each entry is **(name, inode number)**; every directory also has `.` (itself) and `..` (parent).

Example directory `dir` (inode 5):
| inum | reclen | strlen | name |
|---|---|---|---|
| 5 | 12 | 2 | `.` |
| 2 | 12 | 3 | `..` (the root) |
| 12 | 12 | 4 | `foo` |
| 13 | 12 | 4 | `bar` |
| 24 | 36 | 28 | `foobar_is_a_pretty_longname` |

`inum` = the entry's inode; **`strlen`** = actual name length (the book counts the `\0`, so `foo` → 4); **`reclen`** = record length = total bytes for the name **plus any left-over space**. That is why both exist: after `unlink()`, the freed slot is marked with a **reserved inode number such as 0** and left in place; a new entry may **reuse an older, bigger record** (`new` has `strlen 4` but keeps `reclen 36`).

A linear list isn't the only choice: creating a file must first check the name isn't already used, which on a list means scanning the whole directory; **XFS stores directories as B-trees**, making create faster — a time–space tradeoff.

## A.6 Free-space management
**Allocating an inode** for `creat("/foo/bar")`: search the inode bitmap for the first 0 (say bit 4 → inode 4), mark it used (0 → 1), eventually write the bitmap block to disk. Data blocks work the same way with the data bitmap.

**Allocation policy:** ext2/ext3 look for a run of free blocks (say 8) for a new file's data — **pre-allocation** — guaranteeing part of the file is contiguous, which improves performance. **Other structures:** a **free list** (early file systems: superblock points to the first free block, which points to the next); XFS uses a B-tree to represent free chunks compactly.

## A.7 Access paths: which structures does each call touch?
Setting: file system mounted (**superblock in memory**; everything else on disk). File `/foo/bar`, 12 KB = 3 blocks.

### `open("/foo/bar")`: traversing the pathname
Traversal starts at the **root**, which has no parent, so its i-number must be **well known — 2 in most UNIX file systems.** Read root's inode → read root's data to find `foo` → read foo's inode → read foo's data to find `bar` → read **bar's inode** → permission check → allocate a descriptor in the per-process open-file table. **`open()` I/O is proportional to path length** (inode + data per directory), and large directories make it worse.

### `read()` (cold cache, per the slides' Figure 40.3 model)
Each `read()`: **read the inode** (where is this block?) → **read the data block** → **write the inode** (update last-access time). Updating the in-memory open-file offset costs no disk I/O; `close()` costs none either.

> **Aside: reads don't access allocation structures.** A common confusion is that reading must check the bitmap. **Not true** — bitmaps are touched *only to allocate*; the inode already says where every block is.

### `write()` that allocates: 5 I/Os (logically)
1. read the data bitmap (find a free block) 2. write the data bitmap (mark it used) 3. read the inode 4. write the inode (new block's location) 5. write the data block.

### `creat("/foo/bar")`: 10 I/Os
After the path walk (4 reads: root inode, root data, foo inode, foo data): read inode bitmap, write inode bitmap, write directory data (add `(bar, i-number)`), read inode's block, write the initialized inode, write foo's inode — **10 in all**. If the directory must grow: more I/O to the data bitmap and the new directory block.

**Counting (verified): create + three allocating writes = 10 + 3 × 5 = 25 I/Os.**
*Puzzle: why read bar's inode before initializing it?* An inode (256 B) sits inside a 4 KB block with 15 others, so the file system does a **read-modify-write of the whole block.**

### Practice — count the I/Os (cold cache, one-block directories)
- `open("/a/b/c.txt")` → root inode, root data, a inode, a data, b inode, b data, c.txt inode = **7 reads**.
- Then read the first two blocks: 2 × (inode read + data read + inode write) = **6** → 13 I/Os total; **none touch a bitmap.**
- `creat("/a/b/d.txt")` → 6 path reads + the same 6 create I/Os = **12**.

> **The crux: even the simplest operations incur a huge number of I/Os scattered over the disk. How can the file system reduce them?** → caching and buffering.

## A.8 Caching and buffering
- **Caching reads:** without a cache, `open("/1/2/…/100/file.txt")` needs hundreds of reads. Early systems used a **fixed-size cache** (≈ 10% of memory, allocated at boot, LRU or variants). With a cache, the first open does lots of I/O; reopening the same file (or one in the same directory) mostly hits.
- **Static vs. dynamic partitioning:** *static* = predictable and simpler, but unused cache pages can't be repurposed; *dynamic* = a **unified page cache** where the boundary between file pages and virtual-memory pages moves with demand — better utilization, but more complex, and reclaiming a resource can be slow. *No best method.*
- **Write buffering:** reads can be eliminated by a big cache, but writes must eventually reach disk. Delaying them still helps: **batch** (two creates update the inode bitmap once, not twice), **schedule** (order buffered writes), **avoid** (create then delete → maybe no write at all). Most systems buffer writes **5 to 30 seconds.** *Laziness is a virtue.*
- **The durability/performance tradeoff:** a crash before buffered updates reach disk loses them. Tolerable for some data, not for others (a bank transaction). Applications that can't accept it call **`fsync()`**, use **direct I/O**, or use the **raw disk interface**.

*Homework:* `vsfs.py` simulates a tiny file system with `mkdir`, `creat`, `open/write/close`, `link`, `unlink`, showing inode bitmap, inodes (`[f a:1 r:1]` = type, address of its one block, ref count), data bitmap and data (root is inode 0 there).

---

# PART B — FFS: Locality and the Fast File System (OSTEP Ch. 41)

vsfs says *what's on disk*. It ignores two questions: **performance** (where should each block go?) → FFS; **reliability** (what if power fails mid-update?) → Part C.

## B.1 The old UNIX file system, and why it was slow
Ken Thompson's first UNIX file system: **super block** (volume size, number of inodes, pointer to the head of a *free list* of blocks), then the **inode region**, then data. Simple, and it supported the basic abstractions — but performance was terrible and *got worse over time*: by one measurement it delivered **only 2% of overall disk bandwidth.**

Why: **it treated the disk like memory.**
- Blocks placed with **no regard to positioning cost** (seek + rotation) — a file's data was often far from its inode.
- **Free space wasn't carefully managed** → **fragmentation.** Example: four 2-block files A, B, C, D; delete B and D; allocate E (4 blocks) → `A1 A2 E1 E2 C1 C2 E3 E4`. **E isn't contiguous**, so reading it sequentially needs a seek in the middle. (Hence **defragmentation** tools.)
- **Small blocks (512 B):** little *internal* fragmentation, but data moves 512 bytes at a time and each block may pay a seek and rotation — the useful work is a sliver of the time. (This is Part B of the HDD lesson: positioning dominates small transfers.)

## B.2 FFS: disk awareness is the solution
A Berkeley group (McKusick, Joy, Leffler, Fabry — *"A Fast File System for UNIX,"* 1984) built FFS on one idea: **keep the interface (`open`/`read`/`write`/`close`) unchanged; change the implementation to be disk aware** — structures and allocation policies. This started a new era: file systems keep the API and compete on internals.

## B.3 The cylinder group / block group
A **cylinder** = the tracks at the same distance from the center on all surfaces. FFS groups N consecutive cylinders into a **cylinder group**; two blocks in the same group need only a short seek.

**Modern drives hide geometry** (they export a logical block address space), so Linux **ext2/ext3/ext4** use **block groups** — each a consecutive range of block addresses. Groups are *how FFS improves locality*: place two files in the same group and reading one after the other needs no long seek. FFS must therefore (1) **place files and directories into groups** and (2) **track everything about them inside each group.**

**Inside a group:** `S | ib | db | Inodes | Data` —
1. **S:** a **copy of the superblock in every group**, so if one copy is corrupted you can mount using a working replica.
2. **ib, db:** per-group inode and data bitmaps (a bitmap makes it easy to find large chunks of free space — no fragmented free list).
3. **Inodes, Data:** as in vsfs; most of each group is data.

**What does creating `/foo/bar.txt` (one block) write?** Inode bitmap, the new inode, data bitmap, the data block (≥ four writes in this group), plus the parent directory `foo`'s data (add the entry; maybe a new block and its bitmap) and foo's inode (new length, mtime).

## B.4 Placement policies
> **Keep related stuff together — and its corollary, keep unrelated stuff far apart.**

- **Directories:** pick a group with **a low number of allocated directories** (balance directories across groups) **and a high number of free inodes** (room for that directory's files later). Toy table: group 1 has few directories but is almost full → skip it; **group 2** (1 dir, 40 free inodes) is chosen.
- **Files:** Rule 1 — allocate a file's **data blocks in the same group as its inode** (avoiding exactly the old FS's inode→data seek). Rule 2 — put **all files of a directory in that directory's group** (files in one directory are often used together).

**Worked example** (each group 10 inodes, 10 data blocks; files 2 blocks; directories 1 block; create `/`, `/a`, `/b`, `/a/c`, `/a/d`, `/a/e`, `/b/f`): `/` gets group 0; `/a` and `/b` go to groups with few directories (groups 1 and 2); `c, d, e` follow `a` into group 1 with data next to inodes; `f` follows `b` into group 2. **Reading `/a/c`, `/a/d`, `/a/e` touches one group.**
**Compare a "spread inodes" policy:** each file's inode still sits next to its data (good) but `/a/c`, `/a/d`, `/a/e` now span three groups — **name-based locality is lost.**

**Is "namespace locality" real?** FFS's heuristics are common sense, checked against SEER traces. Metric: *path distance* between consecutive opens = how far up the tree to the common ancestor (`foo.c` then `foo.c` → 0; `src/foo.c` then `src/bar.c` → 1; `proj/src/foo.c` then `proj/obj/foo.o` → 2). Findings (slide's reading of Figure 41.1): about **7%** of opens were to the same file as the previous open; about **40%** were to the same file or one in the same directory (distance 0 or 1); about **25%** were at distance 2. FFS's assumption holds for these traces, but **distance-2 locality isn't captured by FFS's policies** — more seeking for that pattern. (A random trace shows less locality, but not zero: everything shares the root.)

## B.5 The large-file exception
**Problem:** with groups of 10 inodes and 40 data blocks, a 30-block file `/a` placed entirely next to its inode **fills most of its group**, so other files of the same directory can't be placed there — file-access locality suffers.
**Fix:** put the first **chunk** in the inode's group, the next chunk in **another** group (perhaps a low-utilization one), and so on (e.g., 5 blocks per chunk). Each group keeps most of its space free for others.

**But does spreading hurt performance?** Between chunks you pay a seek + rotation, so make chunks **large enough that transfer time dominates — amortization** (verified; positioning 10 ms, peak 40 MB/s):
```
f = T_transfer / (T_transfer + T_position)       chunk needed = f/(1−f) × 10 ms × 40 MB/s
```
| Fraction of peak | Chunk needed |
|---|---|
| 50% | 409.6 KB |
| 80% | 4 × 409.6 ≈ **1.6 MB** (the slide's "your turn") |
| 90% | ≈ 3.6 MB |
| 99% | ≈ 39.6 MB |

*The closer to peak you want, the bigger the chunk — and it grows fast.*

**What FFS actually did:** the first **12 direct blocks** go in the inode's group; **each indirect block and all the blocks it points to go in a different group.** With 4 KB blocks and 32-bit addresses, an indirect block holds 1024 pointers → a new group every **4 MB.** Only the first 48 KB shares the inode's group. 4 MB at 40 MB/s = 100 ms of transfer per 10 ms of positioning → **≈ 91% of peak** in the model (100/110 = 0.909). **Trend:** transfer rates improve much faster than seek times, so chunks must keep growing to stay amortized.

## B.6 A few other things FFS did
- **Sub-blocks (512 B):** 4 KB blocks are good for transfer, but many files were ≈ 2 KB back then → internal fragmentation. A 1 KB file uses 2 sub-blocks; as it grows keep adding sub-blocks; when it reaches 4 KB, **copy into one full block** and free the sub-blocks. The copy is extra I/O, so FFS changed **libc to buffer writes and issue them in 4 KB chunks**, avoiding the path in most cases.
- **Parameterized placement:** reading block 0 then 1 on a standard layout misses block 1 (it has already rotated past) and waits a full rotation. FFS **skips one block** between consecutive blocks, tuned to the disk. Cost: only **50%** of peak bandwidth (each track passed twice). Modern disks' **track buffers** cache the whole track, making this unnecessary.
- **Usability:** long file names (instead of e.g. 8 characters), **symbolic links** (hard links can't point to directories — fear of loops — and only work within one volume), and **atomic `rename()`**. *These probably won FFS its users: usability matters as much as speed.*

**FFS summary:** *"Treat the disk like it's a disk."* Groups (S copy, bitmaps, inodes, data); directories spread by load, files near their inode and directory; large files spread in amortized chunks. Same API, new internals; **Linux ext2/ext3/ext4 are direct descendants.**

---

# PART C — Crash Consistency: fsck and Journaling (OSTEP Ch. 42)

## C.1 The problem
File-system structures are **persistent** (on disk) but power is unreliable. An operation often updates **two or more structures**; the disk services **one write at a time**, so a crash can fall *between* them: `A(old) B(old) → A(new) B(old) ✗`. **Crux: how do we update the disk despite crashes, so that after reboot the on-disk image is in a reasonable state?** Three families of answers: **fsck** (let inconsistencies happen, fix at next boot), **journaling** (write a note first, update in place, replay the note after a crash), and **others** (Soft Updates, copy-on-write, backpointers, optimistic crash consistency).

## C.2 A worked example: appending one block
Tiny file system: 8 inodes, 8 data blocks; one file (inode 2, one data block at 4). Appending one block changes **three** blocks:
- **I[v2]** — the inode: size 2, pointers 4 and 5.
- **B[v2]** — the data bitmap, now `00001100`.
- **Db** — the new data block at block 5.

They sit dirty in the cache and go to disk as **three separate writes** (after, say, 5–30 s). What if only some succeed?

**Only one write reaches disk**
| Reached disk | What the disk says | Result |
|---|---|---|
| **Db only** | data in block 5, no inode points to it, bitmap says free | **Consistent** — as if the write never happened (the user lost data) |
| **I[v2] only** | inode points to block 5 holding **old/garbage contents**; bitmap says free | **Garbage read, and inconsistent** (inode says used, bitmap says free) |
| **B[v2] only** | block 5 marked used, no inode points to it | **Space leak** — can never be used; inconsistent |

**Two writes reach disk**
| Reached disk | Result |
|---|---|
| **I[v2] + B[v2]** | metadata consistent, but **block 5 holds garbage** (never written) — *everything looks consistent, yet the file is wrong* |
| **I[v2] + Db** | inode points to the right data, but bitmap says free → **inconsistent**; block 5 could be given away again |
| **B[v2] + Db** | bitmap and data exist but no inode points to the block → **inconsistent** (whose block is it?) |

**The crash-consistency problem:** we want the three writes to be **atomic** — consistent-before → consistent-after — but the disk commits one write at a time. Possible damage: **inconsistent structures, space leaks, garbage data returned to a user.** (This is the file-system version of RAID-1's consistent-update problem and the "atomicity" wish from your concurrency guides.)

## C.3 Solution #1: fsck
The *lazy* approach: let inconsistencies happen, then fix them at reboot. `fsck` runs **before the file system is mounted** (nothing else touches it) and makes the **metadata internally consistent.** It can't solve everything: for **I[v2] + B[v2]**, the metadata agrees and *the inode points to garbage* — fsck sees nothing wrong.

**What fsck checks:**
- **Superblock:** sane (e.g., file-system size ≥ blocks allocated)? If not, use an **alternate copy** (FFS's per-group copies).
- **Free blocks:** scan all inodes, indirect, double-indirect blocks to learn which blocks are in use; **rebuild the bitmaps from that — when bitmaps and inodes disagree, trust the inodes.** Same for the inode bitmap.
- **Inode state:** each allocated inode has a valid type (file, directory, symlink…); if not, clear it and update the bitmap.
- **Inode links:** walk the whole directory tree from the root, count references to each inode, **fix the stored link count**; an allocated inode with no directory entry → move to **`lost+found`**.
- **Duplicates:** two inodes pointing to the same block → clear one if obviously bad, or give each its own copy.
- **Bad blocks:** a pointer outside the valid range → remove it.
- **Directories:** `.` and `..` first; every entry names an allocated inode; no directory linked more than once.

**On our example:** *I[v2] only* → inode 2 points to 4 and 5 but the bitmap says `00001000` → trust the inode, **mark block 5 used** (fsck can't know block 5 is garbage). *B[v2] only* → bitmap says `00001100` but no inode uses block 5 → **mark it free**; the leak is gone. Either way fsck must **read every inode**.

**fsck's real problem: it's too slow.** It scans the entire disk (minutes to hours for large disks and RAID volumes) — yet only **three** blocks changed. *Like dropping your keys on the bedroom floor and then searching the whole house, starting in the basement.*

## C.4 Solution #2: journaling (write-ahead logging)
An idea from database systems. **Before overwriting structures in place, first write a note to a well-known place on disk — "here is what I am about to do."** Crash during the update? Read the note and try again. **Recovery reads the log, not the whole disk:** a little extra work per update, much less work at recovery. Used by Linux **ext3/ext4**, ReiserFS, IBM JFS, SGI XFS, Windows NTFS. In ext3 the layout is ext2's plus a **journal** (same file system, or a separate device): `Super | Journal | Group 0 | … | Group N`.

### Data journaling: the transaction
Before writing I[v2], B[v2], Db in place, write them into the journal:
```
Journal:  TxB | I[v2] | B[v2] | Db | TxE
```
- **TxB (transaction begin):** the **final addresses** of the following blocks and a **transaction ID (TID)**.
- **Middle blocks:** the **exact contents** of the blocks — **physical logging.** (Alternative: **logical logging**, e.g. "append Db to file X" — smaller log, more complex.)
- **TxE (transaction end):** marks the end; also carries the TID.

Then **checkpoint**: write I[v2], B[v2], Db to their **final locations** — safe to redo after any crash, since the transaction is in the log.

### Crash while writing the journal → commit the TxE last
Issuing all five writes at once risks the disk **reordering** them: e.g., `TxB, I[v2], B[v2], TxE` land but **Db** hasn't (✗ crash). The journal now looks like a **valid** transaction; recovery replays it and **copies garbage into block 5** — worse if the garbage lands on a critical block (the superblock → the file system may not mount).
**Fix:** write everything **except TxE** first (these may go together), **wait for completion**, *then* write **TxE.** A disk guarantees a **512-byte write is atomic**, so make TxE one 512-byte block → **the commit is atomic.**

> **Protocol: 1. Journal write → 2. Journal commit (TxE) → 3. Checkpoint → 4. Free.**

**Aside — forcing writes to disk:** "wait for completion" is harder than it sounds. With **write caching** the disk says "done" when data is in its memory cache, not on the platter, so "B completed after A" says nothing about durability. Fixes: disable write buffering, or use **write barriers** (every write before the barrier is on disk before any after). Some disks **ignore barriers to look faster** — "the fast almost always beats out the slow."

**Aside — optimizing with a checksum:** waiting before writing TxE costs time (usually an extra rotation). Put a **checksum of the transaction contents** in TxB and TxE and write **everything at once**; at recovery recompute it — **mismatch → the transaction wasn't fully written → discard it.** Faster commits and detection of corrupted log contents; adopted in **Linux ext4.**

### Recovery
- **Crash before the commit is on disk** → transaction incomplete → **skip it.** The update is lost, but the file system is consistent.
- **Crash after the commit, before/during checkpoint** → at boot, scan the log for committed transactions and **replay them in order.** This is **redo logging**; replaying blocks that were already checkpointed is **harmless** (the same contents are written twice).

### Making it practical
- **Batching:** creating `file1` and `file2` in one directory modifies the *same* four blocks (inode bitmap, inode block, directory data, directory inode); logging each create separately logs them twice. ext3 marks blocks dirty and adds them to **one global transaction**, committing after a timeout (e.g., 5 s).
- **Circular log:** otherwise recovery slows and the log fills. Once a transaction is checkpointed, its space can be reused; the **journal superblock** records the oldest and newest not-yet-checkpointed transactions. New step: **4. Free.**

### Data journaling: complete protocol and cost
1. **Journal write** (TxB + contents; wait) 2. **Journal commit** (TxE; wait → committed) 3. **Checkpoint** (contents to final locations) 4. **Free** (mark the transaction free in the journal superblock).
**Cost:** **every data block is written twice** (log + in place), plus seeks between journal and data. Data is a big share of write traffic → expensive. *Can we journal less?*

### The four walk-through scenarios (slides 55–58)
*(These slides extracted unreadably, so this is from the protocol, not their text.)* Using the append example with the log starting at block 100:
1. **Normal run:** the three blocks are written to the journal (TxB, I[v2], B[v2], Db copies, then TxE), committed, checkpointed to their homes, and finally freed.
2. **Crash before the commit:** the log has no TxE → recovery **skips** it; disk shows the **old version, consistent** (update lost).
3. **Crash during the checkpoint:** TxE is on disk → recovery scans the log, finds the committed transaction, and **replays** it, writing blocks to their homes again → **consistent new version.**
4. **Why TxE must be last:** if TxE could reach disk before the data copy, a crash would make a *partial* transaction look committed and recovery would replay garbage (the failure shown in "write the commit block last").

## C.5 Metadata (ordered) journaling
**Journal only the metadata; write the user data directly to its final location:** `Journal: TxB | I[v2] | B[v2] | TxE`, and `Db` goes straight to block 5. *When should Db be written?* If **after** the commit and a crash comes first, I[v2] is replayed and **points to garbage.** So write the data **first**, before the metadata commit.

> **Rule: write the pointed-to object before the object that points to it.** This is at the core of crash consistency.

**Protocol:** 1. **Data write** (to its final location; wait) 2. **Journal metadata write** (TxB + metadata; wait) 3. **Journal commit** (TxE; wait) 4. **Checkpoint metadata** 5. **Free.** Steps 1 and 2 may be issued concurrently; both must complete before step 3. Only metadata is written twice; **data is written once.**
**ext3 offers three modes:** **data** (full data journaling), **ordered** (this protocol — the most common choice), **unordered** (data may be written at any time). NTFS and XFS also use a form of metadata journaling.

### A tricky case: block reuse
Directory contents are metadata, so they're logged. (1) Directory `foo` (data block **1000**) is updated and logged. (2) The user deletes everything in `foo`, and `foo` itself — block 1000 is free. (3) The user creates file `bar`, which **reuses block 1000**; only bar's inode is logged and bar's *data* is written in place (not logged). **✗ Crash, then replay:** `D[foo]` is written over block 1000 → **`bar` now contains old directory entries.**
**Fixes:** *Option 1* — never reuse a block until the delete that freed it has been checkpointed out of the journal. *Option 2 (Linux ext3)* — write a **revoke record** when the directory is deleted; **recovery scans for revoke records first, and revoked block images are never replayed.** Linux's **jbd2** has a revoke block type for exactly this.

## C.6 Other approaches and the comparison
| Approach | Idea |
|---|---|
| **Soft Updates** | Order every write carefully so on-disk structures are never inconsistent (data before its inode) — needs intricate knowledge of each structure → complex |
| **Copy-on-write (e.g., ZFS)** | Never overwrite in place; write to unused locations, then flip the root pointer. LFS (next chapter) is an early example |
| **Backpointer-based consistency** | No enforced ordering; every block points back to its owner; agreeing forward and backward pointers mean the access is consistent |
| **Optimistic crash consistency** | Issue as many writes as possible; use transaction checksums to detect inconsistency; up to an order of magnitude faster for some workloads |

| Approach | What happens | Recovery reads | Extra writes |
|---|---|---|---|
| **fsck** | repair metadata after the crash | **the whole disk** | none |
| **Data journaling** | log metadata *and* data, then checkpoint | the log | **all data written twice** |
| **Ordered metadata journaling** | data first, then log metadata | the log | **metadata written twice** |
| **Copy-on-write** | never overwrite; flip the root | the latest root | new copies of changed blocks |

fsck guarantees nothing about **data**; data journaling gives the strongest guarantee here at the highest cost.
**Summary:** a crash between writes can leave the disk inconsistent, leaking space, or pointing at garbage. fsck's recovery is **O(size of the disk volume)** — too slow today; journaling's is **O(size of the log).** Details that matter: **atomic commit block, barriers, checksums, batching, circular log, revoke records.** *Next: Ch. 43, the log-structured file system — what if the whole file system were a log?*

---

# PART D — Luit and Linux: Where Are These Ideas?

**Caveat:** I have the questions, not the speaker-note answers or the Luit source. Items marked **(verify in source)** are my reasoning from the slide excerpts and your earlier guides (e.g., Persistence I's LuitFS facts: 1 KiB blocks; layout `boot | super | inode bitmap | data bitmap | inode table | data blocks`; `DIRSIZ = 28`; `NBUF = 30`; v1 "persistent but not crash-consistent").

## D.1 LuitFS on disk (Ch. 40 questions)
| Question | What the material supports |
|---|---|
| Block size vs. vsfs's 4 KB | LuitFS v1 uses **1 KiB** blocks (Persistence I guide) |
| Regions in order; which does vsfs not have? | `boot, super, inode bitmap, data bitmap, inode table, data blocks`; the slide also shows `sb.inodestart = sb.dbmapstart + ndbmap; sb.datastart = sb.inodestart + ninodeblocks; sb.nlog = 0`. The extra region is **boot (block 0)**, and a **log region** is *reserved* (`nlog`) but empty in v1 |
| `nlog`: what is it for, why 0? | the **journal size**; **0 in v1** because there is no journal — the slide says **"Lab 11 claims a journal here"** |
| Superblock fields giving region starts | the `…start` fields above (`inodestart`, `datastart`, bitmap starts); magic number — **verify in `kernel/fs.h`** |
| `struct dinode` size / `IPB` | fields: `type, major, minor, nlink` (4 × 16-bit = 8 B), `uint32 size`, `uint32 addrs[NDIRECT+1]`. **If `NDIRECT = 12`** (as in xv6) that's 8 + 4 + 13·4 = **64 B**, so **IPB = 1024/64 = 16** **(verify)** |
| Pointers / max file size | `addrs[NDIRECT + 1]` = `NDIRECT` direct pointers + **one single-indirect** slot. With 1 KiB blocks, `NINDIRECT = 1024/4 = 256`, so `MAXFILE = 12 + 256 = 268` blocks = **268 KiB** (if `NDIRECT = 12`) **(verify)** — versus vsfs's ≈ 4 GB with a double-indirect level (Part A.4) |
| `IBLOCK(i, sb)` vs. OSTEP's formula | the same idea: the inode's block is `inodestart + i / IPB` — arithmetic, no search (Part A.3) |
| Root inode; why inode 0 unused? | the Files deck lists `ROOTINO`; a directory entry with `inum = 0` means **free slot** (Persistence I guide), so **0 can't be a valid i-number** — this is the same reserved-number trick as vsfs's "0 = deleted" **(verify the value of ROOTINO)** |
| `struct dirent` | `uint32 inum + char name[28]` = **32 B → 32 entries per 1 KiB block**; longest name 28 chars |
| Fixed-size entries: what's given up/gained? | **gives up** vsfs's `reclen`/`strlen` flexibility (variable-length names, reusing bigger records); **gains** simplicity — finding entry *k* and scanning are trivial, and a free slot is just `inum = 0` |
| Inode allocation vs. xv6 | LuitFS uses an **explicit inode bitmap** (as vsfs does); xv6 instead scans the inode table for a free type **(verify)** |
| Why does mkfs mark metadata blocks `0 … datastart−1` used? | otherwise the **data allocator could hand out the superblock, bitmaps, or inode table as file data**, corrupting the file system |
| Buffer cache: static or dynamic? Size? | **static** — a fixed array, **`NBUF = 30` blocks** (≈ 30 KiB) — the "fixed-size cache allocated at boot" model (Part A.8). Replacement policy: check `kernel/buf.h` (xv6 lineage uses LRU) **(verify)** |

## D.2 Linux (ext4) counterparts (Ch. 40)
```
ls -i file;  stat file;  stat -c %i /;  df -i;  sudo dumpe2fs -h /dev/sdXN | grep -iE 'inode count|block size|inode size';  filefrag -v bigfile
free -h;  time cat bigfile > /dev/null (twice);  cat /proc/sys/vm/dirty_expire_centisecs;  cat /proc/sys/vm/dirty_writeback_centisecs
```
- The **root's inode number** on ext4 is **2** — matching "2 in most UNIX file systems."
- `df -i` reports **inodes used/free** — on ext4 the inode count is **fixed at `mkfs` time**, which tells you the inode table is a pre-sized array (as in vsfs).
- A freshly copied large file typically has **few extents** — extents (Part A.4) are compact for contiguous files.
- The **second `cat` is faster** because the file is in the **page cache**; Linux uses **dynamic partitioning** (a unified page cache that shrinks/grows with demand). The `dirty_*` values are in **centiseconds** — e.g., 3000 = **30 s** expiry, 500 = **5 s** writeback — matching the book's "5 to 30 seconds" **(read the actual values on your machine)**.
- A database makes **`fsync()`** after `write()` to be safe.

## D.3 FFS and journaling in LuitFS and Linux (Ch. 41–42 questions)
**B1 — LuitFS's layout / groups.** LuitFS has **no block groups**: one global inode bitmap, one data bitmap, one inode table, one data region — so **it lacks FFS's per-group structure** (and per-group superblock copies). Whether its allocator searches near the file's inode is something to read in `kernel/fs.c` **(verify)**; given a *global* data bitmap, the natural implementation is a first-free scan from the start of the data region. On QEMU's **virtio-blk**, a "cylinder" means nothing (geometry is hidden), but **block groups would still buy locality** — keeping a file's inode, data, and directory neighbours close in the logical address space, which matters if the host device or its image maps nearby blocks to cheaper accesses.

**B2 — block groups on ext4 (`dumpe2fs`).** 
- **Blocks per group = 32,768** for 4 KB blocks because **one block-bitmap block holds 4096 × 8 = 32,768 bits** (one bit per block in the group) — the same "bitmap block" arithmetic from Part A.2. That's FFS's *cylinder/block group.*
- **Why only groups 0, 1, 3 have a superblock copy:** ext2/3/4's **sparse_super** feature puts backups only in groups 0, 1 and groups that are **powers of 3, 5 and 7** (0, 1, 3, 5, 7, 9, 25, 27, 49, …) — a compromise on FFS's "copy in every group" (less space, still replicas).
- On a **4 GB image** you'd see more groups (≈ 4 GB / 128 MiB per group = 32) and backups in more of them.

**B3 — directory placement in Linux.** `t1/x`, `t1/y`, `t1/sub` land in group 0 (**files and subdirectories near their parent** — the FFS *file* rule), while `t2` lands in group 1 (a new top-level directory **spread to a different group** — the FFS *directory* rule). Why do group 1's inode table (585–1096) and `t2`'s data (2133) lie inside group 0's block range? ext4's **`flex_bg`** feature packs several groups' bitmaps and inode tables together in the first group of a "flex group" — a layout optimization on top of FFS ideas. The directory-placement function is in `fs/ext4/ialloc.c` (find the one that picks a group for a new directory).

**B4 — `e2fsck`.** Its passes map onto Part C.3's checks: **Pass 1** inodes, blocks, sizes (inode state, duplicates, bad blocks); **Pass 2** directory structure; **Pass 3** directory connectivity (reachability, `lost+found`); **Pass 4** **reference counts** (inode links — "Inode 14 ref count is 5, should be 1"); **Pass 5** group summary / **bitmaps** (free blocks). The line **`Block bitmap differences: +2129`** says block 2129 is **in use by an inode but marked free in the bitmap**, so e2fsck marks it used: **it trusts the inode over the bitmap** — the rule *"when bitmaps and inodes disagree, trust the inodes."* After `unlink proj/src/b.c` then `e2fsck -fy`, the now-unreferenced inode is reattached under **`lost+found`** (named by its inode number) — the "allocated inode with no directory entry" rule.

**B5 — the ext4 journal after a "crash."** In `logdump`, the **descriptor block** corresponds to OSTEP's **TxB** — it lists `FS block N logged at journal block M`, i.e., **the final (home) address of each logged block**, which is what recovery needs to replay it — and the **commit block** corresponds to **TxE** (both carry the sequence/transaction number). Blocks that are bitmaps or inode-table blocks look like **B[v2]** and **I[v2]**; identify them with `dumpe2fs`/`debugfs imap`. File `f`'s data ("hi", block 33792) is **not** in the journal → only **metadata** was journaled → this shows **ordered (metadata) journaling**.

**B6 — ext4 journaling modes.**
| Mount option | OSTEP equivalent |
|---|---|
| `data=journal` | **data journaling** (Figure 42.1) |
| `data=ordered` (default) | **ordered/metadata journaling** (Figure 42.2) |
| `data=writeback` | **unordered** mode (data written at any time) |
| `commit=5` | the **batching timeout** (commit the global transaction every 5 s) |
| `barrier` | **write barriers** (ordering of journal writes vs. the commit block) |

`jbd2.h` defines the journal block types (`JBD2_DESCRIPTOR_BLOCK`, `JBD2_COMMIT_BLOCK`, …); **`JBD2_REVOKE_BLOCK` implements the block-reuse fix.** Choosing a mode — my reasoning: a database that calls `fsync()` itself might choose `ordered` or even `writeback` (it manages its own ordering and durability), a laptop the default `ordered` (good safety/performance balance); `data=journal` for the strongest data guarantee at roughly double the write cost.

**B7 — reasoning about LuitFS crashes (no journal; `sb.nlog = 0`).** LuitFS v1 updates blocks in place, so it is "in the same position as the old file systems before journaling." For file creation, list the writes from `kernel/fs.c` in order **(verify the actual order)** — likely: **inode bitmap, new inode, directory data (add the entry), parent directory inode.** Reading each *prefix* with Part C.2's table:
| Crash after… | Outcome |
|---|---|
| inode bitmap only | **space leak** (an inode marked used but unreferenced) |
| inode only (bitmap not yet set) | **inconsistent** — the inode could be given to someone else |
| directory entry before the inode is initialized | the name points at a **garbage inode** |

Is there an order that **never** lets a name point at garbage? **Yes** — apply the rule **"write the pointed-to object before the object that points to it":** mark/initialize the **inode first**, add the **directory entry last**; the worst case is then a **leaked inode** that an fsck-like pass can reclaim. A **write-ahead log** in LuitFS would need all four protocol steps — **journal write, journal commit, checkpoint, free** — with the **commit (the atomic TxE) written only after the log blocks are durable**, and recovery replaying committed transactions at boot; the journal would live in the `nlog` region that **Lab 11** claims.

---

# PART E — Worked Problems (answers verified by script)

1. **Inode location.** vsfs, 256 B inodes, 4 KB blocks, inode table at 12 KB, 512 B sectors. Inode **50**? → `blk = 50·256/4096 = 3`; `sector = (3·4096 + 12288)/512 = 48`. *(Check: iblock 3 holds inodes 48–63 ✓.)*
2. **Max file size** with 1 KiB blocks, 4-byte pointers, 12 direct + 1 single indirect (LuitFS-like): 256 pointers per indirect block → (12 + 256) × 1 KiB = **268 KiB.**
3. **Which pointer?** 4 KB blocks, offset **60,000** → block 14 → 14 − 12 = **entry 2 of the single indirect block** (2 reads: indirect block + data block, inode in memory).
4. **I/O count.** `open("/x/y.txt")` cold, one-block directories: root inode, root data, x inode, x data, y.txt inode = **5 reads.** Then one `read()` = 3 more → **8 I/Os.** Bitmaps touched: **0.**
5. **Creating + writing.** `creat` in `/foo` = 10 I/Os; two allocating writes = 2 × 5 = **20 I/Os total**; each allocating write touches the data bitmap (read+write) and the inode (read+write) plus the data block.
6. **FFS amortization.** To reach **90%** of a 40 MB/s peak with 10 ms positioning: chunk = 9 × 0.4 MB = **3.6 MB**; for **50%**: 0.4 MB (409.6 KB).
7. **Crash scenarios.** After a crash only **B[v2]** reached disk → **space leak** (block marked used, no owner); fsck **marks it free** (no inode uses it). Only **I[v2]** → **garbage read + inconsistent**; fsck marks block 5 used (trust the inode) but can't know the data is garbage.
8. **Journal protocol.** Crash occurs **after TxE is durable but before the checkpoint**: recovery **replays** the transaction (redo) → new version. Crash **before TxE**: transaction **skipped** → old version, consistent.

---

# PART F — Mix-Ups, Formula Sheet, Vocabulary, Connections

## F.1 Common mix-ups
| Mix-up | Correction |
|---|---|
| "Reading a file must check the bitmap" | **No** — bitmaps are touched only to **allocate**; the inode already points to every block |
| "`open()` costs a constant" | Its I/O grows with **path length** (inode + data per directory) |
| "Indirect blocks live in the inode" | They're **allocated from the data region** on demand |
| "More direct pointers is always better" | Optimized for the *common case* (small files); big files use indirect levels |
| "FFS changed the API" | **No** — same interface, disk-aware internals |
| "FFS puts all of a big file in one group" | **No** — the large-file exception spreads it in **amortized chunks** |
| "Cylinders exist on modern disks" | Geometry is hidden; ext2/3/4 use **block groups** |
| "fsck makes the data correct" | It only makes **metadata consistent** (I[v2] + B[v2] leaves garbage data unnoticed) |
| "Journaling means every write goes to disk twice" | Only in **data journaling**; ordered journaling writes **metadata** twice and **data once** |
| "Write TxE with the rest" | **TxE must be written last** (or protected by a checksum), or a partial transaction may look committed |
| "Replaying twice is dangerous" | Replaying already-checkpointed blocks is **harmless** (idempotent redo) |
| "Disk completion = durable" | With write caching, "done" may mean *in the drive's cache* — hence **barriers** |
| "Revoke records are optional" | Without them, **block reuse** can let replay overwrite new data with stale metadata |

## F.2 Formula sheet
```
inodes per block = blockSize / inodeSize          (4096/256 = 16)
inode block      = (inum · inodeSize) / blockSize
inode sector     = ((blk · blockSize) + inodeStartAddr) / sectorSize
pointers per indirect block = blockSize / pointerSize       (4096/4 = 1024)
max file = (D + P + P² + P³) · blockSize      (D direct, P pointers per indirect block)
file block = offset / blockSize ;  indirect entry = block − D ;  double: (block − D − P) / P , % P
allocating write = 5 I/Os (read+write data bitmap, read+write inode, write data)
create  = path-walk reads + 6 I/Os ; /foo/bar total = 10
amortized chunk = f/(1−f) × (T_position × transfer rate)
journal protocol (data):    journal write → commit (TxE) → checkpoint → free
journal protocol (ordered): data write ∥ journal metadata → commit → checkpoint metadata → free
```

## F.3 Vocabulary
| Term | One-line meaning |
|---|---|
| **vsfs** | the Very Simple File System: superblock, inode/data bitmaps, inode table, data region |
| **Superblock** | block describing the whole file system (counts, region starts, magic number) |
| **Inode / i-number** | per-file metadata record / its index in the inode table |
| **Multi-level index** | direct + indirect (+ double/triple) pointers: an imbalanced tree |
| **Extent** | disk pointer + length (contiguous run of blocks) |
| **FAT** | linked allocation with an in-memory next-pointer table (no inodes) |
| **`reclen` / `strlen`** | directory record length / actual name length |
| **Cylinder / block group** | FFS's locality unit (consecutive cylinders / consecutive block addresses) |
| **Namespace locality** | files in one directory tend to be used together |
| **Amortization** | make chunks big enough that transfer time dominates positioning |
| **Sub-block / parameterized placement** | FFS fixes for small files / rotational misses |
| **Crash consistency** | keeping on-disk structures sensible across power loss |
| **fsck** | scan-and-repair checker run before mount; O(disk) |
| **Journal / write-ahead log** | log of intended updates written before updating in place |
| **TxB / TxE** | transaction begin (final addresses + TID) / end (atomic commit block) |
| **Physical vs. logical logging** | log block contents / log the operation |
| **Checkpoint** | write logged blocks to their final locations |
| **Redo logging** | replay committed transactions after a crash |
| **Ordered journaling** | data first, then journal metadata (data written once) |
| **Revoke record** | prevents replay of a block that was freed and reused |
| **Soft updates / COW / backpointers / optimistic** | alternative crash-consistency approaches |

## F.4 Connections to earlier guides
| Earlier guide | Connection |
|---|---|
| **Persistence I (disks, RAID, file API)** | The inode, directory, link count, and offset ideas from the API now have concrete on-disk homes; the HDD time model explains FFS's amortization; **RAID-1's consistent-update problem and write-ahead logging** reappear as journaling |
| **CPU Scheduling / disk scheduling** | Write buffering (batch/schedule/avoid) feeds the disk scheduler; non-work-conserving waits resemble delayed commits |
| **Locks / Condition Variables / Concurrency Bugs** | An in-memory file-system cache is shared state; **atomicity** is the same wish (the commit block is the file-system "atomic instruction"); order violations ↔ write ordering ("pointed-to before pointing-to") |
| **Page Faults / mmap** | The page cache unifies file pages with virtual memory (dynamic partitioning); `mmap` reads file blocks through the same inode → block mapping |
| **Syscall guide** | `open`/`read`/`write` enter the kernel through `ecall`, then walk these structures |

## F.5 Board questions
- Why does `open("/a/b/c")` need 7 reads while a `read` needs no bitmap access? *(One inode + one data block per directory level; bitmaps only for allocation.)*
- Why is the inode tree imbalanced? *(Most files are small; most bytes are in large files.)*
- What exactly does FFS change, and what does it leave alone? *(Changes placement/structures; leaves the API.)*
- Why must the commit block be written last, and why is it one 512-byte block? *(So a partial transaction never looks committed; a 512-byte write is atomic.)*
- What does ordered journaling guarantee that fsck doesn't? *(Data is on disk before the metadata that points to it, so no inode points to garbage.)*
- **Next lecture:** OSTEP Ch. 43 — the **log-structured file system**: what if the whole file system were a log?
