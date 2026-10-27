# NUS Native Code Generation — through Milestone 12

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

The native backend currently lowers:

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
- native drop/drop-if flags and recursive drop glue;
- generic `Vec<T>` runtime operations;
- user enum / `Option` / `Result` tagged values;
- discriminant and variant-payload extraction.

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

MIR `drop` and `drop.if.init` lower to native initialization/drop flags. Heap-owned `string`, `Vec<T>`, structs/arrays containing owned values, and active enum payloads receive recursive drop glue. A move clears the source initialization flag so the backend cannot double-destroy transferred ownership. User-defined `Drop` implementations remain deferred.

## Deliberate limitations

Milestone 9 is not yet a complete ABI/runtime implementation. Native code generation does not yet promise support for:

- user-defined `Drop` implementations;
- slices created from ranges;
- dynamic dispatch or function pointers;
- async/concurrency runtime;
- networking runtime;
- cross compilation;
- optimized LLVM IR.

The backend is intentionally `-O0` and correctness-oriented first.

## Milestone 10 additions

The Windows host target now uses the GNU ABI (`x86_64-w64-windows-gnu`) and MinGW-w64 rather than implicitly relying on MSVC libraries. Runtime codegen now imports heap primitives, emits real drop glue/drop flags, represents `string` and `Vec<T>` as three-word runtime values, and exposes a target-aware layout engine.

## Milestone 11 additions

Generic source functions are monomorphized before MIR. The LLVM backend never receives unresolved type parameters: each used specialization has a concrete symbol such as `identity$g$i32` and a fully concrete signature.

Generic `Vec<T>` operations use the concrete LLVM element type. `vec_push` derives element size using a null GEP/`ptrtoint` sequence instead of a hard-coded element width, and vector drop glue walks non-Copy elements before releasing the allocation.

`Option<T>` and `Result<T,E>` now lower to native tagged values. User enums lower to a discriminant plus payload storage; MIR explicitly extracts discriminants/payloads for `match`, while `?` lowers to a branch that returns the error/empty variant early. Drop glue destroys only the active non-Copy payload.


## Windows process execution

Native linking is launched directly rather than through `cmd.exe`. Generated executables used by integration tests are also started with `CreateProcessW`, with stdout captured through an inherited pipe. The child process receives the configured MinGW-w64 `bin` directory in `PATH`, avoiding shell quoting issues and missing runtime DLLs. Test output is normalized from CRLF to LF before comparison.

## Milestone 12 additions

User-defined enums, `Option<T>` and `Result<T,E>` are native tagged values. `match` is lowered to discriminant-driven CFG blocks, and postfix `?` becomes an explicit success/error branch with early return. The backend emits variant construction, discriminant extraction, payload extraction, and active-variant drop glue. The current user-enum representation flattens variant payload fields into one aggregate rather than overlapping them, favoring correctness and simple lowering over compact size.

## Milestone 13 additions

Trait method contracts and associated types are fully resolved before backend lowering. Dispatch remains static: a concrete trait/inherent method call is lowered to a direct symbol rather than a vtable lookup.

Methods from generic `impl<T>` blocks are monomorphized on demand. Semantic analysis infers the impl substitutions from the concrete receiver, HIR emits a concrete specialization, and MIR/LLVM never see the unresolved impl type parameters.

`Vec<T>` now supports native instance operations `push`, `len`, `get` and `into_iter`. Consuming `for` lowers a vector into an owned `VecIter<T>` runtime aggregate containing `{ data, len, cap, index }`. Iterator MIR checks `index < len`, loads the current element, advances the index, and drop glue releases only the not-yet-consumed suffix on early destruction.

## Milestone 14 additions

Borrowed vector iterators now lower natively. `VecRefIter<T>` and `VecMutIter<T>` use `{ data, len, index }`; `IterNext` yields an element pointer, so shared iteration produces `&T` and mutable iteration produces `&mut T` without copying the element. These iterator values carry MIR loan provenance and never free the vector allocation.

Reference-return calls also carry their originating loan through MIR. The current source-level lifetime model uses conservative elision: a free function returning a reference must have one unambiguous borrowed input, while a borrowed method receiver is the preferred origin.

Consuming `VecIter<T>` no longer requires `T: Copy`. Non-Copy loop bindings receive conditional per-iteration drop cleanup, and the iterator owns only the still-unconsumed suffix. Mutable dereference assignment lowers to `store.deref`, enabling code such as `*item += 1` inside `iter_mut()` loops.

## Milestone 15 native networking lowering

The LLVM backend lowers the networking builtins directly to the host socket ABI. POSIX targets declare `socket`, `connect`, `bind`, `send`, `sendto`, `recv`, `close`, `htonl` and `htons`. Windows GNU targets declare the Winsock equivalents and emit a lazy `nus_net_init` helper around `WSAStartup`.

The Windows native toolchain adds `-lws2_32` automatically. The supported Windows ABI remains `x86_64-w64-windows-gnu`; no MSVC runtime/toolset is introduced.

`Vec<u8>` is the wire-buffer representation. Receive operations allocate a vector backing store with `malloc`, set length to the number of bytes returned by the OS, and transfer that allocation into `Result<Vec<u8>, NetworkError>`. Existing Result/drop lowering therefore owns all cleanup paths.
