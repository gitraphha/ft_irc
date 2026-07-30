# Questions
- what is a spanning tree exactly?\
maybe not relevant, bc we have a single lonely server only

  > In graph theory, a spanning tree is a subgraph of an undirected, connected graph that includes all vertices of the original graph using the minimum possible number of edges.  For a graph with $n$ vertices, a spanning tree always contains exactly $n-1$ edges, ensuring the structure is connected without forming any cycles or loops. 
  > 
  > Spanning trees are critical for network routing protocols (like the Spanning Tree Protocol or STP) to prevent data loops in mesh topologies, and they serve as intermediate steps in pathfinding algorithms such as Dijkstra’s algorithm and A*.  When edges have weights, the goal is often to find a minimum spanning tree using greedy algorithms like Prim’s or Kruskal’s to minimize total connection costs. 
  > 
# RFCs
https://datatracker.ietf.org/doc/html/rfc2810
- contains links to implementations at the end

https://datatracker.ietf.org/doc/html/rfc2811
https://datatracker.ietf.org/doc/html/rfc2812
https://datatracker.ietf.org/doc/html/rfc2813

# Reqs
## Subject

### However, you only have to implement the following features:
You must be able to 
+ authenticate
+ set a nickname
+ a username
+ join a channel
+ send and receive private messages using your reference client
+ All the messages sent from one client to a channel have to be forwarded to every other client that joined the channel.
+ You must have operators and regular users.
+ Then, you have to implement the commands that are specific to **channel operators**:
  + KICK - Eject a client from the channel
  + INVITE - Invite a client to a channel
  + TOPIC - Change or view the channel topic
  + MODE - Change the channel’s mode:
    + i: Set/remove Invite-only channel
	+ t: Set/remove the restrictions of the TOPIC command to channel operators
	+ k: Set/remove the channel key (password)
	+ o: Give/take channel operator privilege
	+ l: Set/remove the user limit to channel

### Verify every possible error and issue, such as receiving partial data, low bandwidth, etc.

### Client Commands basic

+ With both nc and the reference IRC client, check that you can authenticate, set a nickname, a username, join a channel. This should be fine (you should have already done this previously).
+ Verify that private messages (PRIVMSG) are fully functional with different parameters.


## Eval
https://wormav.github.io/42_eval/Cursus/Ft_irc/index.html
+ Ask and check how many poll() (or equivalent) are present in the code. **There must be only one.**
+ **MacOS only!** ~~Verify that each call to fcntl() is done as follows: fcntl(fd, F_SETFL, O_NONBLOCK); Any other use of fcntl() is forbidden.~~

# difference poll() select() kqueue() epoll()

**Performance and Scalability**: The primary difference lies in computational complexity and file descriptor handling. **`select()`** and **`poll()`** both operate in **O(n)** time, requiring the kernel to scan all monitored file descriptors on every call, whereas **`epoll()`** and **`kqueue()`** operate in **O(1)** time by maintaining kernel-side state, making them suitable for high-concurrency servers. **`select()`** is further limited by a default **1024 file descriptor cap** and carries risks of stack corruption if exceeded, while **`poll()`** removes this hard limit but retains the linear scanning performance penalty.

**Portability and Use Cases**: **`select()`** and **`poll()`** are **POSIX-standard** and available across all Unix-like systems, making them ideal for **cross-platform applications** or those with **low concurrency** (<1000 FDs). **`epoll()`** is **Linux-only** (available since kernel 2.5.44) and is the standard for **high-performance Linux servers** handling thousands of connections. **`kqueue()`** is the equivalent high-performance alternative for **BSD-derived systems** (macOS, FreeBSD), offering richer event filtering capabilities than epoll.

**API Design**: **`select()`** uses bitmask structures which are inefficient for sparse file descriptors, while **`poll()`** uses a sparse array of structures for better handling of large FD ranges. **`epoll()`** separates the registration of file descriptors (`epoll_ctl`) from waiting for events (`epoll_wait`), supporting both **edge-triggered** and **level-triggered** modes. **`kqueue()`** integrates event registration and retrieval into a single **`kevent()`** system call, using a more abstracted structure that supports various event types beyond just I/O.

| API | Complexity | FD Limit | Portability | Best For |
| :--- | :--- | :--- | :--- | :--- |
| **select()** | O(n) | 1024 (default) | Universal | Legacy code, few FDs (<10) |
| **poll()** | O(n) | No hard limit | POSIX (Universal) | Cross-platform, moderate concurrency |
| **epoll()** | O(1) | System limit | Linux only | High-performance Linux servers |
| **kqueue()** | O(1) | System limit | BSD/macOS | High-performance BSD/macOS servers |

# std conforming clients

*we can choose one ref client that will also be used in evaluation*

> **From the subject pdf:**\
> Several IRC clients exist. You have to choose one of them as a reference. Your reference client will be used during the evaluation process.

+ HexChat: Open-source, supports multi-server, UTF-8, TLS, SASL, and various scripting languages. 

+ Irssi: A terminal-based client supporting multi-server, UTF-8, TLS, and SASL, often used for always-on connectivity. 

# Reference implementation
https://github.com/42YerevanProjects/ft_irc/blob/master/src/network/Server.cpp#L263