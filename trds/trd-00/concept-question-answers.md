What does socket(AF_INET, SOCK_STREAM, 0) actually return, and what does the kernel do when it's called?

ANS: It returns a file descriptor for an endpoint in network communication. The kernel goes through multiple steps when a C program calls socket(). In brief,socket()
sets off various other syscalls that validate input, initialize structs in memory, call protocol-specific constructors and ultimately creates a VFS wrapper to allow
write() and read() operations.

Why is SO_REUSEADDR needed, and what happens without it when restarting the server quickly?

ANS: It's needed to allow a process to bind to a given address/port


ANS: 

What is the listen() backlog, and what happens when it fills up?

What's the difference between the listening FD and the client FD returned by accept()?

Why does Content-Length matter, and what breaks if it's wrong?

What does Connection: close tell the client?

What happens if close() is never called on the client FD in the loop?
