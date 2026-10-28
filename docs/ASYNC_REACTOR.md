# NUS Resumable Async Runtime — Milestone 18

Milestone 18 turns the async syntax introduced in M17 into resumable execution. `async fn` is lowered to a heap-owned coroutine frame, `await` can return `Pending`, and `block_on` / `Executor` repeatedly poll the frame until it reaches `Ready`.

## Resumable async functions

```nus
async fn suspended(value: i32) -> i32 {
    await yield_now();
    value + 1
}

fn main() {
    print(block_on(suspended(41)));
}
```

An async function is emitted as three native pieces:

```text
constructor(args...) -> Future<T>
poll(frame, out)     -> bool ready
drop(frame)          -> unit
```

The constructor allocates the frame and captures parameters. The poll function switches on the saved continuation state. A pending `await` stores its continuation state and returns `false`; the next poll resumes directly at that await point. The drop callback destroys every still-live owned local before freeing the frame.

`Future<T>` is therefore non-`Copy` in M18 even when `T` is `Copy`.

## Future ABI

A Future carries either an immediate ready value or ownership of a suspended frame:

```text
Future<T> {
    bool ready,
    T value,          // omitted for unit
    ptr frame,
    ptr poll,
    ptr destroy,
}
```

Every suspendable frame starts with a common wait header containing continuation state, native wait handle/events, an optional Reactor pointer, and timeout metadata. `block_on` and the executor use that header to sleep rather than busy-spin.

## Cooperative suspension

`yield_now() -> Future<unit>` always returns `Pending` once before becoming ready. It exists primarily as the bootstrap scheduling primitive and as a deterministic test that a coroutine really leaves and re-enters its frame.

```nus
async fn task() {
    print("before");
    await yield_now();
    print("after");
}
```

## Executor

`Executor` is an owned task queue:

```nus
let mut executor = executor_new();
executor_spawn(&mut executor, task_a());
executor_spawn(&mut executor, task_b());
executor_run(&mut executor);
```

Each task entry owns a frame/poll/drop triple. The executor polls tasks cooperatively, removes completed tasks, and aggregates pending socket handles when every task is pending.

Detached tasks must currently be non-borrowing `Future<unit>` values. A Future carrying a loan to a stack-owned socket/buffer is rejected by `executor_spawn`; task lifetimes are not yet encoded in the public type system.

## Async TCP operations

M18 adds genuinely pending TCP futures:

```nus
let accepted = await tcp_accept_async(&mut listener);
let stream = accepted?;

let received = await tcp_receive_async(&mut stream, 4096usize);
let bytes = received?;

let sent = await tcp_send_async(&mut stream, &bytes);
let count = sent?;
```

These constructors switch the native socket to non-blocking mode and create a Future frame with the corresponding read/write readiness interest. Poll callbacks attempt the system call immediately; `EAGAIN` / `WSAEWOULDBLOCK` returns `Pending`, while success or a terminal error returns `Ready(Result<...>)`.

`block_on` and suspended parent futures use `poll()` on POSIX or `WSAPoll()` on Windows to wait for the handle before polling again.

The Future carries MIR loan provenance from its socket and, for send, its buffer. Those resources therefore cannot be moved or incompatibly mutated while the Future remains live.

## Reactor

The M17 `Reactor` remains available for explicit readiness-driven code. It owns registrations but not sockets, and resource pins prevent registered socket owners from moving/dropping while the Reactor is live. M18 futures can also place Reactor metadata in the common wait header, although the first built-in async TCP operations wait directly on their socket handles.

## Current boundaries

M18 is deliberately a cooperative single-threaded runtime. It does not yet provide multithreaded work stealing, timers/sleep futures, cancellation tokens, task handles/join values, async methods, explicit task lifetimes, or an async `main`. The source-level `async fn` / `await` representation is now resumable, so those features can be layered on the state-machine ABI rather than replacing it.

## Milestone 19 integration

M19 keeps the M18 state-machine ABI and extends the scheduler rather than replacing it. `sleep_ms` contributes a timeout to the same common Future wait header used by socket futures. When all tasks are pending, the Executor now aggregates all socket handles and the minimum timer timeout before one `poll` / `WSAPoll` wait. `TaskHandle<T>` adds join/cancel observation for Copy results, while `TaskScope` gives borrowing `Future<unit>` values a structured lifetime boundary. See `STRUCTURED_CONCURRENCY.md`.
