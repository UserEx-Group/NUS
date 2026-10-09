# NUS Intermediate Representations

## Pipeline

```text
AST -> Semantic Analysis -> HIR -> MIR/CFG -> Flow Analysis -> Drop Elaboration -> Backend
```

## HIR

HIR remains structured, but every expression carries its semantic type and every local binding receives a stable `LocalId` within its function.

Key invariants:

- semantic type checking completed before lowering;
- every local identifier resolves to one `LocalId`;
- shadowed names have different IDs;
- methods carry receiver mode metadata;
- structs and locals are nominally typed.

## MIR

MIR removes structured control flow in favor of basic blocks and explicit terminators:

- `goto`
- `branch`
- `return`
- `unreachable`

MIR instructions represent calls, borrows, construction, field/index access, assignments, iterator operations and drop operations.

### Method normalization

```nus
packet.update(2);
```

with an `&mut self` receiver becomes conceptually:

```text
%receiver = borrow [mut] %packet
call @Packet::update, %receiver, `2`
```

The call operand's MIR function type includes the normalized receiver parameter, so ownership dataflow can distinguish consuming, shared and mutable receivers.

### `for` normalization

`for` currently lowers through:

```text
iter.init
iter.has_next
iter.next
```

A later specialization pass can lower ranges/arrays/slices without changing the frontend.

## Flow analysis

Milestone 8 adds backward liveness and forward move-state dataflow. See `docs/FLOW.md`.

## Drop IR

Two destruction instructions currently exist:

```text
drop
```

for definitely initialized values, and:

```text
drop.if.init
```

for path-dependent initialization. A native backend will lower conditional drops using explicit drop flags.
