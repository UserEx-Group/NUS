# NUS MIR Flow Analysis — through Milestone 14

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

Loan identity propagates through reference assignments and, as of Milestone 14, through borrow-carrying call results and iterators. Returned `&T`, `VecRefIter<T>` and `VecMutIter<T>` therefore keep the originating loan alive even after the temporary reference passed to a call is dead. A loan remains active while any carrier is live, so NLL still ends it at the last real use.

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

Milestone 14 adds conservative interprocedural borrow provenance and reference-return lifetime elision, but the lifetime model is still intentionally smaller than a full named-region system. It does not yet provide:

- named lifetime parameters or lifetime bounds in source syntax;
- reference fields in user aggregates;
- arbitrary returned field/index projections;
- partial-move tracking per struct field;
- lifetime-bearing user-defined aggregate types;
- precise alias analysis for arbitrary raw-pointer-derived places.

The current goal remains a sound and inspectable ownership model while extending NLL across the specific interprocedural relationships the compiler can prove.

## Consuming iterators — Milestone 13

`for value in vector` consumes an owned `Vec<T>` and lowers it to an owned `VecIter<T>`. The source vector becomes moved immediately. An explicit `vector.into_iter()` follows the same ownership transfer.

Loop dataflow now initializes unvisited non-entry block states at the lattice bottom rather than manufacturing an `Uninitialized` predecessor. This matters for backedges: before the correction, a loop header could merge with a not-yet-computed backedge and incorrectly conclude that its owned iterator temporary was unavailable on one path.

`VecIter<T>` drop glue is progress-aware. Because the iterator stores its current index, an early exit destroys only the elements not yet yielded and then frees the backing allocation.

Milestone 14 removes the `T: Copy` restriction for consuming iteration. Each non-Copy loop binding receives conditional per-iteration cleanup, including `break`/`continue` paths, while `VecIter<T>` owns only the not-yet-yielded suffix. Borrowing `.iter()` and `.iter_mut()` are represented by three-word borrow-carrying iterators and participate in the same liveness-derived NLL analysis.

## Milestone 17 resource pins

Reactor registrations introduce an internal third lifetime relation in addition to shared and mutable loans: a **resource pin**. A pin is carried by the Reactor and rooted at the registered socket owner. It prevents moves, reassignment and drop of that owner while the relevant Reactor remains live, but does not conflict with normal shared or mutable borrows used for socket I/O.

Pins are activated only after the registration call's program point and participate in forward CFG dataflow. Moving a Reactor propagates its pins to the new Reactor owner. M17 requires the owners behind registration references to be locally traceable; interprocedural registration effects are deferred.
