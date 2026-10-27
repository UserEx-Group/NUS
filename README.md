# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> **High-level by default. Low-level by choice. Safe by default.**

## Milestone 17 — Native Reactor + Async Foundations

Milestone 17 adds non-blocking socket mode, a real cross-platform readiness reactor (`poll` on POSIX, `WSAPoll` on Windows), resource-pin lifetime safety, and the first explicit `Future<T>` / `async fn` / `await` pipeline. Async functions are eager/ready in this milestone; true suspension and executor scheduling remain a later step. The VS Code extension from Milestone 16.5 remains bundled and now highlights the M17 runtime/types while retaining VS Code 1.32+ compatibility.

```text
.nus
 ↓
Lexer → Parser → AST
 ↓
SemanticAnalyzer
 ├─ generic inference / trait bounds
 ├─ trait contracts + associated types
 ├─ generic impl matching
 └─ generic function/method instantiation registry
 ↓
Monomorphized typed HIR
 ↓
MIR / CFG
 ↓
Liveness + NLL + Move Dataflow
 ↓
DropElaborator
 ↓
ABI / LLVM IR
 ↓
clang + native GNU toolchain
 ↓
native executable
```

## VS Code extension

Milestone 16.5 adds a dependency-free VS Code extension under `editors/vscode`. It registers `.nus`, provides TextMate syntax highlighting, NUS snippets, language configuration, and commands that invoke the real `nusc` for `--check` and `--build`. Compiler diagnostics are surfaced as editor diagnostics instead of being reimplemented in JavaScript.

Open `editors/vscode` in VS Code and press `F5` to launch an Extension Development Host. See `docs/VSCODE.md` for setup, compiler discovery and packaging.

## Build the compiler

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Windows: LLVM + MinGW-w64, no MSVC

NUS uses the GNU Windows target:

```text
x86_64-w64-windows-gnu
```

PowerShell setup used during development:

```powershell
$env:NUS_CLANG = "C:\LLVM\LLVM\bin\clang.exe"
$env:NUS_MINGW = "C:\mingw64"

.\build\nusc.exe .\example.nus --build
.\example.exe
```

The native toolchain passes the equivalent of:

```text
clang --target=x86_64-w64-windows-gnu --gcc-toolchain=C:\mingw64 ...
```

No MSVC runtime or Visual Studio toolset is required. Generated Windows binaries used by tests are executed directly with `CreateProcessW`; `<NUS_MINGW>\bin` is added to the child `PATH` and CRLF output is normalized before assertions.

## Trait methods and associated types

Traits can now define compile-time interfaces:

```nus
trait RouteKey {
    type Output;
    fn route_key(&self) -> Self::Output;
}

struct Packet {
    source: u32,
    destination: u32,
}

impl RouteKey for Packet {
    type Output = u32;

    fn route_key(&self) -> u32 {
        self.source + self.destination
    }
}
```

The semantic analyzer verifies associated-type bindings, receivers, parameters and return types. Dispatch remains static: no trait object or vtable reaches MIR/LLVM.

## Generic impls and method monomorphization

Generic implementation blocks are templates:

```nus
trait Counted {
    type Item;
    fn count(&self) -> usize;
}

impl<T> Counted for Vec<T> {
    type Item = T;

    fn count(&self) -> usize {
        self.len()
    }
}
```

A call on `Vec<i32>` resolves the generic impl, substitutes `T = i32`, and records a concrete method specialization. Only that specialization is emitted to HIR/MIR/LLVM.

Generic bounds can use these implementations:

```nus
fn require_counted<T: Counted>(value: T) -> i32 {
    1
}
```

## `Vec<T>` instance API

Construction remains a bootstrap generic function:

```nus
let mut values: Vec<i32> = vec_new::<i32>();
```

But normal operations now use method syntax:

```nus
values.push(10);
values.push(20);
values.push(30);

print(values.len());
print(values.get(1));
```

`push` transfers ownership, `len` borrows shared, and `get` currently requires `T: Copy` because it returns by value. The old global vector operations remain compatibility shims.

## Owned iteration

A vector can be consumed directly:

```nus
let mut sum = 0;

for value in values {
    sum += value;
}
```

or explicitly:

```nus
let iterator = values.into_iter();
for value in iterator {
    print(value);
}
```

The runtime iterator is:

```text
VecIter<T> {
    ptr data,
    usize len,
    usize cap,
    usize index,
}
```

Iteration consumes the original vector. An iterator dropped before exhaustion destroys only its remaining owned elements and then frees the backing allocation.

Milestone 14 adds both borrowing forms:

```nus
for value in values.iter() {
    print(*value);
}

for value in values.iter_mut() {
    *value += 1;
}
```

Shared/mutable iterator loans remain attached to the original vector through MIR/NLL, so reallocating or directly using the vector incompatibly while an iterator is live is rejected. Owned iteration no longer requires `T: Copy`; yielded non-Copy values are conditionally dropped once per iteration.

## Elided reference lifetimes

A reference can now cross a function boundary when its origin is unambiguous:

```nus
fn identity(value: &i32) -> &i32 { value }
```

At the call site, the returned reference carries the original loan. NLL allows the owner to be mutated again after the returned reference's last use. Multiple borrowed inputs are rejected for reference returns until named lifetime syntax is introduced. Methods with `&self` / `&mut self` use the receiver as the elided output origin.

## Enums and errors

Milestone 12 functionality remains native:

```nus
enum AppError {
    Timeout,
    Refused,
}

fn connect(okay: bool) -> Result<i32, string> {
    let message = "connection refused";
    let result = if okay {
        ok::<i32, string>(8080)
    } else {
        err::<i32, string>(string_clone(&message))
    };

    let port = result?;
    ok::<i32, string>(port)
}
```

`match` is exhaustive and `?` lowers to explicit success/error CFG with ownership transfer on the early-return path.


## Native networking

Milestone 15 reserves five built-in networking types:

```nus
IpAddr
SocketAddr
NetworkError
TcpStream
UdpSocket
```

Addresses are constructed explicitly:

```nus
let local = socket_addr(ipv4(127, 0, 0, 1), 8080);
print(socket_port(&local));
```

TCP/UDP use binary `Vec<u8>` payloads and `Result`:

```nus
fn connect(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
    tcp_connect(address)
}

fn receive(stream: &mut TcpStream) -> Result<Vec<u8>, NetworkError> {
    tcp_receive(stream, 4096)
}
```

`TcpStream`, `TcpListener` and `UdpSocket` are non-`Copy`; their native handle is closed by drop glue. Windows lowering uses Winsock and links `ws2_32` automatically while retaining the LLVM + MinGW-w64 toolchain. See `docs/NETWORKING.md`.

## Async and reactor foundations

```nus
async fn plus_one(value: i32) -> i32 {
    value + 1
}

async fn compute() -> i32 {
    let value = await plus_one(41);
    value
}

fn main() {
    print(block_on(compute()));
}
```

M17 `Future<T>` values are eager/ready: `async fn` already has explicit type/IR lowering and ownership-aware `await`, but does not suspend yet. Separately, `Reactor` performs real OS readiness polling and supports non-blocking `TcpStream`, `TcpListener` and `UdpSocket` registrations. Registration creates an internal resource pin that keeps the OS handle owner alive without blocking normal mutable I/O. See `docs/ASYNC_REACTOR.md`.

## Compiler modes

```text
--tokens       lexer output
--check        validate through MIR ownership flow
--hir          typed/monomorphized HIR
--mir          MIR after drop elaboration
--flow         liveness/loan information
--layout       ABI sizes, alignments and field offsets
--emit-llvm    LLVM IR
--build        native executable
```

## Tests

Milestone 17 contains thirty-two suites:

```text
lexer
parser
semantic
ownership
hir
mir
flow
drop
codegen
native
layout
runtime
generics
generic_native
enums
enum_native
traits
trait_native
iterators
iterator_native
lifetimes
lifetime_native
borrowed_iterators
borrowed_iterator_native
network
network_native
network_services
network_services_native
async
async_native
reactor
reactor_native
```

The native suites now also resolve `localhost`, create and drop a real loopback TCP listener on an ephemeral port, and compile the `accept`, `setsockopt`, DNS and richer error paths through LLVM and the host linker.

## Current boundaries

Still deferred: named lifetime parameters/bounds, reference fields, arbitrary returned field/index projections, lifetime-bearing user-defined aggregates, user generic structs/enums, default trait methods, generic trait methods, `where` clauses, supertraits, associated constants, associated-type equality constraints, trait objects/dynamic dispatch, full generic-impl overlap/coherence checking, non-Copy payload bindings in `match`, user-defined `Drop`, allocator APIs, optimized vector growth, IPv6, multi-address DNS results, peer/local endpoint queries, UDP sender metadata, deregistration, true suspended async state machines/executor scheduling, async methods and TLS.

See [`docs/NETWORKING.md`](docs/NETWORKING.md), [`docs/TRAITS_ITERATORS.md`](docs/TRAITS_ITERATORS.md), [`docs/ENUMS.md`](docs/ENUMS.md), [`docs/GENERICS.md`](docs/GENERICS.md), [`docs/ABI.md`](docs/ABI.md), [`docs/RUNTIME.md`](docs/RUNTIME.md), [`docs/CODEGEN.md`](docs/CODEGEN.md) and [`docs/FLOW.md`](docs/FLOW.md).

For lifetime rules see `docs/LIFETIMES.md`; for sockets see `docs/NETWORKING.md`; for M17 async/reactor semantics see `docs/ASYNC_REACTOR.md`.
