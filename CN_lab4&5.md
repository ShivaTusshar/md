# Computer Networking: A Top-Down Approach
## Beginner's Study Guide — Chapter 4 (Network Layer: Data Plane) & Chapter 5 (Network Layer: Control Plane, up to 5.6)

Same format as your earlier guides: every term is explained from scratch, and the order follows your slide decks.

**Quick map of the two chapters:**
- **Chapter 4 (data plane)** = what a router does with *one packet*, right now, in nanoseconds (look at the header, pick an output port).
- **Chapter 5 (control plane)** = how routers *figure out* which output port is correct (routing algorithms, OSPF, BGP) and how they report problems (ICMP).

---

# CHAPTER 4: The Network Layer — Data Plane

## 4.1 Overview of the Network Layer

The transport layer (Chapter 3) hands a segment to the network layer. The network layer's job is to move that segment from the **sending host to the receiving host**, across many routers.

- **Sender side:** puts the segment inside a **datagram** (an IP packet) and passes it to the link layer.
- **Receiver side:** extracts the segment and hands it up to the transport layer.
- **Routers:** look at the header of every datagram passing through and move it from an **input port** to the right **output port**.

Every Internet device (hosts *and* routers) has a network layer. Routers only go up to layer 3; they don't look inside at TCP/UDP.

### Two key functions: forwarding and routing

| | Forwarding | Routing |
|---|---|---|
| What | Move a packet from a router's input link to the correct output link | Decide the end-to-end path packets should take |
| Scope | One router, one packet | The whole network |
| Speed | Nanoseconds | Seconds/milliseconds |
| Analogy | Getting through **one interchange** on a road trip | **Planning the whole trip** before you leave |

### Data plane vs. control plane

- **Data plane:** local, per-router function. It decides how a datagram that arrived on an input port is forwarded to an output port, using values in the packet header. (This is Chapter 4.)
- **Control plane:** network-wide logic that decides the routes. (This is Chapter 5.)

Two ways to build the control plane:
1. **Per-router (traditional):** a routing algorithm runs in *every* router; they talk to each other and each builds its own **forwarding table**.
2. **SDN (Software-Defined Networking):** a **remote controller** (a server) computes the forwarding tables and installs them in the routers. Routers just follow the tables.

The forwarding table is the link between the two planes: the **control plane writes it, the data plane reads it.**

### Network service model

What guarantees could the network give for moving datagrams? Possible services: guaranteed delivery, guaranteed delay (e.g., under 40 ms), in-order delivery, guaranteed minimum bandwidth, limits on changes in inter-packet spacing.

| Architecture | Service model | Bandwidth | Loss | Order | Timing |
|---|---|---|---|---|---|
| **Internet** | **Best effort** | none | no | no | no |
| ATM | Constant Bit Rate | constant rate | yes | yes | yes |
| ATM | Available Bit Rate | guaranteed minimum | no | yes | no |
| Internet | Intserv (RFC 1633) | yes | yes | yes | yes |
| Internet | Diffserv (RFC 2475) | possible | possibly | possibly | no |

**Best effort** means the Internet promises *nothing*: no guaranteed delivery, order, timing, or bandwidth. It just tries its best.

**Why best effort won (from the slides):**
- Its simplicity let the Internet be widely deployed.
- With enough bandwidth provisioned, real-time apps (voice, video) are "good enough" most of the time.
- Replicated services (data centers, CDNs) sit close to users.
- Congestion control (Chapter 3) helps elastic services.

---

## 4.2 What's Inside a Router?

### Router architecture

Two parts working at very different speeds:
- **Control plane (routing processor)** — software, works in the *millisecond* range: runs routing protocols, manages the router.
- **Data plane (input ports → switching fabric → output ports)** — hardware, works in the *nanosecond* range: forwards each packet.

### Input port functions

An input port does three things in order:
1. **Physical layer:** receive bits.
2. **Link layer:** e.g., Ethernet (Chapter 6).
3. **Lookup & forwarding:** use header fields to find the output port in the forwarding table (a **"match plus action"** idea: *match* the header, *act* by sending to a port).

Goal: finish at **line speed** (as fast as packets arrive). If packets arrive faster than the switching fabric can take them, they **queue at the input port.**

- **Destination-based forwarding:** decide using only the destination IP address (traditional).
- **Generalized forwarding:** decide using *any* header fields (this is the idea behind SDN/OpenFlow).

### Longest prefix matching

A forwarding table can't list every individual address, so it lists **ranges (prefixes)**. When more than one entry matches, the router uses the **longest matching prefix.**

Example table from your slides:

| Destination range | Link |
|---|---|
| 11001000 00010111 **00010** \*\*\* \*\*\*\*\*\*\*\* | 0 |
| 11001000 00010111 **00011000** \*\*\*\*\*\*\*\* | 1 |
| 11001000 00010111 **00011** \*\*\* \*\*\*\*\*\*\*\* | 2 |
| otherwise | 3 |

- Address `11001000 00010111 00010110 10100001` → matches only the first range (prefix `00010`) → **interface 0.**
- Address `11001000 00010111 00011000 10101010` → matches *both* the second (24-bit prefix) and third (21-bit prefix) → the **longer (24-bit) wins → interface 1.**

Hardware trick: routers do this lookup in **TCAMs (Ternary Content Addressable Memory)**, which return a match in **one clock cycle regardless of table size.**

### Switching fabrics

The **switching fabric** moves a packet from an input port to the right output port. Ideal switching rate for N inputs each at rate R: **N × R** (so the fabric is never the bottleneck).

Three types:

| Type | How it works | Limitation |
|---|---|---|
| **Memory** (1st generation) | Packet is copied into the CPU's memory, then to the output. A traditional computer under CPU control. | Speed limited by memory bandwidth (2 bus crossings per datagram) |
| **Bus** | Input port sends the packet over a **shared bus** straight to the output port. | **Bus contention:** only one packet at a time; limited by bus bandwidth (e.g., 32 Gbps in a Cisco 5600, enough for access routers) |
| **Interconnection network** (crossbar, Clos) | A grid of smaller switches lets many packets cross at once. Packets can be chopped into fixed-length **cells**, switched in parallel, and reassembled at the output. | Complex, but scales to hundreds of Tbps (e.g., Cisco CRS uses 8 parallel switching planes) |

### Input port queuing and HOL blocking

If the fabric is slower than all inputs combined, packets queue at the input — causing delay and even **loss from input buffer overflow.**

**Head-of-the-Line (HOL) blocking:** two inputs want the *same* output port, so only one packet can go. The packet that lost is stuck at the front of its queue and **blocks the packets behind it** — even ones headed to free outputs.

### Output port queuing

**Buffering is needed at the output port** when packets arrive through the fabric faster than the output link can transmit (the slides mark this as "a really important slide").
- If buffers fill up, packets are **dropped** (loss due to congestion).
- A **drop policy** decides which packet to drop.
- A **scheduling discipline** decides which queued packet to send next.

### How much buffering?

- **RFC 3439 rule of thumb:** buffer ≈ typical **RTT × link capacity C**. Example: RTT = 250 ms, C = 10 Gbps → 2.5 Gbit of buffer.
- **More recent recommendation** with N flows: buffer ≈ **RTT × C / √N** (far smaller, because many independent flows average out).
- **Too much buffering is bad too:** it increases delay ("bufferbloat"), especially in home routers → sluggish TCP, poor real-time performance. This links back to delay-based congestion control: keep the bottleneck "just full, but no fuller."

### Buffer management

- **Drop:** which packet to drop when full. **Tail drop** = drop the arriving packet. **Priority** = drop based on priority.
- **Marking:** mark packets to signal congestion before dropping (**ECN, RED**).

### Packet scheduling (which packet goes next?)

| Policy | How it works |
|---|---|
| **FCFS / FIFO** | First come, first served — transmit in arrival order (like a queue at a ticket counter). |
| **Priority** | Packets are classified; always send from the **highest-priority non-empty queue** first. FCFS within a class. Risk: low priority can starve. |
| **Round Robin (RR)** | Cycle through the class queues, sending **one complete packet** from each class in turn (if available). |
| **Weighted Fair Queuing (WFQ)** | Generalized RR. Class *i* has weight *wᵢ* and gets a share of **wᵢ / Σwⱼ** of the service each cycle → gives each class a **minimum bandwidth guarantee.** |

### Sidebar: Network neutrality

- **Technical meaning:** how an ISP shares its resources (scheduling and buffer management are the tools).
- **Social/economic meaning:** protecting free speech, encouraging innovation and competition.
- **2015 US FCC rules ("bright lines"):** **no blocking** lawful content, **no throttling** lawful traffic, **no paid prioritization.**
- The legal fight hinges on whether an ISP is a **telecommunications service** (Title II: "common carrier", non-discrimination, regulated) or an **information service** (Title I: lightly regulated).

---

## 4.3 The Internet Protocol (IP): IPv4, Addressing, IPv6

### IP datagram format (IPv4)

Key header fields (each row is 32 bits):

| Field | Meaning |
|---|---|
| **Version** | IP version (4) |
| **Header length** | Needed because of variable-length options |
| **Type of service** | DiffServ (bits 0–5) + ECN (bits 6–7) |
| **Total length** | Whole datagram length in bytes (max 64 KB; typically ≤ 1500) |
| **16-bit identifier, flags, fragment offset** | Used for fragmentation/reassembly |
| **TTL (Time to Live)** | Remaining max hops; **decremented at every router**; at 0 the datagram is discarded (prevents endless looping) |
| **Upper-layer protocol** | Which protocol gets the payload (e.g., TCP or UDP) |
| **Header checksum** | Error detection on the header |
| **Source / Destination IP address** | 32 bits each |
| **Options** | e.g., timestamp, record route (if any) |
| **Payload** | Typically a TCP or UDP segment |

**Overhead:** 20 bytes of TCP + 20 bytes of IP = **40 bytes** per packet, plus any application-layer overhead.

### IPv4 addressing basics

- **IP address:** a **32-bit** identifier for each host or router **interface.**
- **Interface:** the connection between a host/router and a physical link. Routers have several interfaces; a host usually has one or two (e.g., Ethernet and WiFi). So **an IP address belongs to an interface, not to a device.**
- **Dotted-decimal notation:** each byte written in decimal, e.g., `223.1.1.1 = 11011111 00000001 00000001 00000001`.

### Subnets

A **subnet** is a group of interfaces that can reach each other **without passing through a router.**

An IP address has two parts:
- **Subnet part** (high-order bits) — same for all devices in the subnet.
- **Host part** (remaining low-order bits) — identifies the device.

**Recipe to find subnets:** detach every interface from its host or router, creating isolated "islands"; each island is a subnet. A **subnet mask** like **/24** says the high-order **24 bits** are the subnet part (e.g., subnet `223.1.1.0/24`).

### CIDR (Classless InterDomain Routing)

The subnet part can have **any length**. Format: **a.b.c.d/x**, where x = number of bits in the subnet part.

Example: `200.23.16.0/23` → first 23 bits are the subnet part, last 9 bits are the host part (so 2⁹ = 512 addresses).

### How does a host get an IP address? → DHCP

(Two separate questions: how a *host* gets an address within its network, and how a *network* gets its block. Hosts: hard-coded by an admin, or **DHCP**.)

**DHCP (Dynamic Host Configuration Protocol):** a "plug-and-play" protocol where a host dynamically obtains an address from a server when it joins the network. Benefits: addresses can be **renewed (leased)**, **reused** after a host leaves, and it supports mobile users.

**The 4-step exchange:**
1. **DHCP discover** — client *broadcasts* "is there a DHCP server out there?" (src `0.0.0.0:68`, dest `255.255.255.255:67`).
2. **DHCP offer** — server responds "I'm a DHCP server, here's an IP you can use" (includes the address and a lease time, e.g., 3600 s).
3. **DHCP request** — client says "OK, I'd like to use this address."
4. **DHCP ACK** — server confirms "you've got that address."

(Steps 1–2 can be skipped if the client remembers and wants to reuse its old address.)

**DHCP gives more than an IP address:** the **first-hop router** address, the **DNS server** name/address, and the **network mask.**

**Encapsulation (the slide example):** a DHCP message goes inside **UDP**, inside **IP**, inside an **Ethernet frame**, which is **broadcast** (dest MAC FF:FF:FF:FF:FF:FF) on the LAN. Typically the DHCP server is built into the router.

### How does a network get its subnet block? → from its ISP

An ISP owns a block (e.g., `200.23.16.0/20`) and splits it among customers:

| Organization | Block |
|---|---|
| ISP's block | 200.23.16.0/20 |
| Org 0 | 200.23.16.0/23 |
| Org 1 | 200.23.18.0/23 |
| Org 2 | 200.23.20.0/23 |
| … | … |
| Org 7 | 200.23.30.0/23 |

(A /20 split into /23s gives 2^(23−20) = **8 blocks.**)

### Hierarchical addressing and route aggregation

Because all those blocks share the same first 20 bits, the ISP can tell the whole Internet just **one thing**: *"Send me anything beginning with 200.23.16.0/20."* This is **route aggregation** — it keeps routing tables small.

**More-specific routes:** if Organization 1 switches to another ISP (ISPs-R-Us), that ISP advertises the *more specific* prefix `200.23.18.0/23`. Thanks to **longest prefix matching**, routers send Org 1's traffic to ISPs-R-Us, while everything else in `200.23.16.0/20` still goes to the old ISP.

### Who allocates blocks? ICANN

**ICANN** (Internet Corporation for Assigned Names and Numbers) allocates addresses through **5 regional registries**, and manages the DNS root zone. The last chunk of IPv4 space was handed out in **2011.** Two answers to address shortage: **NAT** and **IPv6.**

### NAT (Network Address Translation)

All devices in a local network (home, office) share **one public IPv4 address** as seen from the outside.

- Inside, devices use **private addresses** (`10/8`, `172.16/12`, `192.168/16`) that are only valid locally.
- Datagrams leaving the network all carry the **same source IP (the NAT router's address)** but **different source port numbers.**

**How the NAT router works:**
- **Outgoing:** replace (source IP, port) with (NAT IP, **new port**), and **record the pair in the NAT translation table.**
- **Incoming:** look up the destination (NAT IP, port) in the table and replace it with the original (internal IP, port).

**Slide example:** host `10.0.0.1:3345` sends to `128.119.40.186:80`. NAT rewrites the source to `138.76.29.7:5001` and stores `138.76.29.7,5001 ↔ 10.0.0.1,3345`. The reply to `138.76.29.7:5001` is translated back to `10.0.0.1:3345`.

**Advantages:** needs only one ISP address; internal addresses can change freely; you can switch ISPs without renumbering; outside hosts can't directly address or see inside devices (a security benefit).

**Criticisms:** routers "should" only process up to layer 3; NAT violates the **end-to-end argument** (it manipulates port numbers); **NAT traversal** is hard (a client can't easily connect to a server behind NAT). Still, NAT is everywhere (homes, offices, 4G/5G).

### IPv6

**Motivation:** the 32-bit space runs out; also faster processing and better treatment of "flows."

**IPv6 header:** a **fixed 40-byte header**, **128-bit addresses**. Fields: version, priority, **flow label**, payload length, **next header**, **hop limit** (replaces TTL), source and destination addresses.

**What IPv6 removed (vs. IPv4):**
- **No header checksum** (faster processing at routers)
- **No fragmentation/reassembly**
- **No options** (they move to an upper-layer "next header")

### Transition from IPv4 to IPv6: tunneling

Not all routers can upgrade at once (**no "flag days"**). Solution: **tunneling** — carry the **IPv6 datagram as the payload inside an IPv4 datagram** while it crosses IPv4-only routers ("a packet within a packet"). At the far end of the tunnel, the IPv6 datagram is extracted and continues. (Tunneling is also used heavily in 4G/5G.)

Adoption is slow: about **30%** of Google's clients use IPv6, and it has taken **25+ years and counting.**

### IP fragmentation and reassembly (additional slides)

Links have different **MTUs** (maximum transfer unit = largest link-layer frame). A large datagram is **fragmented** inside the network and **reassembled only at the destination.** Header fields **identifier, flags, and offset** tell the destination how to put the pieces back.

**Worked example (from the slides):** a **4000-byte** datagram over a link with **MTU = 1500.**
- IP header = 20 bytes, so data = 3980 bytes. Each fragment can carry at most **1480 bytes** of data (1500 − 20).

| Fragment | Length | ID | Fragflag | Offset |
|---|---|---|---|---|
| 1 | 1500 | x | 1 | 0 |
| 2 | 1500 | x | 1 | 185 (= 1480/8) |
| 3 | 1040 | x | 0 (last) | 370 (= 2960/8) |

- All fragments share the same **ID x.**
- **Fragflag = 1** means "more fragments follow"; the last fragment has **0.**
- **Offset is measured in units of 8 bytes**, hence 1480/8 = 185.

---

## 4.4 Middleboxes and Architectural Principles

**Middlebox (RFC 3234):** *"any intermediary box performing functions apart from normal, standard functions of an IP router on the data path between a source host and destination host."*

Examples: **NAT**, **firewalls/IDS** (security), **load balancers**, **caches/CDNs**, application-specific boxes.

Trend: from **proprietary hardware** toward **"whitebox"** hardware with an open API and programmable **match+action**, controlled through **SDN** and virtualized as **NFV (Network Functions Virtualization).**

### The IP hourglass

The Internet has a **"thin waist":** many protocols above (HTTP, SMTP, QUIC, TCP, UDP…) and below (Ethernet, WiFi, fiber…), but **only one network-layer protocol, IP**, which every connected device must implement. Middleboxes (NAT, firewalls, NFV) are the "love handles" the hourglass has grown in middle age.

### Architectural principles (RFC 1958)

*"The goal is connectivity, the tool is the Internet Protocol, and the intelligence is end to end rather than hidden in the network."* Three cornerstones: **simple connectivity, the IP narrow waist, intelligence at the network edge.**

### The end-to-end argument (Saltzer, Reed, Clark 1981)

Some functions (e.g., reliable data transfer) can *only be completely and correctly implemented with the help of the applications at the endpoints*, so providing them inside the network is of limited value (an incomplete in-network version may be a useful performance boost). That's why reliability lives in TCP at the hosts, not in routers.

**Where is the intelligence?** 20th-century phone network: in the switches. Internet: at the edge. After 2005: programmable network devices *plus* massive application-level infrastructure at the edge.

*(Note: your Chapter 4 deck mentions "Generalized Forwarding / SDN / OpenFlow" in its roadmap, but the slide file doesn't include those slides, so they're not covered here beyond the match+action idea above.)*

---

## 4.5 Chapter 4 — Key Takeaways

- **Forwarding** (per-packet, data plane) vs. **routing** (path planning, control plane); the forwarding table links them.
- The Internet offers a **best-effort** service: no guarantees.
- Routers have input ports (lookup using **longest prefix match**), a **switching fabric** (memory/bus/interconnection), and output ports (buffering + scheduling). **HOL blocking** happens at inputs; **loss and delay** at outputs.
- Scheduling: **FCFS, priority, round robin, WFQ.** Buffers: ~RTT·C, or RTT·C/√N with many flows.
- **IP address = 32 bits per interface**; subnets, **CIDR (a.b.c.d/x)**, **DHCP** (discover → offer → request → ACK), **hierarchical addressing / route aggregation.**
- **NAT** shares one public address using a translation table of (IP, port) pairs.
- **IPv6**: 128-bit addresses, 40-byte fixed header, no checksum/fragmentation/options; transition by **tunneling.**
- **Fragmentation:** offsets in 8-byte units, flag = 1 on all but the last fragment.
- **Middleboxes** and the **end-to-end argument.**

---

# CHAPTER 5: The Network Layer — Control Plane (up to 5.6)

## 5.1 Introduction

Goals of this chapter: understand how routes get computed (traditional algorithms and SDN controllers), and see the Internet's actual protocols: **OSPF, BGP, ICMP.**

The control plane's output is the **forwarding table** that routers use in Chapter 4. Recall the two structures:
- **Per-router control:** a routing algorithm runs in every router (traditional).
- **Logically centralized control (SDN):** a remote controller computes and installs the tables.

---

## 5.2 Routing Algorithms

### Goal of a routing protocol

Find **"good" paths** from sending hosts to receiving hosts through the network of routers. A **path** is the sequence of routers a packet traverses. **"Good"** can mean least cost, fastest, or least congested.

### Graph abstraction

The network is modeled as a **graph G = (N, E)**:
- **N** = set of routers (nodes), e.g., {u, v, w, x, y, z}
- **E** = set of links (edges)
- **c(a,b)** = cost of the direct link between a and b (**∞** if they are not directly connected)

Cost is set by the network operator: it could always be 1 (hop count), inversely related to bandwidth, or inversely related to congestion.

### How routing algorithms are classified

| Question | Option A | Option B |
|---|---|---|
| Global or decentralized information? | **Global:** all routers know the complete topology and all link costs → **link-state (LS)** algorithms | **Decentralized:** iterative computation, exchanging info with neighbors; routers start by knowing only costs to neighbors → **distance-vector (DV)** algorithms |
| Static or dynamic? | **Static:** routes change slowly | **Dynamic:** routes change quickly (periodic updates or in response to link-cost changes) |

---

### Link-state routing: Dijkstra's algorithm

**Idea:** every node learns the **entire topology and all link costs** (via **link-state broadcast**, so everyone has the same info), then each node runs Dijkstra to compute the **least-cost path from itself to all other nodes**. This gives that node's forwarding table.

**Notation:**
- **c(x,y):** direct link cost from x to y (∞ if not neighbors)
- **D(v):** current estimate of the least-cost path cost from the source to v
- **p(v):** predecessor node of v on the path from the source
- **N′:** set of nodes whose least-cost path is **definitively known**

**Algorithm (source = u):**
```
Initialization:
  N' = {u}
  for all nodes v:
     if v is adjacent to u:  D(v) = c(u,v)
     else:                   D(v) = infinity
Loop:
  find w not in N' such that D(w) is a minimum
  add w to N'
  update D(v) for all v adjacent to w and not in N':
        D(v) = min( D(v),  D(w) + c(w,v) )
until all nodes are in N'
```
In words: repeatedly pick the closest not-yet-finalized node, finalize it, and see whether going *through it* gives a cheaper route to its neighbors.

**Worked example from your slides** (graph: u–v 2, u–w 5, u–x 1, v–x 2, v–w 3, x–w 3, x–y 1, w–y 1, w–z 5, y–z 2), source **u**:

| Step | N′ | D(v),p | D(w),p | D(x),p | D(y),p | D(z),p |
|---|---|---|---|---|---|---|
| 0 | u | 2,u | 5,u | **1,u** | ∞ | ∞ |
| 1 | ux | **2,u** | 4,x | | 2,x | ∞ |
| 2 | uxy | 2,u | **3,y** | | | 4,y |
| 3 | uxyv | | 3,y | | | 4,y |
| 4 | uxyvw | | | | | 4,y |
| 5 | uxyvwz | | | | | |

How to read it: at step 1, x (cost 1) is finalized. Going through x improves w from 5 → 1+3 = **4** and gives y = 1+1 = **2.** At step 2, y is finalized, and going through y improves w again from 4 → 2+1 = **3**, and gives z = 2+2 = **4.** The ties (v and y both at 2) can be broken arbitrarily.

**Resulting forwarding table at u:** to reach v → link (u,v) (directly); to reach **w, x, y, z** → link (u,x) (all via x). Tracing predecessors gives the **least-cost-path tree.**

**Complexity:**
- Computation: n iterations, each checking all nodes → n(n+1)/2 comparisons = **O(n²)** (a better implementation gives O(n log n)).
- Messages: each router's link-state must reach all other routers; efficient broadcast uses O(n) link crossings per router, so overall message complexity is **O(n²).**

**Oscillations:** if link cost depends on **traffic volume**, routes can oscillate: everyone shifts to the currently cheap path, which makes it expensive, so everyone shifts back, and so on.

---

### Distance-vector routing: Bellman-Ford

**The Bellman-Ford equation** (dynamic programming):

> **Dₓ(y) = min over neighbors v of { c(x,v) + Dᵥ(y) }**

- Dₓ(y) = cost of the least-cost path from x to y.
- To get from x to y, try going to each neighbor v first (cost c(x,v)), add *v's own* estimated cost to y, and take the smallest.
- The neighbor that gives the minimum is the **next hop** on the path.

**Slide example:** u's neighbors v, x, w know their costs to z: Dᵥ(z) = 5, Dₓ(z) = 3, D_w(z) = 3. With c(u,v) = 2, c(u,x) = 1, c(u,w) = 5:

D_u(z) = min{ 2+5, 1+3, 5+3 } = min{7, 4, 8} = **4**, with **x** as the next hop.

**The distance vector algorithm (key idea):**
- Each node **keeps its own distance vector** (its estimated cost to every destination) and, from time to time, **sends it to its neighbors.**
- When a node receives a neighbor's DV, it **updates its own DV with Bellman-Ford:** Dₓ(y) ← min over v { c(x,v) + Dᵥ(y) } for every y.
- If its DV changed, it notifies its neighbors; if nothing changed, it stays silent.
- Under natural conditions, the estimates **converge** to the true least costs.

**Properties:** **iterative and asynchronous** (each step is triggered by a local link-cost change or a DV message from a neighbor); **distributed and self-stopping** (no message → no action).

**State diffuses through the network:** a node's information at time t=0 reaches its 1-hop neighbors at t=1, 2-hop neighbors at t=2, and so on, like ripples.

**Computation example (slide, node b at t=1):** b receives DVs from a, c, e and recomputes. For example, D_b(d) = min{ c(b,a)+D_a(d), c(b,c)+D_c(d), c(b,e)+D_e(d) } = min{ 8+1, 1+∞, 1+1 } = **2** (via e). Similarly D_b(f) = **2**, D_b(h) = **2.**

**Link cost changes:**
- **Good news travels fast:** if a link cost *decreases*, the new cheaper path spreads in a couple of rounds.
- **Bad news travels slow — the count-to-infinity problem:** in the slide, y–x cost jumps from 4 to 60. y thinks "z says it can reach x at cost 5, so I'll go through z: cost 6," tells z; z then thinks "y can reach x at 6, so mine is 7," tells y; y updates to 8, and so on, **slowly counting up** because each depends on the other, creating a **routing loop** until the cost finally exceeds 50. (The textbook offers fixes such as *poisoned reverse.*)

### Comparison: Link State vs. Distance Vector

| | Link State (LS) | Distance Vector (DV) |
|---|---|---|
| Info each router has | Entire topology | Only neighbors' DVs |
| Message complexity | n routers → **O(n²)** messages | Exchanges only between neighbors; convergence time varies |
| Speed of convergence | O(n²) algorithm; **may oscillate** | Varies; **may have routing loops; count-to-infinity** |
| Robustness (a router malfunctions or lies) | A router can advertise a wrong *link* cost, but each router computes **only its own table** → damage is contained | A router can advertise a wrong *path* cost ("I have a really low-cost path to everywhere") causing **black-holing**; tables are used by others so **errors propagate** |

---

## 5.3 Intra-AS Routing in the Internet: OSPF

### Why not one big flat routing algorithm?

The idealized model assumed all routers identical and a flat network. In reality:
- **Scale:** billions of destinations; routing tables can't hold them all, and exchanging them would swamp the links.
- **Administrative autonomy:** the Internet is a network of networks; each administrator wants control over routing in its own network.

### Autonomous Systems (AS)

The Internet's solution: group routers into regions called **autonomous systems (ASes, a.k.a. domains).**
- **Intra-AS (intra-domain) routing:** routing among routers *within* one AS. All routers in an AS must run the **same** intra-domain protocol (different ASes may use different ones).
- **Inter-AS (inter-domain) routing:** routing *between* ASes.
- **Gateway routers:** at the edge of an AS, with links to routers in other ASes. They run **both** intra- and inter-AS routing.

**How the forwarding table is built:** intra-AS routing determines entries for destinations **inside** the AS; **inter-AS and intra-AS together** determine entries for **external** destinations. (If a router in AS1 gets a datagram for outside AS1, it needs inter-AS routing to learn which destinations are reachable via which AS, and which gateway to send it to.)

### Common intra-AS protocols

| Protocol | Type | Notes |
|---|---|---|
| **RIP** (Routing Information Protocol, RFC 1723) | Classic **distance vector**; DVs exchanged every 30 s | No longer widely used |
| **EIGRP** | Distance-vector based | Cisco-proprietary for decades; open since 2013 (RFC 7868) |
| **OSPF** (Open Shortest Path First, RFC 2328) | **Link state** | Widely used |
| IS-IS | Essentially the same as OSPF | ISO standard |

### OSPF

- **"Open"** = publicly available specification.
- A **classic link-state** protocol: each router **floods link-state advertisements** to **all** other routers in the entire AS (sent **directly over IP**, not TCP/UDP).
- Supports multiple link-cost metrics (bandwidth, delay).
- Each router builds the full topology and runs **Dijkstra** to compute its forwarding table.
- **Security:** all OSPF messages are **authenticated** to prevent malicious intrusion.

### Hierarchical OSPF

For large ASes, OSPF has a **two-level hierarchy: local areas + a backbone.**
- Link-state advertisements are flooded **only within an area** (or within the backbone).
- Each router knows detailed topology of its own area, and just the *direction* to reach other destinations.
- **Area border routers** "summarize" distances to destinations in their area and advertise them into the backbone.
- **Backbone routers** run OSPF limited to the backbone; **boundary routers** connect to other ASes.

---

## 5.4 Routing Among ISPs: BGP

### What BGP is

**BGP (Border Gateway Protocol)** is the **de facto inter-domain routing protocol** — *"the glue that holds the Internet together."* It lets a subnet advertise its existence to the rest of the Internet: *"I am here, here is who I can reach, and how."*

BGP gives each AS the means to:
- **eBGP:** get reachability information about subnets from **neighboring ASes.**
- **iBGP:** spread that reachability information to **all routers inside its own AS.**
- Determine **good routes** based on reachability *and policy.*

Gateway routers run **both eBGP and iBGP.**

### BGP basics

- Two BGP routers ("peers") exchange messages over a **semi-permanent TCP connection** (a "BGP session").
- They advertise **paths to destination network prefixes**; BGP is a **path-vector** protocol.
- When AS3's gateway advertises the path **"AS3, X"** to AS2, AS3 is **promising** it will forward datagrams toward X.

### Path attributes and BGP routes

A BGP advertisement = **prefix + attributes.** Two important attributes:
- **AS-PATH:** the list of ASes the advertisement has passed through (used to detect loops and to measure path length).
- **NEXT-HOP:** the specific internal router that leads to the next-hop AS.

**Policy-based routing:** the receiving gateway applies an **import policy** to accept or reject a path (e.g., "never route through AS Y"), and an AS's policy also decides **whether to advertise** a path to other neighbors.

### How an advertisement spreads (slide example)

1. AS3's router 3a advertises **"AS3, X"** to AS2's router 2c (eBGP).
2. Based on AS2's policy, 2c accepts it and spreads it to all AS2 routers via **iBGP.**
3. AS2's router 2a advertises **"AS2, AS3, X"** to AS1's router 1c (eBGP).
4. A router may learn **multiple paths** to the same destination: 1c learns "AS2, AS3, X" from 2a **and** "AS3, X" from 3a. By policy it picks "AS3, X" and tells the rest of AS1 via iBGP.

**Putting it together in a router's forwarding table:** to reach X, router 1d learns via iBGP "the path to X goes through 1c," then uses its **intra-AS (OSPF) routing** to find the interface that reaches 1c. So the final entry for X is **the same interface used for 1c.** (Different routers in the same AS may use different interfaces for the same destination.)

### BGP messages (exchanged over TCP)

| Message | Purpose |
|---|---|
| **OPEN** | Opens the TCP connection to the peer and authenticates the sender |
| **UPDATE** | Advertises a new path (or **withdraws** an old one) |
| **KEEPALIVE** | Keeps the connection alive when there are no updates; also ACKs an OPEN |
| **NOTIFICATION** | Reports errors in a previous message; also used to close the connection |

### Why different intra-AS and inter-AS routing?

| | Intra-AS | Inter-AS |
|---|---|---|
| **Policy** | One administrator → policy is less of an issue | Admin wants control over how traffic is routed and who uses their network |
| **Scale** | Hierarchy saves table size and update traffic | |
| **Performance** | Can focus on **performance** | **Policy dominates over performance** |

### Hot-potato routing

When a router has more than one gateway that can reach a destination, **hot-potato routing** picks the gateway with the **least intra-domain cost** (the closest exit) — *"get the packet out of my network as cheaply as possible"* — **without caring about the inter-domain cost** beyond it. (Slide: 2d picks 2a even though that path has more AS hops.)

### Achieving policy via advertisements

Real ISPs only want to carry traffic to/from their **customers**, not free **transit traffic** between other ISPs.
- Example: provider A advertises a path "A,w" to both B and C. **B chooses not to advertise "B,A,w" to C**, because none of C, A, or w is B's customer, so B would earn **no revenue** carrying C's traffic. So C routes to w via A directly.
- Dual-homed customer x (connected to providers B and C) does **not** advertise to B a route to C, so B and C can't use x as a transit between them.

### BGP route selection

When a router learns multiple routes to a destination AS, it chooses using these rules **in order:**
1. **Local preference** value (a policy decision)
2. **Shortest AS-PATH**
3. **Closest NEXT-HOP router** (hot-potato routing)
4. Additional criteria

### Why traffic engineering is hard with traditional routing

With LS/DV routing and destination-based forwarding, **link weights are the only control "knobs."** An operator can't easily:
- Force u→z traffic along u-v-w-z instead of the shortest path u-x-y-z (you'd have to change link weights, which affects *all* traffic).
- **Split** (load-balance) traffic between two paths.
- Route traffic from the *same* router to the same destination **differently** depending on, say, which flow it is.

These limits are the motivation for **generalized forwarding and SDN** (Chapter 4's match+action + a central controller).

---

## 5.5 The SDN Control Plane (what your deck covers)

Your Chapter 5 deck lists this section in its roadmap but doesn't include slides for it, so here is only the core idea (from the Chapter 4/5 overview slides):

- In an **SDN**, a **remote controller** (logically centralized) **computes and installs** the forwarding/flow tables in routers, instead of every router running its own routing algorithm.
- The routers become simple **match+action** devices that follow the tables, and the controller has the network-wide view needed for the traffic-engineering goals that traditional routing couldn't do (custom paths, load balancing, per-flow routing).
- Real-world instantiations named in the deck: **OpenFlow**, and the **ODL** and **ONOS** controllers.

If you want more depth on 5.5 (controller architecture, OpenFlow messages), tell me and I'll cover it from the textbook PDF.

---

## 5.6 ICMP: The Internet Control Message Protocol

**ICMP** is used by hosts and routers to communicate **network-level information:**
- **Error reporting:** unreachable host, network, port, or protocol.
- **Echo request/reply** (this is what **ping** uses).

ICMP sits "above" IP: ICMP messages are carried **inside IP datagrams.** An ICMP message has a **type**, a **code**, and the **first 8 bytes of the offending IP datagram** (so the sender can tell which packet caused the error).

**Important type/code pairs:**

| Type | Code | Meaning |
|---|---|---|
| 0 | 0 | Echo reply (ping) |
| 3 | 0 | Destination network unreachable |
| 3 | 1 | Destination host unreachable |
| 3 | 2 | Destination protocol unreachable |
| 3 | 3 | Destination **port** unreachable |
| 3 | 6 / 7 | Destination network / host unknown |
| 4 | 0 | Source quench (congestion control — not used) |
| 8 | 0 | Echo request (ping) |
| 9 | 0 | Route advertisement |
| 10 | 0 | Router discovery |
| 11 | 0 | **TTL expired** |
| 12 | 0 | Bad IP header |

### Traceroute and ICMP

Traceroute maps the route to a destination using **TTL** and ICMP:
1. The source sends **sets of UDP segments** (3 probes per set). The **1st set has TTL = 1, the 2nd set TTL = 2**, and so on.
2. The datagram in the **nth set** reaches the **nth router**, which sees TTL hit 0, **discards it, and sends back an ICMP message (type 11, code 0)** — often including the router's name and IP address.
3. The source records the **RTT** for each reply, and so learns each hop on the path.
4. **Stopping:** when a probe finally reaches the destination host, the destination returns an ICMP **"port unreachable" (type 3, code 3)** message (since the UDP port used is unlikely to be open), and the source stops.

This ties together **TTL** (Chapter 4), **UDP** (Chapter 3), and **ICMP.**

---

## 5.7 Chapter 5 — Key Takeaways

- The **control plane** computes the forwarding tables: per-router (traditional) or SDN (central controller).
- **Link state (Dijkstra):** global knowledge, O(n²), can oscillate, contained errors. **Distance vector (Bellman-Ford):** neighbor-only exchange, Dₓ(y) = minᵥ{c(x,v)+Dᵥ(y)}, can loop and has **count-to-infinity.**
- The Internet scales by using **autonomous systems:** **intra-AS** (RIP, EIGRP, **OSPF**) and **inter-AS** (**BGP**).
- **OSPF** is link-state, floods LSAs over IP, uses Dijkstra, authenticates messages, and is hierarchical (areas + backbone).
- **BGP** is the path-vector glue between ASes: **eBGP** (between ASes) + **iBGP** (inside an AS), **AS-PATH** and **NEXT-HOP**, runs over **TCP** with OPEN/UPDATE/KEEPALIVE/NOTIFICATION. **Policy beats performance** between ASes.
- **Route selection order:** local preference → shortest AS-PATH → closest NEXT-HOP (hot potato) → others.
- Traditional routing has limited traffic-engineering control, motivating **SDN.**
- **ICMP** reports errors and powers **ping** (types 8/0) and **traceroute** (type 11 TTL-expired, type 3 code 3 port-unreachable).

---

# Formula & Quick-Reference Sheet (Chapters 4–5)

| Topic | Formula / rule |
|---|---|
| Router buffer size (classic) | B ≈ RTT × C |
| Router buffer size (N flows) | B ≈ RTT × C / √N |
| WFQ share for class i | wᵢ / Σⱼ wⱼ of the service per cycle |
| Number of addresses in a /x block | 2^(32 − x) |
| Number of sub-blocks when splitting /a into /b | 2^(b − a) |
| IP fragment data size | MTU − 20 (IP header), multiple of 8 for non-last fragments |
| Fragment offset | (bytes of data before this fragment) / 8 |
| Total fragments needed | ⌈ (datagram length − 20) / (MTU − 20) ⌉ |
| Dijkstra update | D(v) = min( D(v), D(w) + c(w,v) ) |
| Dijkstra complexity | O(n²) (O(n log n) with better data structures) |
| Bellman-Ford (distance vector) | Dₓ(y) = minᵥ { c(x,v) + Dᵥ(y) } |
| BGP route selection order | Local pref → shortest AS-PATH → closest NEXT-HOP → other |

**Common quiz traps:**
- **Forwarding ≠ routing.** Forwarding = per-packet table lookup; routing = computing the table.
- **Longest prefix wins**, not the first match.
- **Fragment offsets are in 8-byte units**, and the **last fragment has flag 0.**
- **NAT table maps (IP, port) pairs**, so many internal hosts share one public IP.
- **IPv6 has no checksum, no fragmentation, no options** in the base header.
- **OSPF messages go directly over IP** (not TCP/UDP), while **BGP runs over TCP.**
- **LS vs. DV:** LS = everyone knows everything (Dijkstra); DV = only neighbors talk (Bellman-Ford). Count-to-infinity belongs to **DV**; oscillations belong to **LS**.
- **eBGP vs. iBGP:** eBGP is *between* ASes; iBGP is *inside* one AS.
- **Hot potato** minimizes **intra-AS cost**, ignoring the cost beyond the exit.
- **Traceroute** uses **TTL-expired (type 11)** from routers and **port-unreachable (type 3, code 3)** from the destination.

---

*This guide summarizes and explains Kurose & Ross, "Computer Networking: A Top-Down Approach," 8th Edition, Chapters 4 and 5 (through section 5.6), based on your uploaded slide decks.*
