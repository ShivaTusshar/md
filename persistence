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
