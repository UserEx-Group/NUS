# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> **High-level by default. Low-level by choice. Safe by default.**

## Milestone 11 — Generics & Traits Foundations

NUS now has compile-time generic functions, marker-trait bounds, monomorphization, generic `Vec<T>` runtime operations, and semantic foundations for `Option<T>` and `Result<T, E>`.

```text
.nus
 ↓
Lexer → Parser → AST
 ↓
SemanticAnalyzer
 ├─ generic inference
 ├─ trait bounds
 └─ instantiation registry
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

## Build the compiler

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Windows: LLVM + MinGW-w64, no MSVC

NUS uses the GNU Windows target:

```text
x86_64-w64-windows-gnu
```

For the standalone toolchains used during development:

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

No MSVC runtime or Visual Studio toolset is required for this target.

### Windows native test execution

Milestone 11.1 executes generated Windows binaries directly with `CreateProcessW` rather than routing them through `cmd.exe`. The child process receives `<NUS_MINGW>\bin` at the front of its `PATH`, so native tests can locate MinGW-w64 runtime DLLs independently of the shell used to start CTest. Captured CRLF output is normalized to LF before assertions.

## Generic functions

```nus
fn identity<T>(value: T) -> T {
    value
}

fn main() {
    let number = identity(42);
    let flag = identity(true);
}
```

NUS monomorphizes these calls into concrete functions such as:

```text
identity$g$i32
identity$g$bool
```

Only concrete specializations reach MIR and LLVM.

## Trait bounds

```nus
fn duplicate<T: Copy>(value: T) -> T {
    value
}
```

Marker traits are also supported:

```nus
trait NetworkValue {}

struct Packet {
    value: i32,
}

impl NetworkValue for Packet {}

fn route<T: NetworkValue>(value: &T) -> i32 {
    1
}
```

Milestone 11 traits are compile-time markers. Trait methods, associated types and dynamic dispatch are intentionally deferred.

## Generic `Vec<T>`

The milestone 10 `vec_*_i32` functions remain compatibility shims. New code uses generic operations:

```nus
let mut values: Vec<i32> = vec_new::<i32>();

vec_push(&mut values, 10);
vec_push(&mut values, 20);
vec_push(&mut values, 30);

print(vec_len(&values));
print(vec_get(&values, 1));
```

The backend computes the element stride from the concrete LLVM type, so the runtime is no longer hard-coded to `i32`.

Owned values can also be moved into vectors:

```nus
let literal = "owned";
let owned = string_clone(&literal);

let mut values: Vec<string> = vec_new::<string>();
vec_push(&mut values, owned);
```

`Vec<T>` drop glue destroys non-Copy elements before freeing the buffer. `vec_get` currently requires `T: Copy` to prevent accidental duplication of ownership.

## `Option<T>` and `Result<T, E>`

The type checker now understands:

```nus
let a: Option<i32> = some(42);
let b: Option<i32> = none::<i32>();

let c: Result<i32, string> = ok::<i32, string>(7);
let d: Result<i32, string> = err::<i32, string>("error");
```

These constructors are semantic-only in Milestone 11; native construction, matching and `?` lowering are future work.

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

Milestone 11 contains fourteen suites:

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
```

The generic native test verifies actual monomorphized functions plus generic `Vec<i32>` and `Vec<string>` behavior through LLVM and a native executable.

## Current boundaries

Still deferred: generic structs, generic methods, trait methods, `where` clauses, associated types, trait objects, native `Option`/`Result` lowering, reference-return lifetimes, user-defined `Drop`, allocator APIs and optimized vector growth.

See [`docs/GENERICS.md`](docs/GENERICS.md), [`docs/ABI.md`](docs/ABI.md), [`docs/RUNTIME.md`](docs/RUNTIME.md), [`docs/CODEGEN.md`](docs/CODEGEN.md) and [`docs/FLOW.md`](docs/FLOW.md).
