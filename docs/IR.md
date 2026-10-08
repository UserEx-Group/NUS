# NUS Intermediate Representations

## Pipeline

```text
AST -> Semantic Analysis -> HIR -> MIR/CFG -> future backend
```

## HIR

HIR remains structured, but every expression carries its semantic type and every local binding receives a stable `LocalId` within its function.

HIR is responsible for removing source-level ambiguity, not control-flow structure. `if`, loops and blocks still exist as structured nodes.

Key invariants:

- semantic analysis completed successfully before lowering;
- every local identifier resolves to one `LocalId`;
- shadowed names have different `LocalId` values;
- every HIR expression has a type (or `<unknown>` only where the current semantic model has not yet materialized contextual coercion information);
- method members carry receiver mode metadata (`self`, `&self`, `&mut self`).

## MIR

MIR removes structured control flow in favor of explicit basic blocks and terminators.

Each basic block ends in exactly one of:

- `goto`
- `branch`
- `return`
- `unreachable`

Calls, borrows, field operations and constructors are explicit instructions. Compiler-created temporaries are ordinary MIR locals marked as temporary.

### Method normalization

```nus
packet.update(2);
```

with:

```nus
fn update(&mut self, value: i32)
```

becomes conceptually:

```text
%receiver = borrow [mut] %packet
call @Packet::update, %receiver, `2`
```

This means the backend does not need to understand source-level method syntax.

### `for` normalization

`for` currently lowers through an abstract iterator protocol:

```text
iter.init
iter.has_next
iter.next
```

A later lowering pass may specialize ranges, arrays and slices without changing the frontend.

## CFG verifier

The verifier is intentionally small in Milestone 7. It verifies structural CFG invariants before later passes run. Dataflow correctness belongs to the next milestone.

## Planned Milestone 8 passes

- predecessor/successor construction
- local liveness
- move-state dataflow
- non-lexical borrow ranges
- deterministic drop placement
- reference escape analysis
