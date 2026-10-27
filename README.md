# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> **High-level by default. Low-level by choice. Safe by default.**

## Milestone 13 — Trait Interfaces, Generic Impls & Owned Iterators

NUS now has statically checked trait methods and associated types, generic `impl<T>` blocks with on-demand monomorphization, idiomatic `Vec<T>` instance methods, and consuming vector iteration through an owned `VecIter<T>` runtime value.

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

Milestone 13 intentionally does **not** expose borrowing `.iter()` / `.iter_mut()` yet. Those require reference-return lifetime relationships that NUS does not currently express. `for` over `Vec<T>` / `VecIter<T>` also currently requires `T: Copy` until per-iteration non-Copy ownership/drop accounting is complete.

## Enums and errors

Milestone 12 functionality remains native:

```nus
enum NetworkError {
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

Milestone 13 contains twenty suites:

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
```

The new native suites exercise concrete trait methods/associated types, generic trait impls, generic method specialization, `Vec<T>` method syntax, consuming `for`, explicit `into_iter`, and native iterator ownership/drop behavior.

## Current boundaries

Still deferred: user generic structs/enums, default trait methods, generic trait methods, `where` clauses, supertraits, associated constants, associated-type equality constraints, trait objects/dynamic dispatch, full generic-impl overlap/coherence checking, borrowing iterators, non-Copy loop yields, reference-return lifetimes, non-Copy payload bindings in `match`, user-defined `Drop`, allocator APIs and optimized vector growth.

See [`docs/TRAITS_ITERATORS.md`](docs/TRAITS_ITERATORS.md), [`docs/ENUMS.md`](docs/ENUMS.md), [`docs/GENERICS.md`](docs/GENERICS.md), [`docs/ABI.md`](docs/ABI.md), [`docs/RUNTIME.md`](docs/RUNTIME.md), [`docs/CODEGEN.md`](docs/CODEGEN.md) and [`docs/FLOW.md`](docs/FLOW.md).
