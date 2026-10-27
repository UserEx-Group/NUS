# NUS Traits, Generic Impls & Iterators — through Milestone 14

Milestone 13 turned traits from marker-only capabilities into statically checked interfaces with methods and associated types, added generic `impl<T>` blocks, and introduced owned vector iteration. Milestone 14 extends that iterator model with shared/mutable borrowed iterators, interprocedural lifetime elision, and non-`Copy` consuming yields.

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

## Borrowed iteration — Milestone 14

Borrowed vector iteration is now available:

```nus
for value in values.iter() {
    print(*value);
}

for value in values.iter_mut() {
    *value += 1;
}
```

`iter()` yields `&T` through `VecRefIter<T>` and keeps a shared loan of the vector alive. `iter_mut()` yields `&mut T` through `VecMutIter<T>` and keeps an exclusive loan alive. Both loans are tracked through MIR carriers, so vector operations that could invalidate the backing buffer are rejected until the iterator's last use.

Borrowed iterators do not own or free the vector allocation. Their native shape is `{ ptr data, usize len, usize index }`.

## Non-Copy consuming iteration

The M13 `T: Copy` restriction has been removed. Owned iteration can now move values such as `string` out of a vector one element at a time. The loop binding is conditionally dropped at the end of each iteration; moving the binding clears its drop flag. `break` and `continue` run the same cleanup, while the iterator destructor destroys only elements from its current index onward.

## Current boundaries

The iterator/trait layer still intentionally defers:

- named lifetime parameters and lifetime bounds;
- reference fields in user structs/enums;
- generic methods declared directly inside a trait;
- `where` clauses, supertraits and associated constants;
- trait objects / dynamic dispatch;
- user-defined generic structs/enums.

See `docs/LIFETIMES.md` for the lifetime-elision and loan-provenance rules introduced in Milestone 14.
