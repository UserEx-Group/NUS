# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 5 — User-defined types

The C++20 compiler frontend now supports the first nominal user-defined type model.

### Implemented

- source management, lexer, parser and AST
- Pratt expression parser and control flow
- lexical scopes, name resolution and primitive type checking
- `struct` declarations
- nominal struct types in parameters, returns and locals
- struct literals with required named fields
- field existence, duplicate-field and field-type validation
- field access such as `packet.source`
- field assignment with mutability propagation
- `impl` blocks
- method receivers: `self`, `&self`, `&mut self`
- method lookup and argument/return-type checking
- `&mut self` calls require a mutable receiver
- method bodies can access and mutate `self` according to the receiver
- AST debug output for structs, impls and struct literals

Example:

```nus
struct Packet {
    source: u32,
    destination: u32,
}

impl Packet {
    fn route_key(&self) -> u32 {
        self.source + self.destination
    }

    fn set_source(&mut self, source: u32) {
        self.source = source;
    }
}

fn main() {
    let mut packet = Packet {
        source: 10,
        destination: 20,
    };

    packet.set_source(42);
    print(packet.route_key());
}
```

## Semantic rules added in this milestone

Struct names live in the type namespace. Struct fields are resolved from nominal type metadata rather than dynamically. A literal must initialize every declared field exactly once and cannot introduce unknown fields.

Mutability propagates through field l-values:

```nus
let packet = Packet { source: 1, destination: 2 };
packet.source = 10; // error: immutable base value
```

```nus
let mut packet = Packet { source: 1, destination: 2 };
packet.source = 10; // valid
```

Methods explicitly declare how they receive `self`:

```nus
fn inspect(&self) { }
fn update(&mut self) { }
fn consume(self) { }
```

Milestone 5 validates receiver mutability, but full move/borrow lifetime semantics are intentionally deferred to the ownership milestone.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use

Run parsing + semantic analysis + AST output:

```bash
./build/nusc example.nus
```

Check only:

```bash
./build/nusc example.nus --check
```

Inspect tokens:

```bash
./build/nusc example.nus --tokens
```

## Architecture

```text
.nus source
    ↓
SourceManager
    ↓
Lexer
    ↓
Parser
    ↓
AST
    ↓
SemanticAnalyzer
    ├── lexical SymbolTable
    ├── primitive Type model
    ├── nominal Struct registry
    ├── field metadata
    ├── method metadata
    └── diagnostics
```

## Current boundary

This milestone does **not** claim full ownership or borrowing yet. `self`, `&self` and `&mut self` establish the semantic surface required for that work, while move tracking, borrow conflicts and lifetime analysis remain future work.

Associated functions without a `self` receiver are also deferred; methods inside `impl` currently require a receiver.

## Next milestone

Milestone 6 should introduce ownership and borrowing foundations:

1. value move tracking
2. use-after-move diagnostics
3. immutable and mutable borrow state
4. borrow-conflict diagnostics
5. explicit `&mut` expressions/type syntax
6. reference-aware function parameters
7. receiver consumption for `self`
8. groundwork for deterministic destruction / `Drop`
