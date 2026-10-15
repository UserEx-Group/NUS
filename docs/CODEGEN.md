# NUS Native Code Generation — Milestone 9

Milestone 9 introduces the first executable backend for NUS.

## Pipeline

```text
NUS source
  ↓
Lexer / Parser / AST
  ↓
Semantic analysis
  ↓
Typed HIR
  ↓
MIR + CFG
  ↓
Flow checking / NLL
  ↓
Drop elaboration
  ↓
LLVM textual IR
  ↓
clang
  ↓
native executable
```

NUS currently emits textual LLVM IR rather than linking against LLVM's C++ API. This keeps the compiler bootstrap small and makes the backend boundary explicit. `clang` is used as the LLVM assembler and native linker.

## CLI

Inspect LLVM IR:

```bash
nusc program.nus --emit-llvm
```

Build a native executable:

```bash
nusc program.nus --build
```

On Windows this creates `program.exe`. On Unix-like systems it creates `program`.

`clang` must be available on `PATH`, or set `NUS_CLANG`:

```bash
NUS_CLANG=/path/to/clang nusc program.nus --build
```

PowerShell:

```powershell
$env:NUS_CLANG = "C:\\LLVM\\bin\\clang.exe"
.\\build\\nusc.exe .\\program.nus --build
```

## Current native ABI model

- NUS functions are mangled with a `nus_` prefix.
- Methods become ordinary functions such as `nus_Packet__route_key`.
- `&T` and `&mut T` lower to LLVM opaque pointers (`ptr`).
- Structs lower to named LLVM aggregate types.
- Primitive integers keep their bit width.
- `usize`/`isize` use the host compiler pointer width.
- NUS `main()` lowers to an internal `nus_main`, with a C ABI `i32 @main()` wrapper.

## Supported MIR lowering

Milestone 9 lowers:

- local variables and assignments;
- integer, floating point, boolean, char and string literals;
- arithmetic, comparisons, bitwise operations and shifts;
- `if`, `while`, basic-block branches and gotos;
- direct function calls;
- struct construction and field load/store;
- references and method receivers;
- arrays and direct indexing;
- ranges and range iterator MIR operations;
- built-in `print` and `assert`;
- trivial drop/drop-if placeholders.

## Built-in print

`print` currently lowers through the platform C `printf` ABI. It supports:

- bool;
- signed/unsigned integers;
- floats;
- char;
- string;
- references/pointers.

This is a bootstrap implementation. A NUS runtime/standard-library I/O layer will replace direct `printf` lowering later.

## Contextual literals

The frontend can accept code such as:

```nus
struct Packet { payload_size: usize }
let packet = Packet { payload_size: 1500 };
```

The literal begins as the default `i32` type in the frontend. The native backend now materializes the required numeric coercion (`i32` → `usize`) when emitting LLVM IR.

## Drops

MIR `drop` and `drop.if.init` are recognized by the backend. They are currently no-ops for the types that can reach native code because NUS has not yet introduced user-defined destructors or heap-owning native runtime objects.

The explicit MIR instructions are preserved so later milestones can lower them to destructors and drop flags without changing the frontend.

## Deliberate limitations

Milestone 9 is not yet a complete ABI/runtime implementation. Native code generation does not yet promise support for:

- user-defined `Drop` implementations;
- heap allocation;
- full string ownership;
- slices created from ranges;
- dynamic dispatch or function pointers;
- async/concurrency runtime;
- networking runtime;
- cross compilation;
- optimized LLVM IR.

The backend is intentionally `-O0` and correctness-oriented first.
