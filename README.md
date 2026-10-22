# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> **High-level by default. Low-level by choice. Safe by default.**

## Milestone 10 — Runtime & ABI Foundations

NUS now has a concrete native ABI model, heap-backed runtime values and real native drop glue.

```text
.nus
 ↓
Lexer → Parser → AST
 ↓
SemanticAnalyzer
 ↓
Typed HIR
 ↓
MIR / CFG
 ↓
Liveness + NLL + Move Dataflow
 ↓
DropElaborator
 ↓
ABI / Layout + LLVM IR
 ↓
clang + native GNU toolchain
 ↓
native executable
```

The bootstrap backend still emits textual LLVM IR. Runtime operations are lowered to the platform C ABI (`malloc`, `realloc`, `free`, `memcpy`, `printf`, `abort`) while NUS develops its own standard library/runtime layer.

## Build the compiler

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Windows: LLVM + MinGW-w64, no MSVC

The default Windows ABI is now:

```text
x86_64-w64-windows-gnu
```

NUS does **not** require MSVC for its Windows GNU target. Configure standalone LLVM and MinGW-w64 when they are not discoverable automatically:

```powershell
$env:NUS_CLANG = "C:\LLVM\LLVM\bin\clang.exe"
$env:NUS_MINGW = "C:\mingw64"
```

Then:

```powershell
.\build\nusc.exe .\example.nus --build
.\example.exe
```

The native toolchain invokes the equivalent of:

```text
clang --target=x86_64-w64-windows-gnu --gcc-toolchain=C:\mingw64 ...
```

`NUS_MINGW` is checked first; common `C:\mingw64` and `C:\msys64\mingw64` installations are fallback locations.

## Heap-backed `string`

A runtime `string` now uses a three-word ABI:

```text
{ data pointer, length, capacity }
```

String literals use `capacity = 0`, which marks borrowed static storage. `string_clone` creates owned heap storage:

```nus
let literal = "network";
let owned = string_clone(&literal);

print(string_len(&owned), owned);
```

Owned strings are freed automatically by native drop glue. Moving a string transfers its drop flag, preventing double-free.

## `Vec<T>` foundation

The type system now understands generic vector types such as:

```nus
Vec<i32>
```

The ABI is also three words:

```text
{ data pointer, length, capacity }
```

Until generic functions are implemented, Milestone 10 exposes bootstrap `i32` vector operations:

```nus
let mut values: Vec<i32> = vec_new_i32();

vec_push_i32(&mut values, 10);
vec_push_i32(&mut values, 20);
vec_push_i32(&mut values, 30);

print(vec_len_i32(&values));
print(vec_get_i32(&values, 1));
```

These helpers are intentionally temporary; the final standard library should expose generic `Vec<T>` methods.

## ABI layout inspection

```bash
./build/nusc example.nus --layout
```

On x86-64 this includes output similar to:

```text
target x86_64-... (pointer width: 64 bits)
string size=24 align=8
Vec<i32> size=24 align=8
struct Packet size=16 align=8
  source offset=0 size=4 align=4
  destination offset=4 size=4 align=4
  payload_size offset=8 size=8 align=8
```

## Compiler inspection modes

```text
--tokens       lexer output
--check        validate through MIR ownership flow
--hir          typed HIR
--mir          MIR after drop elaboration
--flow         liveness/loan information
--layout       native ABI sizes, alignments and field offsets
--emit-llvm    LLVM IR
--build        native executable
```

## Tests

Milestone 10 contains twelve suites:

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
```

The native/runtime suites compile LLVM IR with `clang` and execute generated NUS binaries when the native toolchain is available.

## Current boundaries

Milestone 10 establishes the runtime foundation, not the final standard library. Generic functions/methods, user-defined `Drop`, reference-return lifetimes, reference fields, allocators, fallible allocation and optimized vector growth remain future work.

See [`docs/ABI.md`](docs/ABI.md), [`docs/RUNTIME.md`](docs/RUNTIME.md), [`docs/CODEGEN.md`](docs/CODEGEN.md) and [`docs/FLOW.md`](docs/FLOW.md).
