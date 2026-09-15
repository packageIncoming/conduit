<p align="center">
  <img src="assets/conduit-header.png" alt="Conduit" width="600">
</p>
<p align="center">
  <a href="https://github.com/packageIncoming/conduit/actions/workflows/build.yml"><img src="https://github.com/packageIncoming/conduit/actions/workflows/build.yml/badge.svg" alt="build"></a>
</p>
<p align="center">
  <b>A multithreaded HTTP/1.1 static file server, built on epoll from raw sockets up.</b>
</p>
<p align="center">
  C &middot; epoll &middot; POSIX threads &middot; Linux
</p>
<p align="center">
  Mert Isik &middot; <a href="https://github.com/packageIncoming">github.com/packageIncoming</a> &middot; <a href="https://linkedin.com/in/mert-c-isik">linkedin.com/in/mert-c-isik</a> &middot; mertisik329@gmail.com
</p>

---
 

Conduit is a multithreaded HTTP/1.1 static file server written in C. It is built using epoll edge-triggered I/O with per-core event loops behind `SO_REUSEPORT`, and was made as a learning project for the Linux systems programming interface. Conduit covers sockets, non-blocking I/O, signals, and POSIX threads.

**Status:** Feature-complete. Passes `valgrind --leak-check=full` under load with no leaks. Handles 200/400/403/404/405/408/500/503. Survives malformed input, oversized requests, slowloris connections, and graceful shutdown via SIGINT/SIGTERM.

---

## Architecture

```
                    ┌──────────────────────────────────────────┐
                    │  main: parse argv, block SIGINT/SIGTERM, │
                    │        spawn N threads, sigwait()        │
                    └────────────────────┬─────────────────────┘
                                         │
              ┌──────────────────────────┼──────────────────────────┐
              ▼                          ▼                          ▼
      ┌───────────────┐          ┌───────────────┐          ┌───────────────┐
      │   Thread 1    │          │   Thread 2    │   ...    │   Thread N    │
      │               │          │               │          │               │
      │ own listen fd │          │ own listen fd │          │ own listen fd │
      │ SO_REUSEPORT  │          │ SO_REUSEPORT  │          │ SO_REUSEPORT  │
      │ own epoll fd  │          │ own epoll fd  │          │ own epoll fd  │
      │ own conn_list │          │ own conn_list │          │ own conn_list │
      │ own state     │          │ own state     │          │ own state     │
      └───────┬───────┘          └───────┬───────┘          └───────┬───────┘
              │                          │                          │
              └──────────────────────────┴──────────────────────────┘
                                         │
                            kernel distributes incoming
                            connections across listeners

      each thread, independently:

          epoll_wait(timeout=1s)
                 │
                 ▼
          for each event:
            • EPOLLERR/EPOLLHUP -> tear down
            • EPOLLOUT          -> finish partial write
            • listen fd         -> accept loop
            • client fd         -> read, parse, build, write
                 │
                 ▼
          conn_list_sweep  (idle timeout eviction)
```

Every thread runs the same loop over its own listen socket, its own epoll instance, its own connection list, and its own `thread_state`. Nothing is shared between threads, so there is no task queue, no condvar, and no mutex anywhere in the request path. The kernel's `SO_REUSEPORT` group distributes incoming connections across the listeners.

### Connection lifecycle

```
accept ─► conn_list (CONN_READING) ─► EPOLLIN events read into buffer
            │                                    │
            │ (10s idle sweep)                   ▼
            ▼                          headers complete (\r\n\r\n)
         408 + close (CONN_DONE)                 │
                                                 ▼
                                       parse + validate + build
                                                 │
                                                 ▼
                                       write (CONN_WRITING)
                                        │              │
                              complete  │              │  partial
                                        ▼              ▼
                            close (CONN_DONE)   rearm EPOLLOUT,
                                                finish on next event
```

---

## Build & Run

Requires Linux (epoll), gcc, and pthreads.

```bash
make                                              # produces ./conduit
./conduit <port> <docroot>                        # e.g. ./conduit 8080 ./docroot
./conduit -t 8 -c 100 8080 ./docroot              # 8 threads, 100 connections each
./conduit -h                                      # full usage
```

| Flag | Meaning | Default |
|------|---------|---------|
| `-t`, `--threads` | Worker threads, each with its own listen fd and epoll instance | 4 |
| `-c`, `--conns-per-thread` | Max concurrent connections per thread; beyond this, new connections get a raw 503 | 20 |

Send `SIGINT` (Ctrl+C) or `SIGTERM` for graceful shutdown.

### Module layout

| File | Responsibility |
|------|---------------|
| `conduit.c` | Entry point: thread spawn, socket setup, signal handling, event loop |
| `epoll_handler.[ch]` | Connection lifecycle, epoll integration, idle-timeout sweep |
| `request.[ch]` | HTTP request struct + parsing helpers |
| `response.[ch]` | HTTP response struct + builder primitives |
| `dochandler.[ch]` | Filesystem path resolution, MIME detection, file I/O |
| `args.[ch]` | `getopt_long` argument parsing with strict integer validation |

---

## Benchmarks

Measured with `bench.sh`, which sweeps server and client configurations and reports medians with a per-row noise floor.

**Conditions**

| | |
|---|---|
| CPU | Intel i7-7700 @ 3.60GHz, 4 cores / 8 threads |
| Kernel | Linux 7.0.0-31-generic x86_64 |
| Governor | `performance` |
| Client | `wrk` 4.1.0, co-resident on the same machine |
| Path | loopback |
| File | `index.html`, static |
| Per config | 3s warmup discarded, then 3 runs × 10s, medians reported |

Loopback never reaches a NIC, and the load generator shares all 4 physical cores with the server. Both are confounds and both are recorded in the conditions block that `bench.sh` prints with every run.

### Server thread scaling

`-c 100` in all rows, so the capacity cap never fires and every row is serving files rather than 503s. `wrk` threads set to `min(connections, nproc)`.

| Connections | `-t 1` | `-t 2` | `-t 4` | `-t 8` |
|---|---|---|---|---|
| 1 | 14,093 | 12,010 | 11,908 | 11,399 |
| 10 | 24,655 | 33,112 | 41,495 | 43,730 |
| 50 | 17,216 \* | 27,582 | 48,146 | 51,955 |

\* 26.8% run-to-run spread, noise-dominated. Every other row in this table is under 12%.

At 50 connections, throughput goes from 17,216 at one thread to 51,955 at eight, a 3.0x gain. Most of it lands by four threads: 1 -> 2 is 1.60x, 2 -> 4 is 1.75x, 4 -> 8 is 1.08x.

At a single connection, more threads is slightly worse (14,093 down to 11,399).

### Client thread scaling

Server pinned at `-t 8 -c 100`, `wrk` threads swept, to check whether the load generator was the limiter.

| `wrk` threads | 50 connections | 200 connections |
|---|---|---|
| 1 | 18,499 | 16,058 |
| 2 | 35,517 | 32,331 |
| 4 | 46,272 | 48,803 |
| 8 | 51,555 | 49,059 |

### Headline

| | |
|---|---|
| Configuration | `-t 8 -c 100`, 50 connections, 8 `wrk` threads |
| Throughput | **51,955 req/s** |
| p50 / p90 / p99 | 642 µs / 1,560 µs / 2,700 µs |
| max | 6,150 µs |
| Socket errors, non-2xx | 0, 0 |
| Run-to-run spread | 1.0% |

The same configuration measured 51,555 req/s in a separate sweep run, 0.8% apart.

### Capacity cap

Total capacity is `threads × conns-per-thread`. At 200 offered connections, rejections fall as capacity crosses the offered load:

| Capacity | 503 rate |
|---|---|
| 100 (`-t 1 -c 100`) | ~100% |
| 200 (`-t 2 -c 100`) | 57% |
| 400 (`-t 4 -c 100`) | 11% |
| 800 (`-t 8 -c 100`) | 0.3% |

Rows where the cap is firing report high throughput because the 503 path performs no malloc, no parse, and no file read. `bench.sh` reports a `non2xx` column so that the speed is not misinterpreted as successful requests. The early 503 exit is simply faster and allows more failed requests/second.

### Reproducing

```bash
./bench.sh -b ./conduit -r ./docroot/ -S "1 2 4 8" -K "100" \
           -C "1 10 50 200" -o results.csv http://localhost:8080/index.html
```

`bench.sh -h` documents the client sweep, server sweep, CSV output, and an optional `-N` mode that runs the server in a network namespace behind a veth pair instead of loopback.

### Benchmark history

The v1 README reported 2,839 req/s at 1.50 ms p50. That figure was measured under WSL2, on a different concurrency model, before the thread pool was replaced. It is not comparable to anything in the table above and should be disregarded.

---

## Design Decisions

**Edge-triggered epoll (`EPOLLET`).** Forces a single drain per readiness notification, which keeps the event loop's per-event work bounded. Comes with the constraint that all reads/writes must consume their buffers until `EAGAIN`, as partial drains cause silent stalls.

**`SO_REUSEPORT` with one listen socket, epoll instance, and connection list per thread.** The earlier design had a single event loop that read, parsed, and built responses, then handed the connection to a worker pool that only wrote and closed. Sweeping `-t` across 1, 2, 4, and 8 on the current design gives 3.0x at 50 connections. Because each thread owns its connections end to end, nothing is shared, and the mutex that previously guarded the task queue and the connection counter no longer exists. Thus now the threads do not have to contend over a single mutex for accessing counters or the task queue.

**Explicit event-bit dispatch rather than if/else.** `EPOLLERR`/`EPOLLHUP`, `EPOLLOUT`, and `EPOLLIN` are checked as separate conditions in that order.  A single event can carry several flags, but handling them in this order handles events in terms of severity.  An `else` branch that assumes "not EPOLLIN means error" will tear down a connection that was only waiting to finish a write.

**Response body ownership by pointer + flag, not embedded buffer.** The `http_response_t` struct holds a `const char*` to the body and an `owns_body_flag` indicating whether to free it on cleanup. This avoids copying file contents into the struct & the struct behaves as metadata around an externally-allocated buffer.

**Per-connection linked list for idle-timeout tracking.** Each thread maintains a linked list of `CONN_READING` connections and sweeps it once per event loop iteration, evicting any connection idle for more than 10 seconds with a 408. This defends against slowloris-style attacks alongside the connection limit.

**The sweep runs after the event batch, not before it.** `epoll_wait` fills `events[]` before the sweep runs. Sweeping first can free a connection that still has a pending entry in that batch, leaving `events[i].data.ptr` dangling. This produced a use-after-free that only appeared under sustained load long enough for connections to reach the idle threshold while a batch was in flight. It was confirmed by lowering `TIMEOUT_SECONDS` to 1, which reproduced it within seconds, and the fix verified against 3× the reproducing load.

**Per-thread connection counter for the capacity cap.** Increments on accept, decrements on free, in the write path, in the timeout sweep, and in error paths. When the count reaches the per-thread limit, new connections receive a raw 503 and are immediately closed. This short-circuit is a fast write since no mallocs or frees are called.

**`SO_REUSEPORT` on the listen socket.** Required for the per-thread listener design, and it also allows immediate restarts during development. During development it hid some silent bugs: a zombie server holding the port and stealing traffic from a newly started one, which produced benchmark tables attributed entirely to the wrong process. `bench.sh` now refuses to start a server if the port is already answering.

**`SIGPIPE` ignored at startup.** Prevents a closed client from killing the server. The default action terminates the process when writing to a closed socket, but for a network server this is undesirable.

**`sigwait` on a dedicated main thread.** `SIGINT` and `SIGTERM` are blocked in `main` before any thread is spawned, so they are inherited blocked by every worker. `main` then blocks in `sigwait`, clears `ACTIVE`, and joins. No async-signal-safety constraints in the handler.

**`listen(fd, SOMAXCONN)`.** The backlog is the accept queue depth, not a concurrency limit. Tying it to the per-thread connection cap produced maxima at 1× and 2× the kernel's 200 ms minimum RTO, from dropped handshake ACKs when the queue overflowed. Capacity is enforced in `add_new_connections`, where it belongs.

---

## Known Limitations

These are scoping decisions, not bugs.

- **No HTTP keep-alive.** Every request opens and closes a fresh TCP connection. This is the single largest performance ceiling since TCP handshake and teardown dominate the per-request cost for small files. A keep-alive implementation would likely produce a 5–10x throughput improvement.
- **Per-request `malloc` churn.** Each connection allocates a `connection_t`, an `http_request_t`, an `http_response_t`, a read buffer, and (on success) a write buffer plus the file contents. A pool allocator (or slab allocator) for the per-request structs would replace the three malloc/free pairs per connection with O(1) free-list operations and improve cache locality.
- **`realpath()` on every request.** The docroot is resolved once at thread startup, but the request path is still resolved per request, and `realpath` stats every component of the path.
- **No static file cache.** Every request opens, stats, reads, and closes the target file, and mallocs a buffer for its contents, even for a file that has not changed across hundreds of thousands of requests.
- **Static files only, no dynamic content.** No CGI, no reverse proxy, no application handlers. Suitable for serving HTML/CSS/JS/images; not a general-purpose server.
- **Loopback benchmarks, with the client on the same machine.** Numbers reflect server-side processing capacity with the load generator competing for the same 4 physical cores. Throughput on a real LAN/WAN will be lower, and latency will be higher. `bench.sh -N` runs the server behind a veth pair, which removes the loopback shortcut but not the co-location.
- **No HTTPS.** Implementing TLS would require integrating a crypto library (OpenSSL or BoringSSL), loading server certificates from disk, and handling the TLS handshake on each connection. Significant work and a different problem domain than what Conduit was built to explore.
- **Limited HTTP/1.1 compliance.** Conduit parses requests and produces well-formed responses, but doesn't implement chunked transfer encoding (always uses Content-Length), range requests (no Range header support, no 206 Partial Content), or content negotiation (ignores Accept-* headers). Enough for static file serving, but not enough to be a drop-in HTTP/1.1 server.

---

## What I'd Do Next

If extending Conduit beyond its current scope, the priority order would be:

1. **HTTP keep-alive.** The single largest throughput boost. Requires per-connection state machines for sequential request handling and pipelining, and preserving buffered bytes across requests so a pipelined second request is not discarded.
2. **Static file cache.** Removes the open/stat/read/close and the per-request malloc of file contents for hot files.
3. **Cache or eliminate the per-request `realpath()`.**
4. **Slab allocator for connection structs.** Eliminates per-request malloc, improves locality, reduces fragmentation under sustained load.
5. **`writev(2)` for the response.** Sends the header buffer and the body in one syscall without concatenating them into a single allocation first.
6. **`sendfile(2)` above a size threshold.** Avoids reading arbitrarily large files fully into memory per request. The crossover point against the in-process cache would have to be measured.
7. **Measure off-box.** Every number in this README has the load generator on the same 4 cores as the server.

These are understood and deferred, not pending TODOs.

---

## Testing

```bash
./valgrind_run.sh           # memory audit under traffic
./conduit-test-runner.sh    # functional tests (TRD-00 through TRD-06)
./bench.sh -h               # load sweep harness
```

The valgrind run exercises 200, 404, 405, and idle-timeout paths under traffic, then verifies clean shutdown with zero bytes leaked. It may take some time to finish due to testing the idle timeout.

```bash
wrk -t8 -c50 -d10s --latency http://localhost:8080/index.html
```

---

## Honest Framing

This is a learning project. It was built to a defined curriculum across eight milestones (TRD-00 through TRD-07). It is not production software. It does not compete with nginx, Apache, or any other established servers that have decades of optimization, security hardening, and ecosystem integration. Conduit's purpose is to demonstrate that I understand what those servers do at the systems-call level, and to provide a substrate for talking about epoll, non-blocking I/O, signal handling, concurrency design, memory safety, and the trade-offs in each.

If you're reviewing this for a systems or infrastructure engineering role: the design decisions and known limitations sections above are the parts of the README I most want you to read. The benchmark numbers are evidence; the analysis of *why* those numbers look the way they do is the actual signal.