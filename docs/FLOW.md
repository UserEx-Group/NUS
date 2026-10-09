# NUS MIR Flow Analysis

Milestone 8 introduces control-flow-sensitive ownership analysis on MIR.

## Liveness

For each basic block NUS computes:

- `live_in`
- `live_out`
- locals live before each MIR instruction
- locals live after each MIR instruction
- locals live immediately before the terminator

The analysis is a standard backwards fixed-point calculation over CFG successors.

## Loans and NLL

Every MIR `borrow` creates a loan:

```text
%3 = borrow %0
```

is represented conceptually as:

```text
loan L0 {
    root: %0
    reference: %3
    kind: shared
}
```

When a reference is assigned to another reference local, loan identity propagates to that local. A loan remains active while any local carrying it is live. This allows a borrow to end at its last real use instead of at the closing brace of the source block.

## Move states

Each MIR local participates in forward dataflow with possible states:

```text
Uninitialized
Available
Moved
```

At CFG joins, states are merged. A local that is `Available` on one predecessor and `Moved` on another becomes path-dependent and cannot be read until safely reinitialized.

## Borrow conflicts

The flow checker rejects:

- mutable borrow while any loan of the same owner is live;
- shared borrow while a mutable loan is live;
- moving an owner while any loan is live;
- writing an owner while any loan is live;
- directly reading an owner while a mutable loan is live.

## Drop elaboration

For each return block, owned non-`Copy` values are classified from move-state dataflow.

Definitely initialized:

```text
drop %x
```

Initialized on only some incoming paths:

```text
drop.if.init %x
```

Definitely moved/uninitialized values receive no drop.

`drop.if.init` is an intermediate operation. Native codegen will materialize the corresponding drop flag.

## Current limitations

Milestone 8 intentionally does not solve every lifetime problem. It does not yet provide:

- lifetime parameters in source syntax;
- reference-return inference;
- reference fields;
- partial-move tracking per struct field;
- interprocedural lifetime relationships;
- precise alias analysis for arbitrary pointer-derived places.

The current goal is a sound and inspectable CFG-local ownership foundation for native code generation.
