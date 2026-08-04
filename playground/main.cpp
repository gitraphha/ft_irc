#include <iostream>
#include <csignal>
#include <unistd.h>
//#include <atomic>
#include <map>
#include <string>
#include <string.h>
#include <utility>

// Global flag to indicate when to exit
// std::atomic<int> should_exit(false);
volatile static std::sig_atomic_t should_exit = 0;

class A {
private:
std::string s;
public:
A() {s = "Hello";}
~A() {
	std::cout << "dtor: " << s << std::endl;
}
};

// Signal handler - NO I/O operations here!
extern "C"
void handle_sigint(int sig) {
    should_exit = sig;  // Only set the flag
}

// Function to release resources
void cleanup_resources(A *pa) {
    std::cout << "[Cleanup] Releasing resources...\n";
    // Close files, free memory, close database connections, etc.
	delete pa;
    std::cout << "[Cleanup] Resources released successfully.\n";
}

const std::pair<int, std::string>p(SIGINT, "SIGINT");
static const std::map<int, std::string> sigs(&p, &p+1);

int main() {
    // Install the signal handler for SIGINT
    if (std::signal(SIGINT, handle_sigint) == SIG_ERR) {
        std::cerr << "std::signal failed\n";
        return 1;
    }
    
    std::cout << "Signal handler installed. Press Ctrl+C to exit gracefully.\n";

	A *pa = new(A);
	A b;
	
    // Main program loop
    while (!should_exit) {
        std::cout << "Running... (PID: " << getpid() << ")\n";
        sleep(1);  // Simulate work
    }
    
    // Release resources before exiting
    std::cout << "\n[Signal Handler] Caught signal: " <<
	 /* sigs.at(should_exit) */
	 strsignal(should_exit)
	  << ". Shutting down gracefully...\n";
    cleanup_resources(pa);
    
    std::cout << "Program terminated cleanly.\n";
    return 0;
}
