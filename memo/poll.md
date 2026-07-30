The **`poll()`** function in C++ (via `<poll.h>`) is a POSIX system call used for **I/O multiplexing**, allowing a program to monitor multiple file descriptors to see if I/O is possible on any of them. It is similar to `select()` but uses an array of `struct pollfd` instead of bitmasks, avoiding the `FD_SETSIZE` limit and offering better performance for large numbers of descriptors.

### Syntax and Structure

```cpp
#include <poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

*   **`fds`**: An array of `struct pollfd` specifying the file descriptors and events to monitor.
*   **`nfds`**: The number of elements in the `fds` array.
*   **`timeout`**: Time to wait in milliseconds. Use **`0`** for non-blocking, **`-1`** for infinite blocking, or a positive integer for a specific duration.

### Key Event Flags

*   **`POLLIN`**: Data is available for reading.
*   **`POLLOUT`**: Data can be written without blocking.
*   **`POLLERR`**: An error occurred.
*   **`POLLHUP`**: Connection hung up (disconnected).
*   **`POLLNVAL`**: Invalid file descriptor.

### Basic Example

```cpp
#include <iostream>
#include <poll.h>
#include <unistd.h>

int main() {
    struct pollfd fds[1];
    
    // Monitor stdin (file descriptor 0) for input
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    // Wait for 5 seconds (5000 ms)
    int ret = poll(fds, 1, 5000);

    if (ret < 0) {
        std::cerr << "poll error" << std::endl;
    } else if (ret == 0) {
        std::cout << "Timeout: No data received." << std::endl;
    } else {
        // Check if the specific event occurred
        if (fds[0].revents & POLLIN) {
            std::cout << "Data is available on stdin." << std::endl;
        }
        if (fds[0].revents & POLLHUP) {
            std::cout << "Connection closed." << std::endl;
        }
    }
    return 0;
}
```

### Windows Support
Standard `poll()` is not natively supported on Windows. For Winsock, use **`WSAPoll()`** or implement a compatibility layer using `select()`. Linux and macOS support `poll()` natively.



### Performance and Scalability
While `poll()` improves upon `select()` by removing the 1024 file descriptor limit and using a flexible array instead of bitmasks, it still suffers from **O(n) performance complexity**. Every call requires the kernel to copy the entire file descriptor array from user space, linearly scan every descriptor to check for events, and copy the results back. This makes `poll()` inefficient for applications monitoring thousands of connections, as the CPU cost grows linearly with the number of descriptors regardless of how many are actually active. For high-concurrency scenarios, **`epoll`** (Linux) or **`kqueue`** (BSD/macOS) are preferred as they use event-driven callbacks to achieve O(1) or O(active) complexity.

### Timeout Behavior and Granularity
> must be 0 (non-blocking) for ft_irc!

The `timeout` argument specifies the wait duration in **milliseconds**.
*   **`0`**: Returns immediately (non-blocking poll), useful for checking status without waiting.
*   **`-1`**: Blocks indefinitely until an event occurs or a signal interrupts the call.
*   **Positive Integer**: Blocks for at least the specified time. Note that system timer granularity may round up the actual wait time slightly.
A return value of **`0`** specifically indicates a timeout occurred with no events, whereas **`-1`** indicates an error (check `errno`).

### Critical Pitfalls and Event Handling
*   **Regular Files**: `poll()` always reports regular files as ready for reading/writing immediately. It cannot be used to wait for file content changes; using it on regular files in a loop will cause a "busy spin" consuming 100% CPU.
*   **Race Conditions**: A positive result in `revents` (e.g., `POLLIN`) guarantees that a read *would not block at that exact instant*, but it does not guarantee future state. Data could be consumed by another thread or the connection could close before your `read()` call. Always handle `EAGAIN`/`EWOULDBLOCK` even after `poll()` returns ready.
*   **Automatic Flags**: The kernel sets `POLLERR`, `POLLHUP`, and `POLLNVAL` in `revents` **even if you did not request them** in `events`. Robust code must always check for these flags to detect connection resets, errors, or invalid descriptors.
*   **Array Limits**: Passing an `nfds` value larger than the process's `RLIMIT_NOFILE` (often 1024 by default) will cause `poll()` to fail immediately with `EINVAL`.

### Windows Compatibility
Standard `poll()` is part of POSIX and is not natively available in the standard Winsock API. Windows provides **`WSAPoll()`** in newer versions (Vista/Server 2008+), but it has historically had bugs and performance issues. Production C++ applications targeting Windows often use `select()`, Overlapped I/O (IOCP), or cross-platform libraries like **`libuv`** or **`Boost.Asio`** to abstract these differences.

## --- Pitfalls ---

Common pitfalls when using `poll()` in C++ often stem from misunderstanding how the kernel updates the `pollfd` structure or mismanaging the event loop logic.

### 1. Misinterpreting `revents` vs `events`
A frequent error is checking the `events` field after the call instead of `revents`.
*   **`events`**: Input only. Specifies what you are interested in. It is **not** modified by `poll()`.
*   **`revents`**: Output only. Filled by the kernel with actual events that occurred.
*   **Pitfall**: Checking `fds[i].events & POLLIN` after the call will always return true if you requested it, regardless of whether data is actually available. You must check `fds[i].revents & POLLIN`.

### 2. Failing to Consume Data (The "Busy Loop")
`poll()` is level-triggered. If `POLLIN` is set, it means data is available *now*.
*   **Pitfall**: If `poll()` returns `POLLIN` but you do not call `read()` (or `recv()`) to consume the data, the next call to `poll()` will immediately return `POLLIN` again.
*   **Result**: This creates a tight, 100% CPU usage loop. Always ensure you read all available data or switch to non-blocking I/O with proper handling.

### 3. Ignoring `POLLHUP`, `POLLERR`, and `POLLNVAL`
These flags are special because they are **always** reported in `revents` if the condition exists, even if you did not request them in `events`.
*   **Pitfall**: Only checking for `POLLIN` or `POLLOUT`. If a socket disconnects (`POLLHUP`) or an error occurs (`POLLERR`), `poll()` returns successfully, but `POLLIN` might not be set (or might be set alongside `POLLHUP` depending on the OS).
*   **Consequence**: If you don't explicitly check for `POLLHUP` or `POLLERR`, you might keep a dead socket in your array forever, or worse, attempt to write to a closed connection.
*   **Best Practice**: Always check `if (revents & (POLLHUP | POLLERR | POLLNVAL))` and close/remove the descriptor immediately.

### 4. Not Resetting or Managing the `pollfd` Array
Unlike `select()`, `poll()` does not modify the `events` field, but the logic for managing the array can be tricky.
*   **Pitfall**: Passing invalid or closed file descriptors in the array without marking them. If an FD is closed but left in the array, `poll()` will set `POLLNVAL` for that entry.
*   **Strategy**: When removing a socket, it is common to either compact the array (shift elements) or set the `fd` to a negative value (e.g., `-1`). The standard dictates that if `fd < 0`, `poll()` ignores the entry and sets `revents` to 0.

### 5. Performance Scalability (O(N) Complexity)
While `poll()` fixes the 1024 FD limit of `select()`, it retains the **O(N)** performance characteristic.
*   **Pitfall**: Using `poll()` for thousands of concurrent connections. The kernel must scan the *entire* array passed by the user on every call, regardless of how many FDs are actually active.
*   **Impact**: As the number of connections grows, CPU usage increases linearly, even if traffic is low. For high-concurrency servers (10k+ connections), **`epoll`** (Linux) or **`kqueue`** (BSD/macOS) is required.

### 6. Signal Interruption (`EINTR`)
* probably n/a for ft_irc, since poll is used non-blocking and hence isn't waiting
* BUT: the server process is maybe stopped by a signal, so this must be handled? in the main for loop?\
-> Maybe it's only Ctrl+C / SIGINT, that is handled "automagically"?\
-> NO, see **SIGNALS** below
*   **Pitfall**: Not handling the return value `-1` with `errno == EINTR`.
*   **Context**: If a signal is caught while `poll()` is waiting, it returns early with an error.
*   **Fix**: Always wrap `poll()` in a loop that retries if `errno == EINTR`.

#### SIGNALS
Yes, handling signals is a mandatory requirement for the ft_irc project. 

According to the project specifications, you must handle specific signals to ensure robust client-server interaction:

+ SIGINT (CTRL+C): To properly terminate the server or client process.
+ CTRL+D: To handle partial messages or end-of-file conditions.
+ CTRL+Z: To handle suspended processes. 

The guidelines also specify that you should use no memory leaks (including still reachable ones) and implement send buffers for each client to efficiently manage these signal interruptions, particularly for CTRL+Z. 

### 7. ERRNO handling
* subject requires to not implement any logic based on ERRNO
* Key Characteristics\
EAGAIN and EWOULDBLOCK are POSIX error codes (stored in errno) indicating that an operation cannot be completed immediately without blocking, but may succeed if attempted later.\
Not a Fatal Error: Receiving these codes is normal behavior in non-blocking I/O.  It simply means "try again later." It does not indicate a broken connection or a bug.

### Summary Checklist for Robust Usage
1.  Check return value: `-1` (error), `0` (timeout), `>0` (events).
2.  Iterate only if return `> 0`.
3.  Check `revents` (never `events`) for status.
4.  Prioritize checking `POLLERR`, `POLLHUP`, and `POLLNVAL` before data flags.
5.  Consume data immediately upon `POLLIN`.
6.  Handle `EINTR` by retrying.

