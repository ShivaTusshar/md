# Luit OS Labs — Viva Preparation Handbook


> This is a speaking guide. Text in block quotes is a natural answer you can say; the surrounding notes are what lets you defend it when the examiner probes.

## Scope and reconstruction note

I reconstructed the intended work from the supplied starter code, TODO markers, specifications, tests, and surrounding Luit kernel code. There are four unique items:

| Item | Topic | Expected work | Core files | Viva priority |
|---|---|---|---|---|
| Lab 1 | User programs, processes, pipes, syscall ABI | Four user-side implementations | user/sleep.c, user/pingpong.c, user/pstree.c, tools/syscallmap.py | 🔴 |
| Lab 2 | Kernel tracing | Per-hart ring tracer | kernel/trace.c, kernel/syscall.c, user/trace.c | 🔴 |
| Lab 3 | Shared kernel–user page | Per-process read-only page plus seqlock | kernel/usysinfo.c, user/ulib.c | 🔴 |
| Page-fault practicum | VM diagnosis | Observe faults and one temporary repair | user/pfdemo.c, kernel/trap.c, kernel/vm.c | 🟠 |

The Labs 2–3 combined distribution and the separate Lab 3 distribution are overlapping versions. The final Lab 3 release constrains the implementation to K1 in kernel/usysinfo.c and U1–U3 in user/ulib.c. It contains pid, hart, ticks, syscall_count, ctxsw_count, and state_gen; an earlier combined specification also mentions ppid. In a code viva, describe the fields in the version the examiner is looking at.

The PageFault ZIP explicitly says it is non-graded and does **not** ask for a complete COW or mmap implementation. Do not claim otherwise.

---

# 0. One mental model for all labs

~~~text
user C call
  ↓
generated syscall stub
  ↓  a7 = syscall number; a0–a5 = arguments
ecall
  ↓
trap → usertrap() → syscall()
  ↓
sys_* handler
  ↓  result placed in trapframe a0
sret → user continues
~~~

**Simple meaning:** a system call is a safe gate into the kernel.

**Technical meaning:** Luit uses the RISC-V ABI: syscall number in a7, arguments in a0–a5, and return value in a0. ecall is a privilege-changing trap, not an ordinary C call.

> “A user wrapper loads the syscall number into a7 and arguments into a0 onward, then executes ecall. The kernel validates and dispatches the request in syscall(), puts the result in the saved a0, and returns to user mode.”

Two universal rules:

1. **Mechanism versus policy:** a page fault is the mechanism; lazy allocation, COW, and mmap are policies deciding whether/how to repair it.
2. **Visible state needs a consistency rule:** Lab 2 uses locked rings; Lab 3 uses sequence validation and ordering barriers.

---

# 1. Lab 1 — User programs, processes, pipes, and syscall ABI

## What I implemented

### 10-second version

> “I implemented validated sleep, a two-pipe parent/child ping-pong, a deterministic pstree from one process snapshot, and a tool that validates the generated syscall ABI.”

### 30-second version

> “Lab 1 was user-side work. sleep validates a complete non-negative decimal argument before calling the syscall. pingpong creates two unidirectional pipes, forks, closes all unused endpoints, and exchanges a byte in both directions. pstree takes exactly one procstat() snapshot, sorts it by PID, then prints the hierarchy recursively using ppid. syscallmap.py parses the source syscall table and generated kernel/user ABI files and checks that they agree.”

### 1-minute version

> “The objective was to understand safe user–kernel interaction. In sleep, I keep parsing separate from the syscall, rejecting empty, non-decimal, negative, and overflowing input. In pingpong, I use two pipes because a normal pipe is one-way. Since fork makes both processes inherit every endpoint, each role immediately closes descriptors it does not own; this is essential for correct EOF and blocking behavior. In pstree, I avoid repeated calls to a changing process table: I take one snapshot, validate it, sort it by PID for deterministic output, and recursively find children by matching ppid. The ABI checker treats syscall.tbl as source of truth and compares it to generated numbers, dispatch handlers, names, and user stubs.”

## Objective, files, and expected work

| Component | Concept | Expected implementation |
|---|---|---|
| sleep.c | input validation plus syscall | Parse full non-negative decimal int; call sleep(ticks) and handle error. |
| pingpong.c | pipes, descriptors, fork, wait | Make p2c and c2p, fork, close unused ends, send one byte and reply. |
| pstree.c | snapshot plus process tree | One procstat() snapshot; PID sort; recursion by ppid; cycle/missing-parent handling. |
| syscallmap.py | user–kernel ABI | Parse table/generated ABI, report every mismatch, print all numbered slots including gaps. |

| Important file/symbol | Purpose |
|---|---|
| kernel/pstat.h : struct pstat | Kernel/user snapshot record: pid, ppid, state, name. |
| kernel/syscall.c : syscall() | Dispatch choke point: validates number then calls handler. |
| kernel/syscall.tbl | Source-of-truth syscall description used to generate ABI artifacts. |
| syscallnums.h, syscalltab.h, user stubs | Generated representations that must agree. |
| parse_ticks() | Strict parsing before invoking kernel sleep. |
| print_subtree() | User-space hierarchy reconstruction from a fixed snapshot. |

## Execution flows

### sleep 10

~~~text
argv[1] = "10"
  ↓
parse_ticks(): require every character decimal; reject overflow
  ↓
sleep(10) wrapper → ecall → sys_sleep()
  ↓
kernel waits until ticks advance far enough
  ↓
return 0 → user program exits successfully
~~~

### pingpong

~~~text
parent creates p2c and c2p
  ↓
fork()
  ├─ child: close p2c[1], c2p[0]
  │         read p2c[0] → print ping → write c2p[1] → close → exit
  └─ parent: close p2c[0], c2p[1]
            write p2c[1] → read c2p[0] → print pong → close → wait
~~~

### pstree root-pid

~~~text
parse optional positive root PID
  ↓
procstat(processes, NPROC) exactly once
  ↓
validate count/root; sort by PID
  ↓
print_subtree(root): mark visited → print → recurse to each ppid == root
  ↓
report missing parent links from that same snapshot
~~~

## Important functions

### Function: parse_ticks()

**Purpose:** convert one complete non-negative decimal string to a signed int.

**Inputs:** text and output pointer. **Output:** 0 on success, -1 otherwise.

~~~text
reject null/empty
  ↓
for every character: require '0'..'9'
  ↓
check bound before value = value*10 + digit
  ↓
store result and return 0
~~~

**Why written this way?** A loose parser could accept 12abc, -1, or an overflowed value.

> “I check the limit before multiplying and adding the next digit, because an overflowed intermediate value is no longer reliable. I require the entire argument to be one non-negative decimal integer.”

### Function: print_subtree()

**Purpose:** print one rooted tree from the local pstat snapshot.

~~~text
find record by root PID
  ↓
if visited: stop/warn
  ↓
mark before descent; print indentation, name(pid), state
  ↓
scan sorted snapshot for each ppid == root PID
  ↓
recurse at depth + 1
~~~

> “I mark the record before recursion so even anomalous/cyclic snapshot data cannot make the program recurse forever. Sorting by PID makes sibling output deterministic.”

### Function: syscallmap.py validate()

**Purpose:** compare all ABI representations and return every inconsistency.

**It checks:** duplicate/non-positive source rows; NSYSCALL; generated SYS_name; dispatch handler/name; user stub; and unexpected generated entries.

> “I report all mismatches rather than stopping at the first, because one bad source or generation step can create several downstream ABI inconsistencies.”

## Viva questions — ready answers

### 🔴 Q: Why do you close unused pipe descriptors?

### What the professor expects

That fork duplicates descriptors and EOF needs every write end closed.

### Answer I should say

> “After fork, both parent and child own copies of every pipe descriptor. I close each endpoint that a role does not use. A reader receives EOF only when no write descriptor remains open anywhere, so an inherited unused write end can otherwise make a read wait forever.”

### If professor asks “why?”

> “The pipe tracks whether writers still exist, not whether my intended writer finished its current write.”

### Likely follow-up

**Q:** Why two pipes?  
**A:** “A normal pipe is unidirectional. One is parent-to-child and the other child-to-parent, which makes ownership and ordering clear.”

### 🔴 Q: What does fork() do in ping-pong?

### Answer I should say

> “It creates a child process. The child receives zero and the parent receives the child PID, so the same program chooses child or parent behavior. The child also inherits the pipe descriptors, which is why descriptor closing is part of the protocol.”

### If professor asks “why wait?”

> “wait() reaps the child’s exit status, avoiding a zombie, and makes parent completion explicit.”

### 🔴 Q: Why exactly one procstat() snapshot?

### Answer I should say

> “I build the whole tree from one observed snapshot. Repeated procstat() calls could mix processes from different times—one child could appear after its parent exits—so the output would not represent one coherent view.”

### If professor asks “why sort?”

> “Kernel table order is not a presentation contract. PID sorting makes sibling order deterministic and testable.”

### 🟠 Q: How is a syscall different from a normal function call?

> “A normal function call remains at the same privilege level. A syscall executes ecall, enters kernel trap handling, validates untrusted user arguments, dispatches a kernel handler, and returns through saved trap state.”

### 🟡 Q: What if a pstree root PID is absent?

> “I reject it before recursion. Printing nothing would look like a valid empty tree and hide an invalid request or incomplete snapshot.”

### 🔵 Q: Why must the kernel not directly dereference a user pointer?

> “A user pointer may be null, unmapped, or a forbidden address. The kernel uses copyin, copyout, and copyinstr so it validates the mapping rather than allowing untrusted input to crash or corrupt kernel execution.”

## Follow-up chain: pipes

~~~text
Professor: What does a pipe provide?
You: A kernel byte stream with one read end and one write end.

Professor: Why two pipes?
You: Each pipe is one-way, so we need one direction per message direction.

Professor: What changes after fork?
You: Both processes inherit all pipe descriptors.

Professor: Why close unused endpoints?
You: Correct ownership, no leaks, and correct EOF: any open writer prevents EOF.

Professor: What if an unused writer stays open?
You: A reader can block forever waiting for more data or EOF.
~~~

## Common confusions

| Don’t confuse | With | Actual difference |
|---|---|---|
| fork | exec | fork creates a child; exec replaces current program image. |
| PID | PPID | PID identifies process; PPID identifies parent in snapshot. |
| empty pipe | EOF | Empty with writers blocks; EOF means empty and all writers closed. |
| pipe read end | pipe write end | Reads consume bytes; writes append bytes. |
| syscall | C call | Syscall crosses user/kernel privilege boundary via ecall. |

## Lab 1 — 5 minute viva revision

### 10 facts I MUST remember

1. a7 is syscall number; a0 is first argument and return value.
2. ecall enters kernel; sret returns to user mode.
3. fork() returns 0 in child and PID in parent.
4. wait() reaps the child.
5. Pipes are one-way: ping-pong needs two.
6. fork() duplicates descriptors.
7. Open unused writers prevent EOF.
8. pstree uses one snapshot, then PID-sort and recursive ppid scan.
9. visited[] prevents infinite recursion on anomalous data.
10. syscall.tbl is ABI source of truth.

### 15 most likely questions

~~~text
What did you implement? → Validated sleep, two-pipe pingpong, snapshot pstree, ABI checker.
Why two pipes? → One pipe is unidirectional; reply needs another channel.
Why close descriptors? → Inherited writers prevent EOF and can hang readers.
What does fork return? → 0 child, child PID parent, negative failure.
Why wait? → Reap child and synchronize termination.
Why validate sleep input? → Reject malformed, negative, or overflowed durations.
Why one procstat call? → One coherent observed data set.
Why sort? → Deterministic tree output.
Why visited[]? → Avoid a bad cycle causing infinite recursion.
What is PPID? → Parent PID in the snapshot.
Where is syscall number? → a7.
Where is syscall result? → a0.
Why checked copy helpers? → User pointers are untrusted.
Why generated ABI files? → User stubs and kernel dispatch must agree.
What does ABI checker verify? → Table, numbers, handlers/names, and user stubs agree.
~~~

### 5 non-trivial questions

~~~text
Why does EOF depend on all writers? → Kernel must allow each remaining writer to send data.
What if child retains unwanted writer? → Reader may never receive EOF.
Why are repeated snapshots bad? → They mix states from different times.
Why print ABI gaps? → Gaps are part of numbering contract and reveal omissions.
Why check before integer multiply? → Avoid using overflowed value.
~~~

### Diagram to memorize

~~~text
parent                         child
 p2c[1] write ───────────────→ p2c[0] read
 c2p[0] read  ←─────────────── c2p[1] write
~~~


---

# 2. Lab 2 — Kernel tracing with per-hart ring buffers

## What I implemented

### 10-second version

> “I implemented a structured kernel tracer with a separate ring buffer per hart, per-process filters, overflow accounting, fork inheritance, and a user-space drain interface.”

### 30-second version

> “I added tracing at the syscall dispatch choke point after the handler returns, so each event contains the actual return value. A traced process records into the ring belonging to its current hart, avoiding a global hot-path lock. When a ring is full I increment dropped instead of blocking or overwriting. tracectl enables or disables a per-process filter, fork copies that filter to the child, and traceread safely drains records to user space.”

### 1-minute version

> “The central design decision was per-hart buffering. Every syscall passes through syscall(), so I saved the syscall number and first argument, executed the handler, and then called trace_record with the result. Each event has pid, hart, syscall number, timestamp, return value, and arg0. Each hart records into its own ring under that ring’s lock, so unrelated harts do not contend on a global lock. A full ring increments a dropped counter because observability must report loss rather than silently corrupt history. The filter lives in struct proc, so tracectl affects the current process and trace_fork makes a child inherit that policy. traceread locks each ring while safely returning structured events through copyout.”

## Core execution flow

~~~text
user program
  ↓ ecall
kernel syscall()
  ├─ get syscall number from trapframe a7
  ├─ save first argument a0
  ├─ call syscalls[num]()
  ├─ result is now in a0
  └─ trace_record(num, result, saved_arg0)
          ↓ filter check
     current hart’s trace ring
          ↓
event {pid, hart, num, ts, ret, arg0}
          ↓
traceread(buffer, max) → copyout → user trace command formats output
~~~

## Structures, filters, and functions

| Symbol | Meaning | Viva point |
|---|---|---|
| struct trace_event | pid, hart, num, ret, ts, arg0 | Fixed-size structured data is cheap to record and format later. |
| struct trace_ring | lock, event array, head, count, dropped | Bounded FIFO queue with explicit loss accounting. |
| rings[NCPU] | one ring per hart | Hot path avoids global inter-hart lock contention. |
| struct proc trace_filter | per-process policy | Child inheritance is natural process behavior. |
| trace_record() | producer hot path | Filter first, lock only local ring, append or increment dropped. |
| trace_ctl() | tracectl backend | Enable/disable current process filter. |
| trace_read() | traceread backend | Validate max, drain safely, copy output into caller’s user memory. |
| trace_fork() | fork hook | Copy parent trace filter to child. |

~~~text
filter == 0       tracing disabled
bit 63 set        trace every syscall
otherwise         bit num selects syscall number num
~~~

### Ring picture

~~~text
                 producer writes at head
                          ↓
 [event][event][empty][empty] ... [empty]
    ↑
 oldest index = (head + NTRACE - count) % NTRACE

 count == NTRACE → full → do not overwrite → dropped++
~~~

## Important functions — code-reading answers

### Function: trace_record(num, ret, arg0)

**Purpose:** append one completed selected syscall event to the current hart’s ring.

**Inputs:** syscall number, completed return value, and first argument saved before dispatch.

~~~text
get current process
  ↓
return if filter disabled or excludes number
  ↓
select rings[hal_hart_id()]
  ↓
acquire local ring lock
  ↓
if full: dropped++
else: fill entire event; advance head modulo NTRACE; count++
  ↓
release lock
~~~

**Why after the handler?** The return value does not exist until it completes. Recording before execution logs stale or wrong ret data.

> “I filter before locking so disabled tracing has almost no cost. I save arg0 before dispatch because the handler overwrites a0 with its result. Then I lock only the local ring, record the completed event, or count loss if full.”

### Function: trace_read(user_buffer, max)

**Purpose:** deliver at most max kernel events to the user caller.

~~~text
if max <= 0: return -1
  ↓
for every hart while space remains:
  lock ring
  find oldest event
  remove/copy event safely
  copyout to caller buffer
  report one overflow record when dropped > 0 and space remains
  unlock ring
~~~

**Why copyout?** The supplied buffer address belongs to user virtual address space. It is not safe for the kernel to dereference it directly.

> “traceread synchronizes against the producer before consuming ring metadata and uses copyout for every user-space result. If user copying fails after some records, the normal useful behavior is to return the partial count; if none were copied, return an error.”

### Version note: cross-hart ordering

The Lab 2 specification asks for drain in approximately timestamp order across harts. The supplied completed Lab 3 checkpoint drains each ring in hart order. Do **not** claim a strict global timestamp merge unless the code the examiner shows explicitly chooses the earliest timestamp among every non-empty ring. The invariant you can defend in every version is synchronized draining, no torn event, and explicit overflow accounting.

## Viva questions — ready answers

### 🔴 Q: Why per-hart rings instead of one global ring?

### What the professor expects

Contention reasoning, not merely “faster.”

### Answer I should say

> “A global ring makes every traced syscall on every hart compete for one lock. Tracing runs on a hot path, so that serializes unrelated CPUs. With a ring per hart, a producer normally touches only its local lock and storage. The trade-off is that traceread must aggregate multiple rings.”

### If professor asks “why is that trade-off good?”

> “Recording is frequent and latency-sensitive; draining is relatively infrequent. I move aggregation cost to the cold path.”

### Likely follow-up

**Q:** Is global cross-hart order automatic?  
**A:** “No. Per-hart order is natural. A global order needs explicit timestamp merge logic and timestamp semantics.”

### 🔴 Q: Why record after syscall execution?

### Answer I should say

> “The event must contain the real return value. I save arg0 first, execute the handler, and then record the result now stored in a0. Recording before execution would produce an incorrect ret field.”

### 🔴 Q: What happens when a trace ring is full?

### Answer I should say

> “The producer never blocks and never silently overwrites an old event. It increments that ring’s dropped counter. traceread later exposes an overflow record, so the trace consumer knows the history is incomplete.”

### 🟠 Q: Why is a lock required when each ring is local?

> “The normal producer is local to one hart, but a consumer can drain the ring concurrently. The lock prevents inconsistent head/count state or a reader seeing only part of an event.”

### 🟡 Q: What is a torn event?

> “Without synchronization, a reader might see a new PID while timestamp or return value is still from old/uninitialized storage, or see metadata saying an event exists before every field is visible. Locking its publication prevents such mixed state.”

### 🔵 Q: Why is naive printf tracing expensive?

> “Formatting strings and console I/O cost much more than appending a fixed-size record and can serialize execution. The ring defers formatting to user space.”

## Follow-up chain: tracing

~~~text
Professor: Where is tracing hooked?
You: In syscall(), the one choke point every syscall crosses.

Professor: Before or after handler?
You: After, so ret is the actual outcome; arg0 was saved before it changes.

Professor: Why per-hart?
You: Avoid global producer contention on each syscall.

Professor: What if full?
You: dropped++ with no block/no overwrite, later reported to reader.

Professor: How is child tracing enabled?
You: trace_fork copies parent trace_filter during fork.

Professor: How are events returned?
You: traceread synchronizes ring drain and copyout transfers records to user buffer.
~~~

## Common confusions

| Don’t confuse | With | Actual difference |
|---|---|---|
| hart | process | A hart is hardware execution context; a process is scheduled program state. |
| ring | growable list | A ring has fixed capacity, wraparound, and a full policy. |
| timestamp | strict global order | Timestamp helps ordering; global merge is separate policy. |
| filter inheritance | copying trace data | Child gets filter policy, not a private copy of old ring records. |
| full ring | safe overwrite | This lab explicitly reports loss instead of hiding it. |

## Lab 2 — 5 minute viva revision

### 10 facts I MUST remember

1. syscall() is the trace dispatch choke point.
2. Save arg0 before handler; record ret after handler.
3. Event fields: pid, hart, num, timestamp, ret, arg0.
4. There is one bounded ring per hart.
5. Filter before lock.
6. Bit 63 means trace everything; bit num selects one syscall.
7. Full means dropped++, never silent overwrite/block.
8. tracectl changes a per-process filter.
9. trace_fork copies parent filter to child.
10. traceread synchronizes and uses copyout.

### 15 most likely questions

~~~text
What did you implement? → Per-hart structured tracing with filter, overflow, inheritance, readout.
Why syscall hook? → Every syscall crosses it.
Why after handler? → Real return value.
Why save arg0? → Handler overwrites a0.
Why per-hart? → No global hot-path lock contention.
What is an event? → pid, hart, num, ts, ret, arg0.
What if tracing off? → Return before ring lock.
What if full? → Increment dropped.
Why spinlock? → Prevent torn event/metadata races.
How enabled? → tracectl sets current filter.
How inherited? → trace_fork copies trace_filter.
Why copyout? → User buffer is untrusted virtual address.
Why max <= 0 errors? → Invalid ambiguous drain request.
Why printf slower? → Format/I/O work and serialization.
What is cross-hart drain cost? → Visiting/synchronizing rings, moved to cold path.
~~~

### 5 non-trivial questions

~~~text
Why not one global ring? → Every hart serializes at one lock.
Why not overwrite oldest? → Hides loss and changes history.
Why must dropped remain visible? → Consumer must know trace is incomplete.
Why filter first? → Preserve low-overhead off path.
What is torn event window? → Consumer sees event metadata before fields are coherently published.
~~~

### Diagram to memorize

~~~text
Hart 0 syscall → ring 0 ┐
Hart 1 syscall → ring 1 ├─ traceread() → user events
Hart 2 syscall → ring 2 ┘

hot path is partitioned; cold path aggregates
~~~


---

# 3. Lab 3 — Versioned shared kernel–user information page

## What I implemented

### 10-second version

> “I implemented a per-process user-readable shared page and a seqlock protocol so fast user functions can read PID and uptime without a syscall and without seeing torn multi-field data.”

### 30-second version

> “Each process has one physical usysinfo page mapped at fixed USYSINFO_VA with PTE_R and PTE_U, deliberately without PTE_W. The kernel writer makes seq odd, uses a barrier, writes all fields, uses another barrier, and makes seq even. The user reader retries if seq is odd, copies the structure, and accepts it only if a second seq read is unchanged and even. u_getpid and u_uptime use that snapshot and fall back to real syscalls on ABI failure.”

### 1-minute version

> “The lab is about safe sharing rather than just mapping a page. I allocate one page per process, initialize struct usysinfo, and map it at 0x7FFFF000 as user-readable but not user-writable. It is per process so PID and counters cannot leak between processes. In usysinfo_update, I disable local interrupts for the short publication, increment seq to an odd value, issue a full barrier, copy pid, hart, ticks, syscall count, context-switch count, and generation, issue a second barrier, then make seq even. The reader sees odd as writer-active; otherwise it copies all fields and verifies sequence remained equal and even. That detects overlap and prevents torn snapshots. The normal u_getpid/u_uptime path is only page reads, not ecall.”

## Core layout and lifecycle

~~~text
allocproc()
  ↓
palloc one physical page
  ↓
initialize version = 1, seq = 0
  ↓
mappages(process page table,
         USYSINFO_VA = 0x7FFFF000,
         PTE_R | PTE_U)     // deliberately no PTE_W
  ↓
p->usysinfo points to same page for kernel updates
~~~

~~~text
kernel writer                         user reader
-------------                         -----------
seq++ → odd                           read seq1
barrier                               if odd: retry
write every field                     barrier
barrier                               copy every field
seq++ → even                          barrier
                                      read seq2
                                      accept iff seq1 == seq2 and even
~~~

## Fields and important code

| Symbol | Meaning | Why it matters |
|---|---|---|
| USYSINFO_VA | fixed VA 0x7FFFF000 | User library knows address without another syscall. |
| USYSINFO_VERSION | ABI layout version | Reader can reject incompatible layout safely. |
| struct usysinfo seq | volatile sequence counter | Odd means update in progress; matching even reads prove stable copy. |
| p->usysinfo | kernel pointer to backing page | Binds lifetime to process. |
| usysinfo_setup() | allocate/map per process | Mapping is user-readable but not writable. |
| usysinfo_free() | explicit cleanup | Needed because high VA lies above normal p->sz free range. |
| usysinfo_remap() | exec lifecycle | New exec page table must receive this process’s existing info page. |
| usysinfo_update() | K1 writer | Publishes coherent kernel state. |
| u_snapshot() | U1 reader | Validated lock-free snapshot. |
| u_getpid / u_uptime | U2/U3 accessors | Fast path uses page; error path uses actual syscall. |

## Important functions — code-reading answers

### Function: usysinfo_update(struct proc *p)

**Purpose:** publish one coherent snapshot of dynamic process information.

**Inputs:** p, global ticks, and process counters. **Output:** none; readers see previous complete state or next complete state.

~~~text
if p->usysinfo is zero: return
  ↓
push_off() prevents local interrupt/preemption through publication
  ↓
seq++                // odd: writer active
  ↓
full memory barrier
  ↓
write pid, hart, ticks, syscall_count, ctxsw_count, state_gen
  ↓
full memory barrier
  ↓
seq++                // even: stable
  ↓
pop_off()
~~~

> “The odd/even value brackets all data writes. The barriers prevent compiler or CPU visibility reordering from exposing a field outside those brackets. push_off does not make readers lock; it protects the single short kernel writer update from local interruption.”

### Function: u_snapshot(struct usysinfo *out)

**Purpose:** copy one compatible, coherent shared snapshot into ordinary user memory.

**Inputs:** output pointer. **Output:** 0 on a stable compatible snapshot; -1 for null pointer or ABI mismatch.

~~~text
reject out == 0; verify version
  ↓
read seq1
  ↓
if seq1 odd: retry
  ↓
barrier → copy every field → barrier
  ↓
read seq2
  ↓
accept only if seq1 == seq2 and even; otherwise retry
~~~

> “The first sequence read detects an update already in progress. The second detects an update that started or finished while I copied fields. I must copy every field before the second read, otherwise it would not validate the complete snapshot.”

### Function: usysinfo_free() and usysinfo_remap()

> “USYSINFO_VA is above p->sz, so ordinary uvmfree of the normal user range would miss its backing leaf page. usysinfo_free explicitly unmaps/frees it. exec builds a fresh page table, so usysinfo_remap maps this process’s same information page into the new page table before commit.”

## Viva questions — ready answers

### 🔴 Q: Why do odd and even sequence numbers prevent torn snapshots?

### What the professor expects

Explanation of both reader checks.

### Answer I should say

> “Odd means the kernel writer has started updating, so readers do not trust fields while seq is odd. A reader that starts with an even value copies all fields and reads seq again. It accepts only when both reads are equal and even. If a writer overlapped the copy, seq was odd or changed, so the reader retries rather than returning a mixture of old and new fields.”

### If professor asks “why two reads?”

> “The first detects a writer already active; the second detects one that begins or completes during the field copy.”

### Likely follow-up

**Q:** Can a reader retry forever?  
**A:** “Under continuous writes it can retry repeatedly. That is the seqlock trade-off; it is chosen for read-mostly state where writers are short and less frequent.”

### 🔴 Q: Why are there two memory barriers?

### Answer I should say

> “The first barrier keeps the odd update-in-progress publication before field writes. The second keeps all field writes before the final even publication. Without the second, an even sequence could become visible before some fields, and a reader could wrongly accept a partial snapshot.”

### If professor asks “what if first barrier is removed?”

> “A new field could become visible before the odd marker, letting a reader start from an old even value while observing partly new data. Both barriers are necessary.”

### 🔴 Q: Why map the page read-only into user space?

### Answer I should say

> “The kernel is the authoritative writer. User processes need fast observation, not permission to forge their PID, counters, or sequence state. PTE_R plus PTE_U permits reads but no PTE_W means a user write faults that process instead of corrupting kernel-published state.”

### 🟠 Q: Why one page per process, not one global page?

> “These values are process-specific: PID, current hart, syscall count, and context-switch count. A global page leaks state between processes and creates a multi-writer problem. One page per process gives isolation and a simple single-writer-per-process argument.”

### 🟡 Q: Why do fast accessors still fall back to syscalls?

> “The page structure is a versioned ABI. If the reader cannot trust the layout or take a valid snapshot, using getpid or uptime preserves correctness. Optimization must not make ABI incompatibility return wrong data.”

### 🔵 Q: Why is version separate from seq?

> “Version answers whether this reader understands the layout at all. Sequence answers whether one update of that known layout was consistent. Compatibility and consistency are different jobs.”

## Follow-up chain: shared information page

~~~text
Professor: How did you speed up getpid?
You: I mapped a per-process kernel-published page at a fixed user-readable VA.

Professor: Why not one PID integer?
You: This page has multiple changing fields, so I need consistent multiword snapshots.

Professor: Explain the seqlock.
You: Writer odd → barrier → fields → barrier → even; reader checks before/after sequence.

Professor: Why read-only?
You: User can observe authoritative kernel state but cannot forge it.

Professor: What if versions differ?
You: u_snapshot rejects it; fast accessors fall back to real syscalls.

Professor: What happens at exit/exec?
You: Explicit high-page free prevents leak; exec remaps same page into new page table.
~~~

## Common confusions

| Don’t confuse | With | Actual difference |
|---|---|---|
| seqlock | ordinary mutex | Readers do not acquire it; they retry after detecting overlap. |
| odd seq | error | It is a normal in-progress writer state. |
| matching even seq | permanent freshness | It proves consistency for that read, not that no later update occurs. |
| PTE_U | PTE_W | PTE_U permits user-mode access; R/W bits decide allowed operation. |
| fixed virtual address | global physical page | Same VA maps a different physical page in each process. |
| ABI version | sequence value | Version checks layout; sequence checks one publication. |

## Lab 3 — 5 minute viva revision

### 10 facts I MUST remember

1. USYSINFO_VA is 0x7FFFF000.
2. User mapping is PTE_R | PTE_U, without PTE_W.
3. There is one backing page per process.
4. Odd seq means writer active; even means no active update.
5. Writer: odd → barrier → fields → barrier → even.
6. Reader: seq1 → reject odd → copy → seq2 → accept same/even.
7. Barriers prevent visibility reordering.
8. u_getpid and u_uptime use page reads on compatible fast path.
9. They fall back to syscalls on invalid/incompatible snapshot.
10. Explicit free/remap handles page above p->sz and new exec page tables.

### 15 most likely questions

~~~text
What did you implement? → Versioned per-process shared information page with seqlock publication.
Why fixed VA? → User library reads it without address lookup/syscall.
Why per process? → Isolation and process-specific fields.
Why read-only? → Kernel state must not be user-forgeable.
Why odd/even? → Odd declares update; matching even reads validate copy.
Why two seq reads? → Detect overlap before and during copy.
Why two barriers? → Keep odd before fields and fields before final even.
Why volatile seq? → Force rereads from shared memory.
Why push_off? → Avoid local interruption during short writer update.
What fields? → pid, hart, ticks, syscall_count, ctxsw_count, state_gen.
Why version? → ABI compatibility.
Why fallback? → Preserve correctness.
What on user write? → Store page fault kills offending process, kernel survives.
Why explicit free? → Normal p->sz cleanup misses high mapping.
Why remap on exec? → Exec creates new page table.
~~~

### 5 non-trivial questions

~~~text
What if second barrier is gone? → Final even seq may precede field visibility; partial snapshot can be accepted.
What if reader accepts odd seq? → It can return data during active write.
Why reader retries rather than locks? → Cheap common reads, rare short writes.
Can two processes share contents? → No; same VA maps per-process physical page.
What causes retry? → Writer active at start or overlaps field copy.
~~~

### Diagram to memorize

~~~text
Writer: seq odd | barrier | fields | barrier | seq even
Reader: seq1    | if odd retry | copy | seq2
Accept only when: seq1 == seq2 AND seq1 is even
~~~


---

# 4. Non-graded Page-Fault Practicum — what to explain

## Accurate “What did I do?” answer

This item is an observational practicum, not a complete VM implementation lab.

> “In the page-fault practicum, I used pfdemo and GDB to distinguish missing mappings from permission faults by reading scause, stval, sepc, and the PTE. I temporarily repaired only the controlled stack-guard demonstration to observe mapping installation and retry of the same instruction. I did not claim to implement complete COW or mmap; the practicum uses them to explain how future fault handlers classify and repair faults.”

### 10-second version

> “I diagnosed user page faults from trap state and PTEs, then observed one temporary demand-zero repair and retry path.”

### 30-second version

> “I compared an unmapped load, a write to the read-only shared page, a guard-page store, and an instruction fetch fault. The same store-fault cause can mean a missing PTE or valid read-only PTE, so I inspect both trap registers and page table. The temporary guard experiment allocates, maps, fences, and returns to the same instruction; it does not advance epc.”

## Fault decoder

| scause | Operation | Example | Key diagnosis |
|---:|---|---|---|
| 12 | instruction page fault | fetch at unmapped 0x40000000 | No executable translation; demand-paged exec might repair a valid segment. |
| 13 | load page fault | read unmapped address | PTE absent/invalid unless VM metadata says a lazy region is legitimate. |
| 15 | store page fault | USYSINFO or guard write | Could be permission W=0 or absent mapping; inspect PTE. |

~~~text
scause  → why CPU trapped: fetch/load/store fault
stval   → exact faulting virtual address
sepc    → instruction that faulted
satp    → active address-space/page-table root
~~~

## Key comparisons

| Case | PTE state | Baseline result |
|---|---|---|
| Unmapped load | absent/invalid | process killed |
| USYSINFO write | valid, U=1, R=1, W=0 | permission store fault; process killed |
| Stack guard write | absent/invalid | missing-mapping store fault; process killed |
| COW-shaped write | valid, W=0, COW=1 | future COW policy may copy/remap/retry |

## Recoverable fault flow

~~~text
faulting load/store/fetch
  ↓
usertrap reads scause, stval, sepc
  ↓
inspect PTE plus VM metadata
  ↓
valid lazy/COW/VMA policy?
  ├─ no  → kill process
  └─ yes → allocate or update mapping; sfence.vma if needed
              ↓
       do NOT advance saved epc
              ↓
       sret → same instruction retries and now succeeds
~~~

## The crucial epc rule

> “For ecall, the call instruction completed and the kernel advances saved PC to continue after it. For a page fault, the load, store, or fetch did not complete. After repair I return to the same saved PC so the CPU retries it. Advancing epc would skip the operation.”

## COW, copyout, mmap, and demand-paged exec

### Copy-on-write

~~~text
after COW fork:
parent VA → physical P, W=0, COW=1
child  VA → physical P, W=0, COW=1
reference-count(P) = 2

user store → scause 15 → detect COW → private page/remap → retry store
~~~

> “COW deliberately clears PTE_W so hardware traps on the first write. The software COW bit tells the kernel that this temporarily read-only mapping is recoverable, unlike an ordinary read-only page.”

### Why copyout also needs COW handling

> “A user store naturally causes a hardware store fault. copyout is different: the kernel walks the user PTE in software and writes through a kernel mapping of the physical page. If it sees W=0 and COW=1, it must invoke the same break-sharing logic, then re-walk the PTE before copying. Otherwise it could write to the old shared physical page.”

### mmap is a policy for a missing PTE

~~~text
fault_page = page-round-down(stval)
file_offset = vma.off + (fault_page - vma.start)

validate VMA and access permission
  ↓
obtain physical page; read file-backed bytes; zero tail
  ↓
install PTE permissions; fence if needed; retry same instruction
~~~

### Demand-paged exec

The fault handler must retain ELF metadata: virtual range, file offset, filesz, memsz, permissions, and backing inode. filesz is bytes actually in executable file; memsz is full in-memory segment size, including a zero-filled tail such as BSS.

## Viva questions — ready answers

### 🔴 Q: Is every page fault a missing mapping?

### Answer I should say

> “No. A page fault can come from a missing/invalid PTE or from a permission violation. Writing USYSINFO gives a store fault even though its PTE is valid and user-readable, because W is clear. I use scause, stval, sepc, PTE flags, and VM metadata together.”

### 🔴 Q: Why leave the stack guard page unmapped?

> “It deliberately catches stack overflow or invalid stack access immediately. Mapping it permanently as demand-zero hides that bug, so the repair patch is only a controlled experiment and must be reverted.”

### 🟠 Q: Why is sfence.vma needed after PTE repair?

> “The CPU may retain an old translation or permission in its TLB. After a PTE update, sfence.vma ensures the retry uses the new mapping rather than stale translation state.”

### 🟡 Q: How does kernel distinguish illegal missing memory from valid lazy memory?

> “Hardware only reports access type and address. The kernel uses its own metadata: heap bounds for lazy allocation, COW bit for write faults, VMA information for mmap, or saved ELF segment information for demand paging. The same absent PTE can be illegal in one region and recoverable in another.”

## Page-fault practicum — 5 minute revision

~~~text
scause 12 → instruction page fault
scause 13 → load page fault
scause 15 → store page fault
stval     → faulting VA
sepc      → faulting instruction
missing PTE ≠ permission fault
repair → update/map PTE → fence if needed → do not advance epc → retry
COW → W=0 plus software COW bit; first write breaks sharing
copyout → needs software COW path too
filesz → file bytes; memsz → total memory including zero-filled tail
~~~

---

# 5. Code-based viva prompts

| Code pattern | What examiner may ask | Answer I should say |
|---|---|---|
| argument-count check | Why check first? | “It establishes valid user input before resource allocation or syscall.” |
| close(unused_fd) | Why this exact close? | “That endpoint is not owned by this role; closing it prevents leaks and permits correct EOF.” |
| if pid == 0 | Why this branch? | “Only child receives zero from fork; parent gets new child PID.” |
| visited[index] | Why mark before recursion? | “It stops a repeated record from re-entering recursively.” |
| syscall-number bounds | Why before table index? | “User register input is untrusted; it prevents invalid dispatch access.” |
| saved arg0 before handler | Why save now? | “Handler overwrites a0 with return value.” |
| count == NTRACE | Why not overwrite? | “This design requires explicit auditable loss, not hidden history destruction.” |
| ring lock around event update | Why whole update? | “Consumer must not see partial event fields or mismatched metadata.” |
| copy filter on fork | Why? | “Tracing policy should follow the child workload started by traced parent.” |
| PTE_R and PTE_U but no PTE_W | Why? | “User may observe, but cannot alter kernel-published state.” |
| seq odd before fields | Why? | “Reader immediately knows fields are not trustworthy.” |
| barrier before final even seq | Why? | “Stable publication cannot become visible before fields.” |
| read seq twice | Why? | “One read cannot detect a writer that starts during copy.” |
| fallback syscall | Why? | “Correct result remains available when fast ABI cannot be trusted.” |
| sfence.vma after PTE change | Why? | “Retry must not use stale cached mapping/permissions.” |


---

# 6. Top 100 most likely viva questions

The first 30 are highest value. Each answer is deliberately short enough to speak, and each follow-up tells you where the examiner is likely to go next.

1. **Q:** What did you implement in Lab 1?  
   **Answer I should say:** “Validated sleep, two-pipe ping-pong, a snapshot-based pstree, and a syscall ABI checker.”  
   **Follow-up:** Why user programs? → “The lab taught safe use of the existing user–kernel interface.”

2. **Q:** What did you implement in Lab 2?  
   **Answer I should say:** “A per-hart structured syscall tracer with filters, overflow accounting, fork inheritance, and user readout.”  
   **Follow-up:** Why per-hart? → “It avoids global hot-path lock contention.”

3. **Q:** What did you implement in Lab 3?  
   **Answer I should say:** “A versioned per-process shared page with seqlock publication for syscall-free consistent reads.”  
   **Follow-up:** Why seqlock? → “Multi-field raw reads could otherwise tear.”

4. **Q:** What does fork return?  
   **Answer I should say:** “Zero in the child, child PID in parent, and negative on failure.”  
   **Follow-up:** Why useful? → “One program can choose parent and child roles.”

5. **Q:** Why are two pipes required for ping-pong?  
   **Answer I should say:** “A normal pipe is unidirectional; one carries ping and the other pong.”  
   **Follow-up:** Why not one stream? → “Both sides would compete on the same directionless byte stream.”

6. **Q:** Why close unused descriptors?  
   **Answer I should say:** “fork inherits every endpoint; unused writers keep a pipe alive and can prevent EOF.”  
   **Follow-up:** What if not closed? → “A read can block forever waiting for EOF.”

7. **Q:** What does wait do?  
   **Answer I should say:** “It waits for and reaps a terminated child, collecting its status and avoiding a zombie.”  
   **Follow-up:** Is it only a delay? → “No, it releases child process state too.”

8. **Q:** Which registers hold syscall ABI values?  
   **Answer I should say:** “a7 holds syscall number, a0–a5 arguments, and a0 return value.”  
   **Follow-up:** Kernel entry? → “The generated stub executes ecall.”

9. **Q:** Why validate syscall number before dispatch-table access?  
   **Answer I should say:** “It comes from untrusted user register state, so checking bounds prevents invalid kernel indexing.”  
   **Follow-up:** Failure behavior? → “Return controlled error, not crash kernel.”

10. **Q:** Why record trace event after the syscall handler?  
    **Answer I should say:** “Only then is the real return value available; arg0 was saved before a0 is overwritten.”  
    **Follow-up:** What if before? → “Trace ret field would be stale or wrong.”

11. **Q:** What fields are recorded in a trace event?  
    **Answer I should say:** “PID, hart, syscall number, timestamp, return value, and first argument.”  
    **Follow-up:** Why structured event? → “Kernel can record cheaply and format later.”

12. **Q:** What happens if the trace ring is full?  
    **Answer I should say:** “The producer does not block or overwrite; it increments dropped and later reports loss.”  
    **Follow-up:** Why not silent? → “A trace that hides loss is misleading.”

13. **Q:** Why does trace ring need locking?  
    **Answer I should say:** “Consumer and producer can overlap; lock prevents torn event data and inconsistent metadata.”  
    **Follow-up:** Torn event? → “Fields from different update states mixed together.”

14. **Q:** How does a child inherit tracing?  
    **Answer I should say:** “The fork hook copies parent trace_filter into the child.”  
    **Follow-up:** Why? → “Tracing a launcher should trace its child workload.”

15. **Q:** What does odd seq mean in Lab 3?  
    **Answer I should say:** “Kernel writer is updating; reader must retry.”  
    **Follow-up:** What does even mean? → “No active update, but reader still validates before/after.”

16. **Q:** Explain the shared-page writer.  
    **Answer I should say:** “Make seq odd, barrier, write all fields, barrier, make seq even.”  
    **Follow-up:** Why barriers? → “Prevent fields becoming visible outside sequence bracket.”

17. **Q:** Explain the shared-page reader.  
    **Answer I should say:** “Read seq1, retry if odd, copy fields, read seq2, accept only equal even values.”  
    **Follow-up:** Why two reads? → “Detect writer overlap during copy.”

18. **Q:** Why is USYSINFO read-only to user?  
    **Answer I should say:** “User needs observation, not authority to forge kernel PID/counters/sequence.”  
    **Follow-up:** Write attempt? → “Permission store fault kills offending process.”

19. **Q:** Why one information page per process?  
    **Answer I should say:** “Values are process-specific and must remain isolated; same VA maps different physical page per process.”  
    **Follow-up:** Global page problem? → “Leaks state and creates multiple writers.”

20. **Q:** Why fallback to getpid/uptime?  
    **Answer I should say:** “Version mismatch or failed snapshot must not produce wrong data; slow syscall preserves correctness.”  
    **Follow-up:** Optimization priority? → “Correctness first.”

21. **Q:** What is scause 12?  
    **Answer I should say:** “Instruction page fault.”  
    **Follow-up:** Demand exec? → “Could repair if valid text page is intentionally lazy.”

22. **Q:** What is scause 13?  
    **Answer I should say:** “Load page fault.”  
    **Follow-up:** Always illegal? → “No; VM metadata could define valid lazy memory.”

23. **Q:** What is scause 15?  
    **Answer I should say:** “Store page fault.”  
    **Follow-up:** How distinguish reason? → “Inspect PTE for absent mapping versus W=0.”

24. **Q:** What are stval and sepc?  
    **Answer I should say:** “stval is faulting virtual address; sepc is faulting instruction.”  
    **Follow-up:** Why both? → “Address diagnoses translation; PC identifies operation.”

25. **Q:** Why not increment epc after repair?  
    **Answer I should say:** “Faulting load/store/fetch did not complete, so return to same instruction for retry.”  
    **Follow-up:** Why ecall differs? → “Ecall completed and kernel advances after it.”

26. **Q:** Why does COW clear PTE_W?  
    **Answer I should say:** “First write must trap so kernel copies only when necessary.”  
    **Follow-up:** Normal read-only distinction? → “Software COW bit marks recoverable case.”

27. **Q:** Why does copyout need COW handling?  
    **Answer I should say:** “It writes through user page-table mapping in software, not through a user store fault.”  
    **Follow-up:** After COW repair? → “Re-walk PTE before using physical destination.”

28. **Q:** Why leave guard page unmapped?  
    **Answer I should say:** “It catches stack overflow and invalid stack access immediately.”  
    **Follow-up:** Why temporary repair? → “Only to observe map-and-retry behavior.”

29. **Q:** Why use sfence.vma?  
    **Answer I should say:** “It removes stale cached translations after PTE change.”  
    **Follow-up:** Without it? → “Retry may still use old mapping/permissions.”

30. **Q:** Difference between filesz and memsz?  
    **Answer I should say:** “filesz is bytes stored in ELF; memsz is total memory region including zero-filled tail.”  
    **Follow-up:** Why retain both? → “Demand paging needs correct file load and BSS zeroing.”

31. **Q:** Why reject sleep input 12abc?  
    **Answer I should say:** “The interface requires one complete decimal duration, not a valid prefix.”  
    **Follow-up:** Overflow? → “Reject before integer arithmetic exceeds range.”

32. **Q:** Why sort pstree by PID?  
    **Answer I should say:** “Stable output independent of process-table slot order.”  
    **Follow-up:** Complexity? → “O(n²) is fine for NPROC 64.”

33. **Q:** Why mark visited before recursion?  
    **Answer I should say:** “It prevents repeated/cyclic records from causing infinite descent.”  
    **Follow-up:** Can real process tree cycle? → “It should not; this is defensive.”

34. **Q:** What does missing parent warning mean?  
    **Answer I should say:** “A record ppid is absent in the snapshot, so I report uncertainty rather than invent hierarchy.”  
    **Follow-up:** Why possible? → “Snapshot does not necessarily contain every historical parent.”

35. **Q:** Why separate parsing from sleep syscall?  
    **Answer I should say:** “Input contract is explicit/testable before kernel call.”  
    **Follow-up:** Kernel validates too? → “Kernel protects itself; program owns UX correctness.”

36. **Q:** What does pipe read do when empty but writers remain?  
    **Answer I should say:** “It blocks waiting for a writer to send bytes.”  
    **Follow-up:** When is EOF? → “Empty and all write ends closed.”

37. **Q:** Why wait after parent receives pong?  
    **Answer I should say:** “Reply proves protocol; wait still reaps child’s termination state.”  
    **Follow-up:** Zombie? → “Terminated process not yet collected by parent.”

38. **Q:** Why check generated syscall files?  
    **Answer I should say:** “A number/stub mismatch can send valid user request to wrong handler.”  
    **Follow-up:** Source of truth? → “syscall.tbl.”

39. **Q:** Why copyin/copyout?  
    **Answer I should say:** “Kernel validates untrusted user virtual addresses during crossing.”  
    **Follow-up:** Direct user pointer? → “Could fault or access forbidden memory in kernel.”

40. **Q:** Why trace filter belongs to process?  
    **Answer I should say:** “Tracing is a process policy; global filter would affect unrelated processes.”  
    **Follow-up:** Fork? → “Copy policy into child.”

41. **Q:** Why filter before lock?  
    **Answer I should say:** “Disabled/nonmatching calls should not pay ring lock cost.”  
    **Follow-up:** Benefit? → “Low overhead when tracing is off.”

42. **Q:** What is ring head?  
    **Answer I should say:** “Next event slot to write.”  
    **Follow-up:** Oldest index? → “head plus capacity minus count, modulo capacity.”

43. **Q:** Why modulo NTRACE?  
    **Answer I should say:** “Circular index wraps fixed ring storage.”  
    **Follow-up:** Why count? → “Distinguishes empty and full state.”

44. **Q:** How is dropped data surfaced?  
    **Answer I should say:** “Supplied checkpoint uses synthetic overflow record containing per-ring loss count.”  
    **Follow-up:** Why no normal PID? → “It describes ring metadata, not one syscall.”

45. **Q:** Why not block trace producer at full ring?  
    **Answer I should say:** “Instrumentation must not stall arbitrary syscall path; loss is reported instead.”  
    **Follow-up:** Trade-off? → “Trace can be incomplete under heavy load.”

46. **Q:** Why timestamp trace events?  
    **Answer I should say:** “To support chronological analysis and potential cross-hart ordering.”  
    **Follow-up:** Strict total order? → “Needs explicit merge policy.”

47. **Q:** Why traceread max <= 0 is error?  
    **Answer I should say:** “Invalid capacity request makes drain semantics ambiguous.”  
    **Follow-up:** Partial buffer? → “Return number actually copied where applicable.”

48. **Q:** Why retain events after producer exits?  
    **Answer I should say:** “Ring is kernel-resident tracing history; reader should still see completed process events.”  
    **Follow-up:** New events? → “Exited process produces none.”

49. **Q:** Why does trace command see child events?  
    **Answer I should say:** “Trace enables filter then forks; child inherits filter before exec workload.”  
    **Follow-up:** Does exec clear it? → “It is process policy, so it remains unless code says otherwise.”

50. **Q:** What is hart ID used for?  
    **Answer I should say:** “It selects local ring and identifies recording hardware context.”  
    **Follow-up:** Is it PID? → “No, many processes can run on one hart.”

51. **Q:** Why fixed-size events?  
    **Answer I should say:** “Predictable bounded memory and fast append.”  
    **Follow-up:** Cost? → “Finite retention capacity.”

52. **Q:** What is low-overhead observability?  
    **Answer I should say:** “Collect useful execution data without excessively changing execution cost.”  
    **Follow-up:** Measurement? → “Compare off, ring, and printf on same workload.”

53. **Q:** What is USYSINFO_VERSION for?  
    **Answer I should say:** “Reader detects incompatible shared layout.”  
    **Follow-up:** Does it stop torn reads? → “No, seq protocol does.”

54. **Q:** Why volatile seq?  
    **Answer I should say:** “Sequence rereads must come from shared memory, not compiler-cached register.”  
    **Follow-up:** Is volatile sufficient? → “No, barriers supply ordering.”

55. **Q:** What must stay consistent in a snapshot?  
    **Answer I should say:** “All published fields must come from one writer state; test relates ctxsw_count to state_gen.”  
    **Follow-up:** Why matters? → “Mixed updates give impossible observations.”

56. **Q:** Where are shared-page updates called?  
    **Answer I should say:** “At process state publication points such as completed syscall count and scheduling/context-switch state.”  
    **Follow-up:** Why there? → “They are points where visible fields change.”

57. **Q:** Why u_getpid has no ecall normally?  
    **Answer I should say:** “It uses u_snapshot page read on compatible fast path.”  
    **Follow-up:** Benefit? → “Avoids privilege transition overhead.”

58. **Q:** Can snapshot be old but valid?  
    **Answer I should say:** “Yes; seqlock guarantees consistency, not instant freshness.”  
    **Follow-up:** Why acceptable? → “Read-mostly observability optimization.”

59. **Q:** Why readers retry instead of take lock?  
    **Answer I should say:** “Readers are frequent and cheap; locking each would defeat fast page access.”  
    **Follow-up:** Bad workload? → “Continuous writers can cause retries.”

60. **Q:** Why must writer finish publication?  
    **Answer I should say:** “Leaving seq odd permanently makes readers retry forever.”  
    **Follow-up:** push_off purpose? → “Protect short local writer operation.”

61. **Q:** What happens on USYSINFO write?  
    **Answer I should say:** “Store page fault from no PTE_W; offending user process is killed.”  
    **Follow-up:** Kernel? → “Kernel remains intact.”

62. **Q:** Why explicit usysinfo free?  
    **Answer I should say:** “High fixed VA is outside ordinary p->sz free range.”  
    **Follow-up:** If omitted? → “One physical page leaks per process.”

63. **Q:** Why remap on exec?  
    **Answer I should say:** “Exec builds new page table, but process still owns its information page.”  
    **Follow-up:** Permissions? → “Same read-only user mapping.”

64. **Q:** Is every page fault fatal?  
    **Answer I should say:** “No; baseline examples kill, but valid lazy/COW/mmap policy can repair.”  
    **Follow-up:** Who decides? → “Kernel PTE plus metadata policy.”

65. **Q:** Why inspect PTE plus scause?  
    **Answer I should say:** “Same access type can be missing mapping or permission violation.”  
    **Follow-up:** Example? → “Guard write versus USYSINFO write.”

66. **Q:** What is PTE_COW?  
    **Answer I should say:** “Software-reserved bit marking temporarily read-only shared COW mapping.”  
    **Follow-up:** Hardware know it? → “No, kernel interprets after store fault.”

67. **Q:** Why reference-count pages for COW?  
    **Answer I should say:** “Kernel needs sharing count for copy/free decisions.”  
    **Follow-up:** Where stored? → “Allocator kernel metadata under allocator synchronization.”

68. **Q:** What does eager fork do?  
    **Answer I should say:** “Allocate/copy every child page immediately.”  
    **Follow-up:** COW benefit? → “Copy only if a process writes.”

69. **Q:** Why baseline sbrk is eager?  
    **Answer I should say:** “It allocates, zeroes, and maps before return, so no first-touch handler is needed.”  
    **Follow-up:** Lazy alternative? → “Record logical range then allocate on valid fault.”

70. **Q:** What does VMA metadata provide?  
    **Answer I should say:** “Valid range, backing file, offset, and access permissions.”  
    **Follow-up:** Offset formula? → “vma.off plus fault_page minus vma.start.”

71. **Q:** Why page-align stval?  
    **Answer I should say:** “PTEs map page units, so repair targets containing page.”  
    **Follow-up:** Direction? → “Round downward.”

72. **Q:** Why check VMA permissions?  
    **Answer I should say:** “Valid range does not authorize every access type.”  
    **Follow-up:** Example? → “Read-only mapping rejects store.”

73. **Q:** Why memsz can exceed filesz?  
    **Answer I should say:** “Executable reserves zero-filled memory beyond file data, such as BSS.”  
    **Follow-up:** Handler? → “Read file portion, zero rest.”

74. **Q:** Why demand exec needs scause 12 handling?  
    **Answer I should say:** “Text instruction fetch may fault before lazy executable page is installed.”  
    **Follow-up:** Current eager behavior? → “Loads text before user execution.”

75. **Q:** Why baseline copyout rejects W=0?  
    **Answer I should say:** “Kernel must honor writable user destination contract.”  
    **Follow-up:** COW exception? → “Break sharing then re-walk PTE.”

76. **Q:** What does satp identify?  
    **Answer I should say:** “Active address translation root/control state.”  
    **Follow-up:** Why inspect? → “Confirms which page table fault belongs to.”

77. **Q:** Why save trap state?  
    **Answer I should say:** “Kernel must return to exact user instruction/register context.”  
    **Follow-up:** Repair case? → “Saved PC stays on faulting instruction.”

78. **Q:** What if repair advances epc?  
    **Answer I should say:** “It skips unfinished memory operation and changes program behavior.”  
    **Follow-up:** Exception? → “System-call ecall return advances deliberately.”

79. **Q:** What is a TLB?  
    **Answer I should say:** “CPU cache of virtual-to-physical translation and permission information.”  
    **Follow-up:** PTE update? → “Fence stale entries.”

80. **Q:** What is ABI?  
    **Answer I should say:** “Binary contract of registers, layouts, calling and return conventions.”  
    **Follow-up:** Lab example? → “a7 syscall number and pstat layout.”

81. **Q:** Why handle partial pipe setup failure?  
    **Answer I should say:** “If second pipe fails, close first pipe descriptors before exiting.”  
    **Follow-up:** Why? → “Correct cleanup on every error path.”

82. **Q:** Why handle fork failure?  
    **Answer I should say:** “No child exists; parent must close resources and report error.”  
    **Follow-up:** Child branch? → “Never executes.”

83. **Q:** Why verify sent token?  
    **Answer I should say:** “It proves read received expected protocol byte, not merely some successful read.”  
    **Follow-up:** Why one byte? → “Minimal deterministic IPC exercise.”

84. **Q:** Why not use sleep for ping-pong ordering?  
    **Answer I should say:** “Pipes supply real synchronization; timing delays are nondeterministic.”  
    **Follow-up:** Benefit? → “Correctness independent of scheduling luck.”

85. **Q:** How does sys_sleep work?  
    **Answer I should say:** “It remembers ticks, sleeps on tick channel, wakes and rechecks until requested duration.”  
    **Follow-up:** Killed process? → “Returns failure.”

86. **Q:** Why root PID parser is positive while sleep may be zero?  
    **Answer I should say:** “A root process identity must be positive; zero ticks is a meaningful immediate sleep.”  
    **Follow-up:** Negative? → “Reject both.”

87. **Q:** Why NPROC matters?  
    **Answer I should say:** “It bounds snapshot data, making simple O(n²) sorting sufficient.”  
    **Follow-up:** Faster sort possible? → “Yes, unnecessary here.”

88. **Q:** Why translate trace syscall numbers to names in user program?  
    **Answer I should say:** “User trace command formats readable names after kernel records cheap numeric data.”  
    **Follow-up:** Why not kernel print? → “Avoid hot-path formatting.”

89. **Q:** Why use r_time timestamp?  
    **Answer I should say:** “It provides lightweight time data for event chronology.”  
    **Follow-up:** Exact total order? → “Not automatically.”

90. **Q:** Why is filter zero special?  
    **Answer I should say:** “It disables tracing so fast path returns immediately.”  
    **Follow-up:** Trace all? → “Bit 63 sentinel in supplied design.”

91. **Q:** Why not read multi-field shared page without sequence?  
    **Answer I should say:** “Different fields could come from different updates.”  
    **Follow-up:** Single word? → “May be simple, but no multi-field invariant.”

92. **Q:** Define torn snapshot.  
    **Answer I should say:** “A returned structure that combines old and new publication state.”  
    **Follow-up:** Lab test? → “Cross-field invariants catch it.”

93. **Q:** Why include seq in copied snapshot?  
    **Answer I should say:** “It records the stable even generation accepted by reader.”  
    **Follow-up:** Current data later? → “Take fresh snapshot.”

94. **Q:** Why PTE_U needed?  
    **Answer I should say:** “Without user permission, user mode cannot read mapping even if PTE_R is set.”  
    **Follow-up:** Why no W? → “Observation only.”

95. **Q:** What is temporary guard repair?  
    **Answer I should say:** “A controlled demand-zero experiment on normally invalid guard address.”  
    **Follow-up:** Keep patch? → “No, revert to preserve guard safety.”

96. **Q:** Real COW versus COW-shaped PTE?  
    **Answer I should say:** “Real COW has two sharers and correct refcount; shaped PTE only creates W=0/COW hardware entry.”  
    **Follow-up:** Why useful? → “Shows mechanism without claiming implementation.”

97. **Q:** Why does valid COW write fault first?  
    **Answer I should say:** “Fault is deliberate trigger to defer private copy until modification.”  
    **Follow-up:** After repair? → “Mapping becomes private writable and store retries.”

98. **Q:** First GDB step at page fault?  
    **Answer I should say:** “Read scause, stval, sepc, and inspect leaf PTE.”  
    **Follow-up:** Then? → “Use VM metadata to choose kill or repair.”

99. **Q:** What if shared page were writable/global?  
    **Answer I should say:** “Processes could corrupt or observe each other’s state; isolation and consistency design collapses.”  
    **Follow-up:** Correct design? → “Per-process physical page, user read-only.”

100. **Q:** One-sentence summary of labs?  
     **Answer I should say:** “They move from safe user–kernel interaction, to low-overhead concurrent observability, to consistent shared-state and VM-fault reasoning.”  
     **Follow-up:** Common thread? → “Respecting privilege, ownership, concurrency, and address-translation boundaries.”


---

# 7. Final cheat sheet

| Concept | One-line definition | One-line viva answer |
|---|---|---|
| Process | Executing program with its own resources/state | “A process owns an address space, descriptors, execution context, and PID.” |
| fork | Create child process | “It duplicates process state and returns different values to parent and child.” |
| exec | Replace program image | “It installs a new program/address space in the current process.” |
| wait | Reap exited child | “It collects child termination and prevents zombie state.” |
| Pipe | One-way kernel byte stream | “Two pipes give clear two-way parent–child communication.” |
| File descriptor | Per-process resource handle | “It is inherited at fork, so unused pipe ends must be closed.” |
| System call | Controlled kernel service entry | “It uses ecall and validates untrusted request before handler.” |
| RISC-V ABI | Register/layout contract | “a7 is syscall number; a0 is first arg and result.” |
| ecall | Instruction that enters supervisor trap | “It is the deliberate user–kernel boundary.” |
| Trap | CPU transfer for syscall/interrupt/exception | “Kernel examines saved state then returns, kills, or repairs.” |
| Page table | VA to PA mapping plus permissions | “It decides translation and whether read/write/execute is legal.” |
| Virtual address | Address visible to process | “It is translated through current process page table.” |
| Page fault | Failed translation or permission access | “PTE plus VM metadata decides illegal versus repairable.” |
| Trace filter | Per-process syscall selection mask | “It prevents unselected calls paying tracing cost.” |
| Hart | Hardware execution context | “A hart runs processes; it is not itself a process.” |
| Ring buffer | Fixed circular FIFO | “It bounds tracing memory and needs explicit full behavior.” |
| Spinlock | Short kernel mutual-exclusion mechanism | “It protects trace producer/consumer state.” |
| Shared page | Same physical page mapped for kernel and user | “It enables kernel-published reads without a syscall.” |
| Seqlock | Optimistic snapshot protocol | “Reader accepts only equal even sequence values around copy.” |
| Memory barrier | Ordering/visibility constraint | “It keeps data writes within odd/even publication brackets.” |
| sfence.vma | Translation cache fence | “It makes changed PTE visible to retried memory access.” |
| COW | Share page until first write | “Clear W plus set COW; first store triggers private copy.” |
| VMA | Kernel metadata for a virtual range | “It tells fault handler whether absent page is valid and how to fill it.” |

---

# 8. Emergency revision modes

## If I have 15 minutes

Memorize exactly these:

1. The three 30-second “What I implemented?” answers for Labs 1, 2, and 3.
2. Pipes: fork inherits all ends; close unused ends; EOF requires all writers closed.
3. Lab 2: syscall hook **after** handler; per-hart ring; full means dropped++; child inherits filter.
4. Lab 3: **odd → barrier → fields → barrier → even**; reader checks twice; page is read-only.
5. Page faults: 12/13/15, stval, sepc, missing versus W=0, repair means no epc advance.

## If I have 30 minutes

Study the 10 facts and 15 questions in each Lab 1–3 revision section. Rehearse aloud:

~~~text
pipes → fork inheritance → close endpoints → EOF → wait
tracing → dispatch hook → after handler → per-hart → overflow → traceread
shared page → per-process mapping → odd/even → barriers → read-only → fallback
~~~

## If I have 1 hour

1. Say each 1-minute implementation answer twice without looking.
2. Walk each execution diagram from start to finish.
3. Answer master-bank questions 1–30 aloud.
4. Explain four functions line by line: parse_ticks, trace_record, usysinfo_update, u_snapshot.
5. Finish with the page-fault decoder and the COW/ordinary-read-only distinction.

## If I have 2 hours

1. Complete the 1-hour plan.
2. Open Lab 1 starter files and explain each TODO’s purpose in your own words.
3. Read trace.c and point to filter check, local ring selection, full condition, lock, and copyout.
4. Read usysinfo.c and ulib.c; defend each seq increment, barrier, and retry.
5. Answer master questions 31–100 by topic, emphasizing anything your instructor repeatedly asks.
6. Recite this one-page cheat sheet with no notes.

---

# Last-minute speaking template

Use this whenever the professor asks about a line of code:

> “This line exists to enforce **[ownership / privilege / consistency / ordering]**. If it is removed, the first thing that breaks is **[specific failure]**. The surrounding mechanism is **[pipe EOF / ring synchronization / seqlock validation / page-table repair]**.”

Examples:

- “I close this descriptor to enforce pipe ownership. Without it an inherited writer can keep the reader from seeing EOF.”
- “I put the second barrier before final even seq to enforce publication ordering. Without it a reader could accept a partially visible update.”
- “I check full ring before writing to enforce bounded loss policy. Without it I would overwrite history or corrupt queue state.”
- “I do not advance epc after repair because the store did not complete. Advancing would skip it.”

## Honest version-specific fallback

If an examiner asks about an exact detail that is absent from the distribution/version in front of you, say:

> “That exact version-specific detail is not recoverable from the supplied distribution I studied. The invariant I can defend is …”

Then state the relevant invariant:

- pipes: ownership and EOF need unused writers closed;
- tracing: local hot-path recording, no torn events, no silent loss;
- shared page: odd/even sequence plus barriers prevents accepted torn snapshots;
- page faults: inspect PTE plus VM metadata, repair then retry the same instruction.
