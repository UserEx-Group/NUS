# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 8 — MIR dataflow, NLL and drop elaboration

Milestone 8 moves ownership decisions out of the source-level semantic pass and onto MIR control-flow analysis.

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
    ├── names
    ├── types
    ├── mutability
    └── method resolution
    ↓
Typed HIR
    ↓
MIR + CFG
    ↓
Liveness Analysis
    ↓
FlowChecker
    ├── move-state dataflow
    ├── use-after-move across branches
    ├── non-lexical borrow activity
    └── borrow/move conflicts
    ↓
DropElaborator
    ├── drop
    └── drop.if.init
    ↓
future native backend
```

### Non-lexical borrowing

Borrow lifetimes are no longer forced to the end of the lexical block in the normal compiler pipeline.

This is valid:

```nus
struct Packet { value: i32 }

fn main() {
    let mut packet = Packet { value: 1 };

    let view = &packet;
    print(view.value);

    // `view` is no longer live here, so its shared loan has ended.
    packet.value = 2;
}
```

The liveness pass computes local `live_in`, `live_out`, and instruction-level before/after sets. A loan is considered active only while at least one reference local carrying that loan is live.

### Move-state dataflow

Moves are tracked over the CFG rather than only by source order. At a merge point, NUS can detect path-dependent moves:

```nus
let packet = Packet { value: 1 };

if condition {
    consume(packet);
}

print(packet.value); // rejected: packet may have been moved
```

Reinitialization restores availability:

```nus
let mut packet = Packet { value: 1 };
let old = packet;
packet = Packet { value: 2 };
print(old.value, packet.value);
```

### Drop elaboration

Owned, non-`Copy` locals receive explicit MIR destruction before returns.

```text
drop [packet] %0
-> return unit
```

When ownership differs between control-flow paths, MIR emits a conditional drop placeholder:

```text
drop.if.init [packet] %0
```

A native backend will later lower this to a concrete initialization/drop flag.

### Flow inspection

Inspect MIR liveness and loans with:

```bash
./build/nusc example.nus --flow
```

Example:

```text
MIR FLOW
  fn main
    loans:
      L0: %3 -> %0 shared
    bb0: live_in={} live_out={}
      #2 before={%0} after={%0, %3}
      ...
```

### MIR inspection

`--mir` now prints MIR after drop elaboration:

```bash
./build/nusc example.nus --mir
```

### Legacy lexical ownership checker

The old Milestone 6 lexical ownership implementation is still available internally as a regression/reference mode for tests, but it is no longer the normal compilation policy. Production compilation uses MIR flow analysis for move and borrow lifetimes.

## Safe restrictions still active

Full lifetime inference is not complete. NUS still conservatively rejects:

- returning references;
- reference fields inside structs;
- references escaping nested block expressions in cases the frontend cannot yet prove safe;
- partial moves of individual struct fields as a first-class ownership state.

The current NLL implementation is local/CFG-based and does not yet model arbitrary interprocedural lifetimes.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

AST after all safety checks:

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

Flow/liveness information:

```bash
./build/nusc example.nus --flow
```

MIR with drop elaboration:

```bash
./build/nusc example.nus --mir
```

## Tests

Milestone 8 has eight suites:

```text
lexer
parser
semantic
ownership
hir
mir
flow
drop
```

Run them with:

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
    ↓
HirBuilder
    ↓
MirBuilder
    ↓
CfgVerifier
    ↓
LivenessAnalysis
    ↓
FlowChecker
    ↓
DropElaborator
    ↓
[Native Codegen]
```

## Next milestone

Milestone 9 should begin executable code generation rather than add more surface syntax. The recommended path is:

1. target/data-layout abstraction;
2. struct layout and alignment;
3. function ABI lowering;
4. LLVM IR backend;
5. integer/float operations;
6. branches and loops;
7. stack locals and references;
8. calls and basic runtime intrinsics;
9. link a first native NUS executable.
