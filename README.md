# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 7 — Typed HIR + MIR/CFG foundation

Milestone 7 introduces the compiler's first intermediate representations. The frontend no longer needs to jump directly from AST/semantic analysis to a future native backend.

```text
.nus source
    ↓
Lexer
    ↓
Parser
    ↓
AST
    ↓
SemanticAnalyzer
    ↓
Typed HIR
    ↓
MIR + explicit CFG
    ↓
future optimization / ownership dataflow / codegen
```

### HIR

HIR is a typed, resolved representation built after semantic analysis. It adds:

- semantic types on expressions
- stable `LocalId` values for bindings
- explicit distinction between locals and globals
- shadowed variables represented by different local IDs
- normalized method receiver metadata
- typed struct declarations
- structured control-flow nodes retained for convenient lowering

Inspect it with:

```bash
./build/nusc example.nus --hir
```

A local looks roughly like:

```text
Let %0 packet: Packet mut
```

The numeric ID is compiler identity, not source syntax. Two variables with the same source name in different scopes receive different IDs.

### MIR

MIR lowers structured HIR into basic blocks with explicit instructions and terminators.

```text
bb0:
    %2 = binary Greater %0, `0`
    -> branch %2 ? bb1 : bb2

bb1:
    ...
    -> goto bb3

bb2:
    ...
    -> goto bb3

bb3:
    -> return unit
```

Current MIR concepts include:

- locals and compiler temporaries
- assignment
- unary and binary operations
- explicit borrows
- calls
- qualified method calls
- field access and field stores
- indexing and indexed stores
- arrays, ranges and struct construction
- `if` branches
- `while` and `loop` back-edges
- abstract iterator lowering for `for`
- `break` / `continue` edges
- return terminators
- unreachable blocks

Inspect it with:

```bash
./build/nusc example.nus --mir
```

### Method lowering

Method syntax:

```nus
packet.set_source(64);
```

is no longer represented as an opaque member call in MIR. It is normalized toward a qualified function call:

```text
%8 = borrow [mut] %packet
call @Packet::set_source, %8, `64`
```

A consuming `self` receiver is passed by value. `&self` and `&mut self` receivers materialize the appropriate implicit borrow when required.

### CFG verification

`CfgVerifier` validates generated MIR before it is printed or handed to later passes. It currently checks:

- functions have basic blocks
- entry blocks exist
- every block has a terminator
- `goto` targets exist
- both branch targets exist
- block IDs are unique

Invalid compiler-generated MIR is reported as an internal compiler error rather than silently continuing.

## Ownership status

The Milestone 6 ownership/borrowing checker remains active before HIR lowering:

- non-`Copy` moves
- use-after-move rejection
- `&T` shared borrowing
- `&mut T` exclusive borrowing
- lexical borrow scopes
- temporary call borrows
- receiver ownership semantics
- reinitialization after move

Milestone 7 does **not** yet replace that checker with MIR dataflow. It creates the representation required to do so correctly in the next phase.

## Safe restrictions still active

Full lifetime inference is not implemented yet. NUS still conservatively rejects:

- returning references
- reference fields inside structs
- borrowed values escaping nested block expressions

The compiler prefers rejecting code it cannot prove safe over accepting potential dangling references.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

AST after full semantic checking:

```bash
./build/nusc example.nus
```

Tokens:

```bash
./build/nusc example.nus --tokens
```

Check only:

```bash
./build/nusc example.nus --check
```

Typed HIR:

```bash
./build/nusc example.nus --hir
```

MIR / CFG:

```bash
./build/nusc example.nus --mir
```

## Tests

The project now has six test suites:

```text
lexer
parser
semantic
ownership
hir
mir
```

Run all tests with:

```bash
ctest --test-dir build --output-on-failure
```

## Current architecture

```text
SourceManager
    ↓
Lexer
    ↓
Parser
    ↓
AST
    ↓
SemanticAnalyzer
    ├── name resolution
    ├── nominal types
    ├── method resolution
    ├── type checking
    └── lexical ownership / borrowing
    ↓
HirBuilder
    ├── typed expressions
    ├── unique locals
    └── receiver normalization metadata
    ↓
MirBuilder
    ├── temporaries
    ├── basic blocks
    ├── explicit control-flow edges
    ├── explicit borrows
    └── normalized calls
    ↓
CfgVerifier
```

## Next milestone

Milestone 8 should use MIR for control-flow-sensitive safety rather than adding more surface syntax:

1. MIR predecessor/successor graph
2. move-state dataflow per basic block
3. non-lexical borrow ranges
4. liveness analysis
5. deterministic drop points
6. drop elaboration groundwork
7. reference escape analysis on CFG

After that, NUS will be in a strong position to begin a real native backend without baking ownership rules directly into LLVM generation.
