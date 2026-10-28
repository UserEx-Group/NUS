# NUS Structured Concurrency — Milestone 19

Milestone 19 layers task ownership, join handles, cancellation, timers and borrowing task scopes on top of the resumable Future ABI introduced in M18. The runtime remains single-threaded and cooperative: tasks only make progress when polled by an `Executor`, `TaskScope`, `block_on`, or another async state machine.

## Timers

`sleep_ms(ms) -> Future<unit>` is backed by a monotonic deadline. A pending sleep frame publishes its remaining timeout through the common Future wait header. The executor combines that timeout with socket readiness and performs one `poll` / `WSAPoll` wait rather than busy-spinning.

```nus
async fn delayed() -> i32 {
    await sleep_ms(50u32);
    42
}
```

POSIX uses `clock_gettime(CLOCK_MONOTONIC, ...)`; Windows uses `GetTickCount64`.

## TaskHandle<T>

`task_spawn` schedules a Future and returns an owning handle:

```nus
let mut executor = executor_new();
let handle = task_spawn(&mut executor, worker());
let joined = task_join(&mut executor, handle);
```

`TaskHandle<T>` is non-`Copy`. In M19 `T` must satisfy `Copy`; this keeps the type-erased shared task result state small and avoids introducing result-specific drop callbacks before the next runtime ABI revision. `unit` is supported.

`task_join` consumes the handle and drives the owning executor until that task finishes. It returns `Option<T>`:

- `Some(value)` — normal completion.
- `None` — the task was cancelled.

Each executor has a stable runtime ID that is preserved across moves. A handle records that ID when spawned, and `task_join` asserts that it is driven by the same executor. This prevents an accidental join through an unrelated queue from reading uninitialized task state.

Fallible tasks need no special exception mechanism; errors are ordinary Copy results:

```nus
async fn request() -> Result<i32, i32> {
    await yield_now();
    err::<i32, i32>(7)
}

let handle = task_spawn(&mut executor, request());
let joined = task_join(&mut executor, handle);
```

The joined value is `Option<Result<i32, i32>>`.

## Cancellation

Cancellation is cooperative at scheduler poll boundaries:

```nus
let mut handle = task_spawn(&mut executor, slow());
let requested = task_cancel(&mut handle);
let result = task_join(&mut executor, handle);
```

`task_cancel` returns `true` only when it successfully transitions a live task into the cancellation-requested state. The next executor scan destroys the coroutine frame, marks the shared task state cancelled/finished and wakes join logic. `task_finished(&handle)` observes completion without consuming the handle.

Dropping an Executor cancels and destroys all queued tasks. Dropping the last `TaskHandle` releases its shared state.

## TaskScope and borrowing tasks

Detached tasks cannot safely borrow arbitrary stack locals because the Executor can outlive those locals. `TaskScope` provides a structured alternative:

```nus
async fn add_one(value: &mut i32) {
    await yield_now();
    *value += 1;
}

fn main() {
    let mut value = 41;
    let mut tasks = scope_new();

    scope_spawn(&mut tasks, add_one(&mut value));
    scope_run(&mut tasks);

    print(value);
}
```

The FlowChecker transfers the Future's loan provenance into the `TaskScope`. The borrowed roots remain protected until the scope's last use. Therefore this is rejected:

```nus
scope_spawn(&mut tasks, add_one(&mut value));
value = 0; // rejected: the scoped task can still access value
scope_run(&mut tasks);
```

After `scope_run` (when it is also the scope's last use), NLL releases the synthetic scoped loans and the local can be used normally again.

`scope_cancel_all` explicitly destroys every queued scoped Future. Dropping the scope performs the same cleanup.

## Scheduler waiting

When every runnable task is pending, the runtime aggregates all wait metadata:

```text
socket task A ── fd/event ┐
socket task B ── fd/event ├── poll / WSAPoll
sleep task C  ── timeout  ┘
```

The minimum timer deadline becomes the poll timeout. Purely cooperative tasks such as `yield_now` force a short wakeup so they cannot be starved behind indefinite socket waits.

## Source-level syntax boundary

`spawn` and `scope` remain reserved keywords, but M19 deliberately exposes explicit bootstrap APIs (`task_spawn`, `task_join`, `scope_spawn`, ...). This lets the ownership/lifetime model stabilize before syntax sugar introduces an implicit current executor or lexical task scope.

## Current boundaries

M19 is still single-threaded. There is no work stealing, thread pool, preemption, panic propagation, cancellation token tree, nominal task lifetime parameter, non-Copy `TaskHandle<T>` result, or source-level `spawn { ... }` / `scope { ... }` sugar yet. `TaskScope` currently accepts `Future<unit>` tasks, and joining requires the same Executor that created the handle.
