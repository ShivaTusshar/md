# Computer Networking: A Top-Down Approach
## Beginner's Study Guide — Chapter 3: Transport Layer

This guide walks through Chapter 3 the same way as your Chapters 1–2 guide — every concept explained from scratch, matched to your slide deck's structure.

---

## 3.1 What the Transport Layer Actually Does

Recall from Chapter 1: the network layer (IP) only promises to move data **host-to-host** — it doesn't know or care which specific *application* on that host the data is for. The transport layer's job is to bridge that final gap: it provides **logical communication between application processes** running on different hosts (not between hosts themselves).

**Sender side:** Breaks an application's message into **segments**, hands them to the network layer.
**Receiver side:** Reassembles segments back into a message, hands it up to the correct application.

Two transport protocols exist on the Internet: **TCP** and **UDP**. Neither provides delay guarantees or bandwidth guarantees — those limitations come from the network layer beneath them, which the transport layer can't fix, only work around.

---

## 3.2 Multiplexing and Demultiplexing

This is the mechanism that lets **many applications on one host share the network** — remember from Chapter 2 that a host can run a browser, an email client, and a game all talking to the Internet at once. Something has to sort incoming data to the right one.

- **Multiplexing (sender side):** Gathering data from multiple sockets on the sending host, wrapping each chunk with a **transport header** (later used for demultiplexing), and handing it to the network layer.
- **Demultiplexing (receiver side):** Using the header info in an arriving segment to deliver it to the **correct socket**.

**How the receiving host knows which socket to use:**
Every segment carries a **source port #** and a **destination port #** in its header (32 bits total). The host uses the IP addresses (from the datagram) plus these port numbers to route the segment to the right socket.

### Connectionless demultiplexing (UDP)

A UDP socket is identified by just **2 values**: (destination IP address, destination port #). This means: **any UDP segments arriving with the same destination port, even from completely different source IPs or source ports, all get routed to the same socket.**

### Connection-oriented demultiplexing (TCP)

A TCP socket is identified by a full **4-tuple**: (source IP, source port, destination IP, destination port). This means a TCP server can maintain **separate sockets for every single connected client**, even if they're all talking to the same server port (e.g., port 80) — because their source IP/port combinations differ. This is how a web server serves thousands of different clients at once, all through "port 80," without their data getting mixed up.

**Summary:**

| Protocol | Demux key |
|---|---|
| UDP | Destination port only |
| TCP | Full 4-tuple (src IP, src port, dst IP, dst port) |

---

## 3.3 UDP — User Datagram Protocol

Introduced already in Chapter 2, but here's the full detail.

**Why does UDP even exist**, if TCP does more? Because sometimes "more" gets in the way:
- **No connection establishment** — skips the RTT delay of a handshake.
- **No connection state** kept at sender/receiver — simpler, less memory overhead.
- **Small header size** — less overhead per packet.
- **No congestion control** — UDP can send exactly as fast as the application wants, and keeps working even under network congestion (though this can make congestion worse for everyone else if abused).

**Common uses:** streaming multimedia (tolerates loss, cares about timing), **DNS**, **SNMP**, and **HTTP/3** (which layers its own reliability on top of UDP — see the QUIC section later).

### UDP Segment Structure

```
| source port # | dest port # |
| length        | checksum    |
| application data (payload)  |
```
- **Length** = size of the whole UDP segment (header + data) in bytes.
- **Checksum** = used to detect bit errors introduced during transmission.

### The UDP/Internet Checksum

**Goal:** detect errors (flipped bits) in a transmitted segment.

**How it works (sender):** treat the segment's contents as a sequence of 16-bit integers, add them all together (using **one's complement addition** — any carry-out from the leftmost bit wraps around and gets added back in), and store that sum in the checksum field.

**How it works (receiver):** recompute the same checksum over the received data. If it matches the value in the checksum field → assume no error. If it doesn't match → an error is definitely present.

**Important limitation:** the checksum gives only **weak protection**. It's mathematically possible for two different bit-flip errors to cancel each other out in the addition, producing the *same* checksum as an error-free segment — meaning some errors can slip through completely undetected. This is a known, accepted trade-off for the sake of simplicity and speed.

---

## 3.4 Principles of Reliable Data Transfer (rdt)

This is one of the most conceptually important sections in networking courses — it builds up, step by step, **how you'd design a protocol to reliably deliver data over an unreliable channel**, starting from a perfect channel and adding real-world problems one at a time.

**Setup:** We only look at *unidirectional* data transfer (data flows one way), though control information (ACKs, etc.) flows both ways. Protocols are described using **Finite State Machines (FSMs)** — diagrams where you're always "in a state," an **event** causes a transition, and an **action** happens during that transition.

### rdt1.0 — Perfectly reliable channel

The simplest possible case: assume the underlying channel **never** loses packets or flips bits. The sender just makes a packet and sends it; the receiver just reads it and delivers it. No error-handling machinery needed at all — this is a baseline, not a realistic scenario.

### rdt2.0 — Channel with bit errors (no loss)

Now assume the channel can **flip bits** in a packet (but never loses or reorders anything). A **checksum** lets the receiver detect these errors. The remaining question: what do you *do* about a detected error?

- **ACK (Acknowledgement):** receiver tells sender "I got that packet correctly."
- **NAK (Negative Acknowledgement):** receiver tells sender "That packet had errors, please resend."
- On receiving a NAK, the sender **retransmits**.

This is a **stop-and-wait** protocol: the sender sends exactly one packet, then waits for a response before sending the next.

**The fatal flaw of rdt2.0:** What if the **ACK or NAK itself** gets corrupted on the way back? The sender has no way to know what actually happened at the receiver — it might resend a packet the receiver already got fine, creating a **duplicate**, and the receiver has no way to know whether an arriving packet is fresh data or a duplicate resend.

### rdt2.1 — Fixing corrupted ACKs/NAKs with sequence numbers

The fix: attach a **sequence number** to every packet. Since this protocol only ever has **one packet in flight at a time** (stop-and-wait), only **two sequence numbers (0 and 1)** are ever needed — alternating between them is enough to distinguish "this is the next new packet" from "this is a duplicate of what you already got."

- If the sender's timer expires or it gets a corrupted/negative response, it just **resends the current packet**.
- The receiver checks the sequence number of anything that arrives; if it matches what it already delivered, it **discards** it (but still re-sends an ACK, so the sender eventually hears back).

### rdt2.2 — A NAK-free version

Same functionality as 2.1, but with a small elegant simplification: instead of ever sending an explicit NAK, the receiver **always sends an ACK — for the last correctly-received packet's sequence number.** If the sender sees a **duplicate ACK** (same number as before), that's functionally the same signal as a NAK: "something's wrong, resend." **This is the approach TCP actually uses** — TCP has no NAK mechanism at all.

### rdt3.0 — Channel that can also lose packets

New problem: the channel can now **lose** packets *and* ACKs entirely, not just corrupt them. Sequence numbers and ACKs alone can't fix this — if a packet or its ACK vanishes completely, the sender would wait forever with nothing to trigger a resend.

**Solution: a countdown timer.** The sender waits a "reasonable" amount of time for an ACK; if nothing arrives before the timer expires, it **retransmits**. If the original packet/ACK was just delayed (not truly lost), the retransmission becomes a harmless duplicate — already handled correctly by the sequence-number logic from rdt2.1.

This gives you the full **rdt3.0**, aka **"stop-and-wait with sequence numbers and timeout-triggered retransmission."** It correctly handles corruption, loss, and delay — but it's painfully **inefficient**, because the sender sits idle waiting an entire RTT after every single packet.

### The Performance Problem with Stop-and-Wait

**Utilization (U_sender)** = fraction of time the sender is actually busy transmitting, rather than idly waiting.

$$U_{sender} = \frac{L/R}{RTT + L/R}$$

Worked example from the slides: 1 Gbps link, 15 ms one-way propagation delay, 8000-bit packet.
- Transmission time: d_trans = L/R = 8000 / 10^9 = 8 μs = 0.008 ms
- RTT ≈ 30 ms (15 ms each way)
- U_sender = 0.008 / 30.008 ≈ 0.00027 — the sender is busy only **0.027% of the time**! Almost the entire time is wasted waiting.

### Pipelining — the fix for stop-and-wait's inefficiency

**Pipelining:** allow the sender to have **multiple packets "in flight"** (sent but not yet acknowledged) at once, instead of waiting for each one's ACK individually.

- Requires a **larger range of sequence numbers** (can't just alternate 0/1 anymore).
- Requires **buffering** at the sender and/or receiver, to hold onto packets that haven't been acknowledged yet.
- Example from the slides: pipelining 3 packets at once **triples** the utilization compared to plain stop-and-wait.

Two concrete pipelined protocols follow: **Go-Back-N** and **Selective Repeat**.

### Go-Back-N (GBN)

- The sender maintains a **sliding window** of up to N consecutive unACKed packets it's allowed to have outstanding at once.
- **Cumulative ACK:** an ACK(n) means "I have correctly received everything up through sequence number n" — not just packet n alone.
- The sender keeps **one timer**, for the oldest unACKed packet.
- **On timeout:** the sender resends **packet n and every packet after it currently in the window** — even ones that might have arrived fine! This is the defining (somewhat wasteful) trait of GBN: one lost packet forces retransmission of everything sent after it.
- **Receiver behavior:** very simple — it only ever accepts packets **in order**. Anything out-of-order gets **discarded** (not buffered), and the receiver just re-ACKs the last in-order packet it successfully got, over and over, until the missing one finally arrives correctly.

### Selective Repeat (SR)

A smarter (but more complex) alternative to GBN:

- The receiver **individually acknowledges every correctly-received packet**, even out-of-order ones — and **buffers** them, delivering them to the application only once the gap is filled in and they can go up in the right order.
- The sender keeps a **separate timer for every individual unACKed packet**, and retransmits only that specific packet on timeout — not everything after it, unlike GBN.
- This avoids GBN's wasteful blanket-retransmission behavior, at the cost of needing more memory (buffering) and bookkeeping on both ends.

**The subtle "dilemma" with SR:** if the range of sequence numbers isn't large enough relative to the window size, an old retransmitted packet can be mistaken for a brand-new one by the receiver (since sequence numbers eventually wrap around and repeat). The general rule to avoid this ambiguity: **the sequence number space must be at least twice the window size.**

---

## 3.5 TCP — Connection-Oriented Transport

TCP provides everything UDP doesn't:

- **Point-to-point:** exactly one sender, one receiver per connection (not broadcast/multicast).
- **Reliable, in-order byte stream** — TCP doesn't preserve "message boundaries," it just guarantees the *bytes* arrive in order, correctly.
- **Full duplex:** data can flow in both directions simultaneously over the same connection.
- **MSS (Maximum Segment Size):** the largest chunk of application data TCP will put into a single segment.
- **Cumulative ACKs + pipelining:** like GBN/SR concepts above, but with TCP's own specific rules (detailed below).
- **Connection-oriented:** requires a handshake before any real data moves (see 3.7).
- **Flow controlled:** the sender won't overwhelm the receiver's buffer (see 3.6).
- Congestion controlled, as covered separately (see 3.8 below).

### TCP Segment Structure

Key fields (32-bit-wide rows):
- **Source port # / Destination port #**
- **Sequence number:** counts **bytes** in the overall data stream — not segment numbers! If segment 1 starts at byte 0 and has 500 bytes of data, the next segment's sequence number is 500.
- **Acknowledgement number:** the sequence number of the **next byte the receiver expects** — this is how TCP acknowledges data.
- **Flags:** includes bits like **SYN, FIN, RST** (connection management), **ACK** (this segment carries a valid ACK number), and **C, E** (used for **ECN**, congestion notification, covered in the congestion control section).
- **Receive window (rwnd):** how many more bytes the receiver is currently willing to accept — this is the mechanism behind **flow control**.
- **Checksum:** same idea as UDP's.

### TCP Sequence Numbers and ACKs, Illustrated

Simple example (a "telnet" scenario) from the slides:
1. User types 'C'. Host A sends: `Seq=42, ACK=79, data='C'`.
2. Host B ACKs receipt and *also* echoes the character back: `Seq=79, ACK=43, data='C'`.
3. Host A ACKs the echoed character: `Seq=43, ACK=80`.

Notice how the ACK number is always "one more than the last byte successfully received" — that's the cumulative-ACK convention.

### Estimating RTT and Setting the Timeout

TCP needs to guess a reasonable timeout value, but RTT is never fixed — it fluctuates constantly with network conditions.

- **SampleRTT:** the actual measured time between sending a segment and receiving its ACK (ignoring retransmitted segments, since a resent packet makes it ambiguous which transmission the ACK is actually responding to).
- Because a single SampleRTT is noisy, TCP smooths it using an **Exponentially Weighted Moving Average (EWMA)**:

EstimatedRTT = (1 − α) × EstimatedRTT + α × SampleRTT

- Typical value: **α = 0.125**. This gives recent samples more weight while still smoothing out noise — older samples' influence fades exponentially.

**Setting the timeout itself** needs a safety margin on top of the estimate, sized according to how *variable* recent RTTs have been:

TimeoutInterval = EstimatedRTT + 4 × DevRTT

DevRTT = (1 − β) × DevRTT + β × |SampleRTT − EstimatedRTT|

- Typical value: **β = 0.25**. DevRTT is essentially a smoothed measure of how much SampleRTT tends to deviate from the estimate — if RTT has been jumping around a lot recently, the timeout gets a bigger safety cushion.

### TCP Sender and Receiver Behavior (Simplified)

**Sender events:**
- **Data received from application** → create a segment (sequence number = byte-stream number of its first byte), start the timer if it isn't already running.
- **Timeout** → retransmit the segment that caused the timeout, restart the timer.
- **ACK received** → if it acknowledges previously-unACKed data, update what's considered ACKed, and restart the timer if there's still unACKed data outstanding.

**Receiver ACK-generation rules (from RFC 5681):**

| Event | Receiver action |
|---|---|
| In-order segment arrives; nothing else pending | **Delayed ACK** — wait up to 500ms for the next segment; if none comes, send ACK anyway |
| In-order segment arrives; another ACK already pending | Send **one** cumulative ACK covering both |
| Out-of-order segment arrives (gap detected) | Send an **immediate duplicate ACK**, naming the next expected byte |
| A segment arrives that fills (part of) the gap | Send an **immediate ACK** |

### TCP Retransmission Scenarios (worth knowing for exams)

- **Lost ACK scenario:** Data arrives fine at the receiver, but the ACK gets lost on the way back. Sender's timer expires → it retransmits → receiver sees this retransmission, realizes (via sequence number) it's a duplicate of what it already has, and just re-sends the cumulative ACK.
- **Premature timeout:** The timer fires just *before* an ACK that was actually already on its way arrives. The sender needlessly resends, but because TCP uses cumulative ACKs, the eventual next ACK simply covers everything — no real harm done, just some wasted bandwidth.
- **Cumulative ACK covering an earlier lost ACK:** If ACK(100) is lost but a later ACK(120) arrives covering more data, the sender still learns everything it needed to know — no separate retransmission required, because the cumulative nature of TCP's ACKs makes an isolated lost ACK harmless as long as a *later* one gets through.

### TCP Fast Retransmit

Rather than always waiting for a timeout (which can be a long, slow-to-detect event), TCP has a faster loss-detection shortcut:

**If the sender receives 3 duplicate ACKs for the same data** ("triple duplicate ACK"), it assumes that segment was almost certainly lost — because getting the *same* ACK repeatedly usually means multiple *later* segments have arrived successfully, but a gap remains before them. Rather than wait for the timer, TCP **immediately retransmits** the missing segment. This is called **fast retransmit**, and it's much quicker than waiting for a full timeout in most real loss scenarios.

---

## 3.6 TCP Flow Control

**Important — don't confuse this with congestion control** (that comes next): flow control protects the **receiver**, not the network.

**The problem:** if the network delivers data to a receiving host faster than the receiving *application* actually reads it out of its socket buffer, that buffer can overflow, and incoming data would have to be dropped.

**The mechanism:** the TCP receiver continuously **advertises its free buffer space** using the **rwnd (receive window)** field in every segment it sends. The sender is then required to limit the amount of **unACKed ("in-flight") data** it has outstanding to no more than this advertised rwnd value. This guarantees the receiver's buffer physically cannot overflow, because the sender is never allowed to have more data in transit than the receiver has confirmed room for.

---

## 3.7 TCP Connection Management

### Why not a simple 2-way handshake?

You might think "just send a request, get an acknowledgment back, done" would work. But the slides walk through why a **2-way handshake fails** on a real, unreliable network:
- Messages can be **delayed**.
- Messages can be **retransmitted** due to perceived loss.
- Messages can arrive **reordered**.
- Neither side can directly "see" what state the other side is actually in.

Concretely: if a connection request gets retransmitted (because the original was just delayed, not lost), the server might accept a **duplicate** connection, or the client might think a connection succeeded while the server has already forgotten about it entirely — leading to **half-open connections** or **duplicate data being accepted**. A 2-way handshake simply doesn't have enough back-and-forth to rule these scenarios out.

### The TCP 3-Way Handshake (the actual solution)

```
Client (SYN_SENT)                    Server (LISTEN)
      |------ SYN, seq=x ------------------>|
      |                                 (SYN_RCVD)
      |<---- SYN, seq=y, ACK=x+1 -----------|
   (ESTAB)                                    |
      |------ ACK, ACK=y+1 ---------------->|
      |                                  (ESTAB)
```

1. **Client → Server: SYN** — client picks a random **initial sequence number x**, sends a segment with the SYN bit set.
2. **Server → Client: SYNACK** — server picks its **own initial sequence number y**, and acknowledges the client's SYN by sending back ACK number x+1, with its own SYN bit also set.
3. **Client → Server: ACK** — client acknowledges the server's SYN (ACK = y+1). This final segment **may already carry actual application data**, since the client now knows the server is genuinely alive and ready.

Each side only moves to the **ESTAB (established)** state once it has concrete proof the *other* side is alive and has agreed on the connection's starting sequence numbers — which is exactly the guarantee a 2-way handshake couldn't provide.

### Closing a TCP Connection

Either side can independently close its half of the connection by sending a segment with the **FIN bit = 1**. The other side responds with an **ACK** (which can be combined with its own FIN if it's also ready to close). Because both directions of a full-duplex connection have to be closed, and this can happen at different times or **simultaneously** by both sides, TCP has a small set of intermediate states (like FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, LAST_ACK, TIMED_WAIT) purely to correctly track this shutdown handshake and avoid mistaking a stray, delayed old segment for a new connection later.

---

## 3.8 Principles of Congestion Control

*(You've already covered this in detail earlier in this conversation — here's the condensed version, tied back into the rest of the chapter.)*

**Congestion:** informally, "too many sources sending too much data too fast for the network to handle." Manifests as long queuing delays and packet loss at routers. **This is fundamentally different from flow control** — congestion control protects the *network*; flow control protects one *receiver*.

**Approaches to congestion control:**
- **End-end congestion control:** no explicit help from the network — the sender simply **infers** congestion from observed loss and delay. **This is TCP's approach.**
- **Network-assisted congestion control:** routers directly provide feedback to the hosts (e.g., marking a congestion bit) — used by mechanisms like **ECN**, older ATM networks, and DECbit.

### TCP's Congestion Control: AIMD

TCP's core algorithm: **Additive Increase, Multiplicative Decrease (AIMD)**.
- **Additive increase:** as long as things are going well, increase the sending rate by 1 MSS every RTT — a slow, cautious probe upward.
- **Multiplicative decrease:** the instant a loss event is detected, **cut the sending rate in half**.

This creates the classic **AIMD sawtooth** pattern we visualized earlier.

**Detail on multiplicative decrease:**
- Loss detected via **triple duplicate ACK** → cut cwnd in **half** (this variant is called **TCP Reno**).
- Loss detected via **timeout** → cut cwnd all the way down to **1 MSS** (this stricter variant is called **TCP Tahoe**) — a timeout is treated as a much more severe signal than a triple-dup-ACK, since it usually means things have gotten really bad.

**Why AIMD specifically (and not some other increase/decrease scheme)?** It's been mathematically shown to: (1) optimize congested flow rates fairly across the whole network, and (2) have good **stability** properties as a fully **distributed, asynchronous** algorithm — meaning no sender needs to coordinate with any other sender; the system self-organizes.

### TCP Congestion Control States: Slow Start, Congestion Avoidance, Fast Recovery

**Slow start:** at the very beginning of a connection, cwnd starts at 1 MSS and **doubles every RTT** (done in practice by incrementing cwnd by 1 for every single ACK received) — this ramps up exponentially fast, despite the name.

**Transition point — ssthresh:** slow start's exponential growth can't continue forever, or it would blow right past the network's actual capacity. TCP tracks a variable called **ssthresh (slow start threshold)**. When cwnd reaches ssthresh, TCP switches from exponential growth to the much more cautious **linear** additive-increase behavior of **congestion avoidance**. On any loss event, **ssthresh is reset to half of cwnd's value at the moment of the loss** — this becomes the new ceiling for the next round of slow start.

**Fast recovery:** a state TCP enters specifically after a **triple duplicate ACK** (not a full timeout) — since duplicate ACKs actually indicate *some* data is still getting through, TCP treats this as a milder problem than a full timeout and recovers somewhat more gently, temporarily inflating cwnd for each further duplicate ACK before dropping back into congestion avoidance once a fresh ACK arrives.

### TCP CUBIC — A Modern Improvement Over Classic AIMD

**The question CUBIC asks:** is a straight-line AIMD really the smartest way to "probe" for available bandwidth after a loss?

**CUBIC's insight:** call **Wmax** the window size at which the last congestion loss was detected. The intuition is that the network's actual congestion state probably **hasn't changed dramatically** since then — so after cutting the window in half following a loss, it makes sense to ramp back up toward Wmax **quickly** at first (since we have good reason to believe that region is still safe), then approach Wmax **more cautiously/slowly** as we get close to it again (since that's exactly where the loss happened last time).

- This growth follows a **cubic function** of the time elapsed since the last loss, hence the name.
- A parameter **K** marks the point in time when the window is expected to reach Wmax again — growth is aggressive when far from K in time, and cautious as it approaches K.
- **TCP CUBIC is the default congestion control algorithm on Linux**, and is the most widely used TCP variant on popular web servers today.

### The Congested "Bottleneck Link" Concept

Classic TCP (and CUBIC) increase their sending rate until packet loss occurs somewhere along the path — specifically at the router whose outgoing link is the **bottleneck** (the slowest, most heavily-used link on the route). Two connected insights:
- Increasing TCP's sending rate **beyond** what a congested bottleneck link can handle will **not** increase actual end-to-end throughput — the bottleneck caps it regardless.
- Increasing the sending rate **will**, however, increase the **measured RTT** — because packets pile up waiting in the bottleneck router's queue before even reaching the point of loss.

**The stated goal of good congestion control:** *"keep the end-to-end pipe just full, but not fuller."* — use all the available capacity, but avoid needlessly stuffing extra data into queues that only adds delay without adding real throughput.

### Delay-Based TCP Congestion Control

An alternative philosophy to classic loss-based AIMD: **avoid inducing loss in the first place**, by watching **delay** as an early-warning signal instead of waiting for an actual dropped packet.

- **RTT_min** = the minimum RTT ever observed on this connection (essentially, the RTT when the path is *not* congested at all).
- **Uncongested throughput estimate** = cwnd / RTT_min (what throughput *should* be, if there's no queuing delay happening).
- **Measured throughput** = actual bytes sent in the last RTT interval, divided by the actually-measured RTT.

**Logic:**
- If measured throughput is **very close** to the uncongested estimate → the path isn't congested → increase cwnd (linearly).
- If measured throughput is **far below** the uncongested estimate → delay is creeping up, meaning queues are filling → the path is becoming congested → decrease cwnd (linearly), *before* any actual packet loss even happens.

**Google's BBR** is a well-known deployed example of this delay-based philosophy, used on Google's internal backbone network.

### TCP Fairness

**Fairness goal:** if K TCP connections all share one bottleneck link of total capacity R, each connection should, on average, get a rate of **R/K**.

**Is TCP actually fair?** Yes — **but only under idealized assumptions**: all connections have the **same RTT**, and all are simultaneously in the **congestion avoidance** phase (not slow start or recovery). Under AIMD, two competing connections mathematically converge toward the equal-share line over successive rounds of additive-increase/multiplicative-decrease — a nice geometric property of the algorithm.

**Where "fairness" gets complicated in practice:**
- **UDP-based apps** (like real-time multimedia) often skip TCP entirely specifically to *avoid* being throttled by congestion control — there's no "Internet police" enforcing fair congestion behavior on every application.
- **Parallel TCP connections:** an application (like a web browser) can simply open **multiple** TCP connections at once to the same server, effectively grabbing a larger overall share of a bottleneck link than a "fair" single connection would get. Example from the slides: on a link with 9 existing connections, a new app requesting just 1 more TCP connection gets only R/10 of the link — but if it instead opens 11 parallel connections, it ends up getting roughly R/2 of the link's total capacity, at the other apps' expense.

---

## 3.9 Evolving Transport-Layer Functionality: QUIC

TCP and UDP have been the Internet's two workhorse transport protocols for **over 40 years**, but newer scenarios have exposed some of TCP's limitations:

| Scenario | Challenge for classic TCP |
|---|---|
| Long, "fat" pipes (huge transfers) | Many packets in flight; a single loss can stall the whole pipeline |
| Wireless networks | Loss from noise/mobility gets mistakenly treated as *congestion* loss |
| Long-delay links | Extremely long RTTs slow everything down |
| Data center networks | Extremely latency-sensitive; classic TCP timing assumptions don't fit |
| Background traffic | Needs to stay deliberately low-priority |

The modern response, especially for the Web, has been to move some transport-layer functionality **up into the application layer, running on top of UDP** — this is exactly what **QUIC** does.

### QUIC: Quick UDP Internet Connections

QUIC is technically an **application-layer protocol built on top of UDP**, but it re-implements many ideas we just studied in this very chapter: reliable data transfer, connection establishment, and congestion control — just moved out of the OS-level TCP stack and into (often) the browser/application itself.

**Protocol stack comparison:**
```
Classic:  HTTP/2 -> TLS -> TCP -> IP            (HTTP/2 over TCP)
Modern:   HTTP/2 (slimmed) -> QUIC -> UDP -> IP  (HTTP/3, i.e., HTTP/2 over QUIC over UDP)
```

**Key QUIC features:**
- **Multiple application-level "streams"** multiplexed over a single QUIC connection, each with independent reliable-delivery and error handling — this is what solves **HOL (Head-of-Line) blocking**: in old HTTP/1.1 over TCP, a single lost/corrupted packet holds up *everything* behind it on that one shared TCP connection, even data belonging to a completely different, otherwise-unaffected request. QUIC's separate per-stream reliability means a lost packet only stalls its own stream, not the others.
- **Common congestion control** across all streams, deliberately designed in ways that mirror well-known TCP algorithms (the QUIC spec itself explicitly compares its congestion behavior to TCP's).
- **Faster connection establishment:** classic HTTPS-over-TCP needs **two separate, serial handshakes** — first the TCP handshake (for reliability/congestion state), then a separate TLS handshake (for encryption/authentication) on top of it. QUIC **combines reliability, congestion control, authentication, and encryption setup into a single handshake**, cutting connection setup time roughly in half — often achievable in just **1 RTT**.
- QUIC is deployed widely by Google (Chrome, the YouTube mobile app, many Google servers) and now forms the basis of **HTTP/3**.

---

## 3.10 Chapter 3 — Key Takeaways

- The transport layer provides **process-to-process** communication (via port numbers), sitting between applications and the network layer.
- **Multiplexing/demultiplexing:** UDP uses just the destination port; TCP uses the full 4-tuple, allowing many simultaneous client connections on one server port.
- **UDP** is connectionless, unreliable, and "no-frills" — fast and simple, with only a (weak) checksum for error detection.
- **Reliable data transfer (rdt)** is built up incrementally: checksums handle corruption, sequence numbers + ACKs handle duplicates, and timers handle loss — together forming rdt3.0 (stop-and-wait). **Pipelining** (via Go-Back-N or Selective Repeat) is needed to make this efficient.
- **TCP** provides reliable, in-order, full-duplex byte-stream delivery, using cumulative ACKs, smart RTT/timeout estimation (EWMA), and **fast retransmit** (on triple duplicate ACK) as a shortcut around slow timeouts.
- **Flow control** (via the `rwnd` field) protects the *receiver's buffer*; it is NOT the same thing as congestion control.
- The **3-way handshake** (not 2-way) is required to reliably establish a TCP connection over an unreliable network; closing uses a **FIN/ACK** exchange on each side.
- **Congestion control** protects the *network* itself, using **AIMD** (the sawtooth pattern), with **slow start** (exponential growth) transitioning to **congestion avoidance** (linear growth) at `ssthresh`. **TCP CUBIC** (Linux's default) improves on plain AIMD by adapting its growth curve around the previous loss point, `Wmax`. **Delay-based** approaches (like Google's BBR) try to avoid loss entirely by watching RTT instead.
- TCP is fair **only under idealized, matched-RTT, same-phase conditions** — real-world tricks like opening parallel connections can break that fairness.
- **QUIC** (underlying HTTP/3) moves reliability, congestion control, and security into a single UDP-based application-layer protocol, solving TCP's head-of-line blocking problem and cutting connection setup to a single handshake.

---

*This guide summarizes and explains the material in Kurose & Ross, "Computer Networking: A Top-Down Approach," 8th Edition, Chapter 3, based on your uploaded slide decks. Refer back to the original slides/FSM diagrams for the full step-by-step animations.*
