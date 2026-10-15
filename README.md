# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> **High-level by default. Low-level by choice. Safe by default.**

## Milestone 9 — Native Codegen

NUS can now compile a subset of `.nus` programs into real native executables.

```text
.nus
 ↓
Lexer
 ↓
Parser / AST
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
LLVM IR
 ↓
clang
 ↓
native executable
```

The backend emits textual LLVM IR. This deliberately avoids an LLVM C++ library dependency during the bootstrap phase; `clang` performs LLVM assembly and native linking.

## Build the compiler

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with a multi-config generator you may need:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Compile a NUS program

Inspect LLVM IR:

```bash
./build/nusc example.nus --emit-llvm
```

Build natively:

```bash
./build/nusc example.nus --build
```

Windows:

```powershell
.\build\nusc.exe .\example.nus --build
.\example.exe
```

If `clang` is not on PATH, configure it with `NUS_CLANG`.

## Current example

`example.nus` exercises structs, methods, references, NLL and ownership:

```nus
let mut packet = Packet {
    source: 10,
    destination: 20,
    payload_size: 1500,
};

inspect(&packet);
rewrite(&mut packet, 42);

let view = &packet;
print(view.source, view.payload_size);

packet.set_source(64);
packet.consume();
```

Its native output is:

```text
30
42 1500
64 20 1500
```

## Compiler inspection modes

```text
--tokens       lexer output
--check        validate through MIR ownership flow
--hir          typed HIR
--mir          MIR after drop elaboration
--flow         liveness/loan information
--emit-llvm    LLVM IR
--build        native executable
```

## Tests

Milestone 9 contains ten suites:

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
```

The `native` suite invokes `clang` and executes a generated NUS binary when clang is available. If clang is unavailable, that external integration portion is skipped without breaking frontend development.

## Native codegen status

Implemented in the first backend:

- primitive integer/float/bool/char lowering;
- string literals for bootstrap I/O;
- numeric contextual coercions;
- structs and fields;
- references as native pointers;
- value/shared/mutable method receivers;
- functions and direct calls;
- branches, loops and CFG terminators;
- arrays and indexing foundations;
- range iterator MIR foundations;
- built-in `print` and `assert`;
- C ABI entry-point wrapper.

See [`docs/CODEGEN.md`](docs/CODEGEN.md) for backend details and [`docs/FLOW.md`](docs/FLOW.md) for NLL/dataflow.

## Current safety restrictions

Full interprocedural lifetime inference is not complete. NUS still conservatively rejects reference returns and reference fields. User-defined destruction and heap-owning runtime types are also intentionally deferred until their native ABI can be designed explicitly.
