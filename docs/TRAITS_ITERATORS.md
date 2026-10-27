# NUS Traits, Generic Impls & Owned Iterators — Milestone 13

Milestone 13 turns traits from marker-only capabilities into statically checked interfaces with methods and associated types, adds generic `impl<T>` blocks, and introduces an owned vector iterator that can be lowered safely without reference-return lifetimes.

## Trait methods

Traits may declare method signatures. Method bodies and default implementations are not part of this milestone.

```nus
trait Readable {
    type Item;
    fn read(&self) -> Self::Item;
}
```

An implementation must provide every declared associated type and every required method with a compatible receiver, parameter list and return type:

```nus
struct Box {
    value: i32,
}

impl Readable for Box {
    type Item = i32;

    fn read(&self) -> i32 {
        self.value
    }
}
```

Trait dispatch is static. No trait object or vtable reaches MIR/LLVM.

## Associated types

Associated types are referenced through `Self` in trait signatures:

```nus
trait Source {
    type Output;
    fn read(&self) -> Self::Output;
}
```

The implementation binds the associated type:

```nus
impl Source for Box {
    type Output = i32;
    fn read(&self) -> i32 { self.value }
}
```

The semantic analyzer substitutes `Self` and `Self::Output` while validating the implementation contract. Missing bindings, unknown bindings, missing methods, receiver mismatches and incompatible return types are diagnostics.

## Generic impl blocks

NUS can now specialize methods declared on a generic implementation pattern:

```nus
impl<T> Vec<T> {
    fn count(&self) -> usize {
        self.len()
    }
}
```

The implementation remains a template until a concrete call is observed. A call on `Vec<i32>` records a concrete generic-method instantiation; HIR then emits only the specialized function. Unresolved `T` values do not reach MIR or LLVM.

Generic trait implementations are also supported:

```nus
trait Counted {
    type Item;
    fn count(&self) -> usize;
}

impl<T> Counted for Vec<T> {
    type Item = T;

    fn count(&self) -> usize {
        self.len()
    }
}

fn require_counted<T: Counted>(value: T) -> i32 {
    1
}
```

Trait bounds are checked by matching the concrete type against registered generic impl patterns and validating any bounds on the impl's parameters.

## `Vec<T>` instance methods

The bootstrap vector runtime now exposes instance-method syntax:

```nus
let mut values: Vec<i32> = vec_new::<i32>();

values.push(10);
values.push(20);

print(values.len());
print(values.get(1));
```

Supported instance operations are:

- `push(value)` — requires a mutable receiver and transfers ownership of `value` into the vector;
- `len()` — shared access to the current length;
- `get(index)` — shared access followed by a by-value load, currently restricted to `T: Copy`;
- `into_iter()` — consumes the vector and returns an owned `VecIter<T>`.

`vec_new::<T>()` remains the constructor because static/associated method syntax such as `Vec::<T>::new()` is not implemented yet. The older global `vec_push`, `vec_len` and `vec_get` operations remain compatibility shims.

## Consuming iteration

A vector can now be consumed directly by `for`:

```nus
let mut values: Vec<i32> = vec_new::<i32>();
values.push(10);
values.push(20);
values.push(30);

let mut sum = 0;
for value in values {
    sum += value;
}
```

After the loop, `values` is moved and cannot be used again.

The explicit form is:

```nus
let iterator = values.into_iter();
for value in iterator {
    print(value);
}
```

The compiler lowers a consumed `Vec<T>` to an owned runtime iterator:

```text
VecIter<T> {
    ptr data,
    usize len,
    usize cap,
    usize index,
}
```

`IterHasNext` checks `index < len`. `IterNext` loads the element at `index` and then increments the stored index. If the iterator is dropped early, drop glue destroys only the not-yet-consumed elements before freeing the allocation.

## Why there is no `.iter()` yet

A borrowing iterator would need to return/reference data owned by a vector and prove that the vector remains alive and cannot be invalidated by reallocation for the entire iterator lifetime. NUS does not yet have reference-return lifetime inference or lifetime parameters in source syntax.

Rather than make an unsound borrowed iterator, Milestone 13 deliberately provides only consuming iteration. `.iter()` and `.iter_mut()` are deferred until the lifetime model can express them safely.

## Current non-Copy iteration boundary

`for` over `Vec<T>`/`VecIter<T>` currently requires `T: Copy`.

The iterator itself already owns the backing allocation and can correctly destroy unconsumed non-Copy elements on early exit. What is still missing is complete destruction/move accounting for each non-Copy loop binding after an element has been yielded. Rejecting this case prevents leaks or double-destruction until per-iteration ownership is modeled precisely.

Non-Copy elements may still be stored safely in `Vec<T>` and are recursively destroyed by vector drop glue when the vector itself owns them.

## Flow-analysis correction

Milestone 13 also fixes a loop fixed-point issue in MIR ownership dataflow. Unvisited predecessor states now begin at the lattice bottom instead of an artificial `Uninitialized` state. This prevents a future loop-back edge from contaminating the first merge and falsely reporting owned iterator temporaries as unavailable.

## Deferred features

The current trait/iterator model intentionally does not yet provide:

- default trait methods;
- generic methods declared directly inside a trait;
- `where` clauses;
- supertraits;
- associated constants;
- associated-type constraints such as `Iterator<Item = T>` in bounds;
- trait objects, `dyn`, vtables or dynamic dispatch;
- specialization and complete overlap/coherence checking for generic impls;
- borrowing `.iter()` / `.iter_mut()`;
- non-Copy values yielded by `for`;
- user-defined generic structs/enums.

These boundaries keep dispatch static, monomorphized and compatible with the current ownership model.
