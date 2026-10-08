# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 6 — Ownership + Borrowing foundations

The C++20 compiler frontend now implements the first memory-safety ownership model on top of the nominal type system introduced in Milestone 5.

### Implemented

- move semantics for non-`Copy` values
- use-after-move diagnostics
- scalar and shared-reference `Copy` behavior
- `&T` and `&mut T` in function parameter types
- `&value` and `&mut value` borrow expressions
- shared-borrow tracking
- exclusive mutable-borrow tracking
- lexical borrow release at scope exit
- temporary borrow release after calls/statements
- mutation blocked while an owner is borrowed
- moves blocked while an owner is borrowed
- mutable borrow requires a mutable place
- `&mut T` is non-`Copy`, preventing accidental mutable-reference aliasing
- reference-aware field access and mutation
- `self` consumes method receivers
- `&self` creates/uses a shared borrow
- `&mut self` creates/uses an exclusive mutable borrow
- assignment can reinitialize a moved mutable binding
- non-`Copy` array repetition is rejected
- conservative safety restrictions for reference escape until lifetime inference exists

## Ownership model

Primitive scalar values are `Copy`:

```nus
let a = 10;
let b = a;
print(a, b); // valid
```

Owned structures, strings and buffers use move semantics:

```nus
struct Packet { value: i32 }

let packet = Packet { value: 1 };
let moved = packet;
print(packet.value); // error: use of moved value
```

Passing an owned non-`Copy` value to a by-value parameter also moves it:

```nus
fn send(packet: Packet) {}

send(packet);
print(packet.value); // error
```

## Borrowing

Shared borrow:

```nus
fn inspect(packet: &Packet) {
    print(packet.value);
}

inspect(&packet);
```

Multiple shared borrows may coexist:

```nus
let a = &packet;
let b = &packet;
```

Exclusive mutable borrow:

```nus
fn update(packet: &mut Packet) {
    packet.value = 42;
}

let mut packet = Packet { value: 1 };
update(&mut packet);
```

A mutable borrow cannot coexist with another borrow:

```nus
let shared = &packet;
let exclusive = &mut packet; // error: already borrowed
```

And the owner cannot be used directly while exclusively borrowed:

```nus
let mut packet = Packet { value: 1 };
let reference = &mut packet;
print(packet.value); // error
```

## Lexical borrow lifetimes

Milestone 6 deliberately uses lexical lifetimes. A stored borrow lives until the end of its lexical scope:

```nus
let mut packet = Packet { value: 1 };

if true {
    let view = &packet;
    print(view.value);
} // borrow ends here

packet.value = 2; // valid
```

Temporary borrows passed directly to a call end with the call:

```nus
inspect(&packet);
update(&mut packet);
```

This model is intentionally more conservative than a future non-lexical lifetime (NLL) analysis, but it is simple, deterministic and memory-safe for the supported language subset.

## Mutable references are not Copy

Shared references can be copied. Mutable references cannot:

```nus
let mut packet = Packet { value: 1 };
let first = &mut packet;
let second = first;
print(first.value); // error: first was moved
```

This prevents two independently usable `&mut` aliases from being created by ordinary assignment.

## Receiver ownership

Methods now participate in ownership semantics:

```nus
impl Packet {
    fn inspect(&self) {}
    fn update(&mut self) {}
    fn consume(self) {}
}
```

- `&self`: shared temporary borrow
- `&mut self`: exclusive temporary borrow
- `self`: consumes the receiver

After:

```nus
packet.consume();
```

using `packet` again is a use-after-move error.

## Safe restrictions in this milestone

Full lifetime inference is not implemented yet. To avoid accepting dangling references, Milestone 6 intentionally rejects:

- returning references from functions/methods
- reference fields inside structs
- borrowed values escaping a nested block expression

These restrictions will be relaxed only when the compiler can prove the required lifetime relationships.

The borrow analysis is also not path-sensitive yet. It prefers conservative rejection over accepting potentially unsafe code.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use

Run parsing + semantic/ownership analysis + AST output:

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
    ├── Type model
    ├── nominal Struct registry
    ├── name/type resolution
    ├── move state
    ├── shared borrow state
    ├── mutable borrow state
    └── diagnostics
```

## Next milestone

Milestone 7 should turn ownership from a local semantic feature into a stronger compiler model:

1. explicit ownership/borrow facts in an intermediate representation
2. control-flow-aware move analysis
3. non-lexical lifetime inference
4. deterministic destruction points
5. `Drop` groundwork
6. reference escape analysis
7. groundwork for HIR/MIR lowering

After that, the compiler will be in a much better position to add enums/`Option`/`Result` and eventually native code generation without baking AST-specific assumptions into every semantic pass.
