Why is SIGPIPE fatal by default? What design assumption does that reflect?
    ANS: 
Why must the signal handler use volatile sig_atomic_t instead of a plain int?
    ANS: A plain int can have its values corrupted but the volatile keyword prevents it being cached  and the sig_atomic_t keyword forces the kernel to perform one  R/W 
How does the idle timeout sweep interact with the epoll event loop without blocking?
    ANS: It sweeps in the main thread not on worker threads 
What is a slowloris attack and which requirement in this TRD defends against it?
    ANS: It is an attack wherein a client purposefully sends their request bytes slowly to the server. R4  defends against slowloris attacks.