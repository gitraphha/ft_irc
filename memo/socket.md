### sockets
man 2 socket
man 7 socket
man setsockopt
man listen

> The  accept()  system  call  is used with connection-based socket types (SOCK_STREAM, SOCK_SEQPACKET)


## for `int listen(int sockfd, int backlog);` can the backlog parameter be set to unlimited?

**tl;dr: set `backlog` to `SOMAXCONN`**

No, the `backlog` parameter in `int listen(int sockfd, int backlog);` **cannot be set to unlimited**.

### Key Constraints

1.  **Silent Truncation**: The `backlog` argument is a **hint**, not a strict command. If you pass a value larger than the system's maximum limit, the kernel **silently truncates** it to that maximum. It does not error out, nor does it create an infinite queue.
2.  **System Limit (`somaxconn`)**: On Linux, the effective maximum is defined by the kernel parameter **`net.core.somaxconn`**.
    *   **Default**: Historically **128**, but increased to **4096** in Linux kernel 5.4+.
    *   **Behavior**: If you call `listen(sockfd, 10000)` but `somaxconn` is 128, the queue size becomes 128.
3.  **Negative Values**: Passing a negative value (e.g., `-1` intending "unlimited") typically results in the backlog being set to **0** (minimum) or a small default, depending on the OS implementation. It does not trigger unlimited behavior.
4.  **`SOMAXCONN` Constant**: The constant `SOMAXCONN` (defined in `<sys/socket.h>`) represents the maximum useful backlog value for the platform at compile time.
    *   **Linux**: Often defined as 4096 or 128 in headers, but the *runtime* limit is what matters (`/proc/sys/net/core/somaxconn`).
    *   **Windows**: `SOMAXCONN` is defined as `0x7fffffff` (max int), acting as a special flag to tell the OS to use the "maximum reasonable value," but this is still finite and managed by the OS, not truly unlimited.

### Best Practice
To maximize the backlog:
1.  Pass a large integer (e.g., `4096` or higher) to `listen()`.
2.  Ensure the system's `net.core.somaxconn` is tuned accordingly (e.g., via `sysctl -w net.core.somaxconn=65535`).
3.  The effective backlog will be `min(requested_backlog, somaxconn)`.

