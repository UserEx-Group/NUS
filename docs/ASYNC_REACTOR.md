# NUS Async and Reactor Foundations — Milestone 17

Milestone 17 introduces two related but deliberately separate foundations: a real native readiness reactor and the first compiler/runtime representation of `async fn`, `Future<T>` and `await`.

## Async functions

An async function has a normal source-level result type, but callers receive `Future<T>`:

```nus
async fn plus_one(value: i32) -> i32 {
    value + 1
}

async fn compute() -> i32 {
    let first = await plus_one(40);
    await plus_one(first)
}

fn main() {
    print(block_on(compute()));
}
```

In Milestone 17 these futures are **eager/ready futures**. Calling `compute()` executes its body immediately and returns a ready `Future<i32>`. `await` validates and unwraps that ready future; `block_on` is the synchronous bridge used by non-async code.

This is intentionally not yet a task executor or coroutine state machine. No source-level async function suspends and resumes in M17. The compiler does, however, carry `Future<T>` explicitly through semantic analysis, HIR, MIR and LLVM, which provides the representation boundary needed by a later state-machine/executor milestone.

`?` works inside async functions. An early `Err`/`None` path is wrapped into the async function's `Future<Result<...>>` / `Future<Option<...>>`, preserving ownership before returning.

Current async restrictions include: `main` cannot be async, async methods are deferred, and an async function cannot return a borrowed reference.

## Non-blocking sockets

Owned socket resources can be switched to non-blocking mode:

```nus
tcp_set_nonblocking(&mut stream, true)?;
tcp_listener_set_nonblocking(&mut listener, true)?;
udp_set_nonblocking(&mut socket, true)?;
```

POSIX uses `fcntl(F_GETFL/F_SETFL, O_NONBLOCK)`. Windows GNU uses Winsock `ioctlsocket(FIONBIO)`.

## Reactor

`Reactor` is an owned, non-`Copy` runtime value. It stores NUS-owned registration records and converts those records to native polling structures only at the OS boundary.

```nus
let mut reactor = reactor_new()?;
reactor_register_listener(&mut reactor, &listener, 7usize)?;
let ready = reactor_wait(&mut reactor, 1000)?;
```

Available registration functions are:

```text
reactor_register_tcp
reactor_register_listener
reactor_register_udp
```

Each registration has a user token. After `reactor_wait`, readiness can be inspected with:

```text
reactor_len
reactor_event_token
reactor_event_readable
reactor_event_writable
```

The host backend uses `poll()` on POSIX and `WSAPoll()` on Windows. A zero timeout is a non-blocking poll.

## Resource pins

A reactor stores OS handles rather than owning the corresponding NUS socket objects. To keep this safe, registration creates an internal MIR/NLL **resource pin**.

A pin prevents the registered `TcpStream`, `TcpListener` or `UdpSocket` from being moved, reassigned or dropped while the live Reactor can still reference its handle. Unlike a normal shared borrow, a pin does **not** prevent ordinary mutable I/O such as `tcp_receive(&mut stream)` or `tcp_accept(&mut listener)`.

The pin follows Reactor moves inside the function and ends at the Reactor's last live use/drop. M17 requires registration arguments to have directly traceable local owners; hiding registration behind a helper that receives only borrowed Reactor/socket parameters is rejected until interprocedural resource effects are represented explicitly.

## Portable errors

Reactor/poll failures use networking operation code `11`. Platform-specific `errno`/Winsock values remain backend details and are mapped to the existing portable `NetworkError` reason codes.

## Next step

The intended follow-up is to lower async functions into resumable state machines and drive pending futures from the Reactor. That will turn the eager `Future<T>` representation introduced here into true suspended asynchronous execution without changing the source-level `async fn` / `await` syntax.
