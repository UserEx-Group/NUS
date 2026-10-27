# NUS Lifetimes & Borrowed Iterators — Milestone 14

Milestone 14 introduces the first interprocedural lifetime relationships in NUS. The source language deliberately starts with lifetime **elision** instead of exposing named lifetime parameters such as `'a` before the compiler has a stable region model.

## Reference-return lifetime elision

A free function may return a reference when exactly one borrowed parameter can determine the output provenance:

```nus
fn identity(value: &i32) -> &i32 {
    value
}
```

The output borrow is tied to `value`. At the call site the MIR flow checker propagates the original loan through the call result:

```nus
let mut number = 41;
let reference = identity(&number);
print(*reference);

// The loan ended at the last use of `reference`.
number = 42;
```

A function with no borrowed input is rejected:

```nus
fn bad() -> &i32 {
    let local = 1;
    &local
}
```

Likewise, output lifetime elision is rejected when more than one borrowed input exists because the origin would be ambiguous:

```nus
fn choose(a: &i32, b: &i32) -> &i32 { a }
```

Named lifetime syntax is intentionally deferred rather than guessing which input the result borrows from.

Methods follow the receiver elision rule. `&self` / `&mut self` is the preferred output origin:

```nus
struct Packet { value: i32 }

impl Packet {
    fn as_ref(&self) -> &Packet {
        self
    }
}
```

Milestone 14 currently validates direct returned-reference provenance. More complex returned projections and aggregate reference fields remain deferred until named lifetimes/regions are represented explicitly.

## Loan provenance through MIR calls

Loans no longer propagate only through assignment of `&T`. A borrow identity may now be carried by:

- returned references;
- `VecRefIter<T>`;
- `VecMutIter<T>`;
- `IterInit` and `IterNext` operations derived from those values.

This lets NLL keep the owner borrowed for exactly as long as the result that carries the borrow is live.

## Shared vector iteration

`Vec<T>::iter()` returns a borrowed iterator:

```nus
let mut values: Vec<i32> = vec_new::<i32>();
values.push(10);
values.push(20);

for value in values.iter() {
    print(*value);
}

// The vector was borrowed, not consumed.
values.push(30);
```

Its runtime shape is:

```text
VecRefIter<T> {
    ptr data,
    usize len,
    usize index,
}
```

Each yielded item has type `&T`.

A mutation that could invalidate the backing allocation is rejected while the iterator remains live:

```nus
let iterator = values.iter();
values.push(30); // error: mutable borrow conflicts with live shared iterator
for value in iterator { print(*value); }
```

## Mutable vector iteration

`Vec<T>::iter_mut()` creates an exclusive borrow of the vector and yields `&mut T`:

```nus
for value in values.iter_mut() {
    *value += 1;
}
```

Dereference assignment lowers to MIR `store.deref`, so mutable borrowed iteration works for scalar elements as well as mutable field access on structs.

While a mutable iterator remains live, direct reads, other shared borrows and additional mutable borrows of the vector are rejected.

`VecMutIter<T>` uses the same three-word runtime representation as `VecRefIter<T>` but carries an exclusive loan in MIR.

## Non-Copy consuming iteration

Owned `VecIter<T>` can now yield non-`Copy` elements:

```nus
let literal = "owned";
let mut names: Vec<string> = vec_new::<string>();
names.push(string_clone(&literal));

for name in names {
    print(name);
}
```

`IterNext` advances the owned iterator before handing out the element. The loop binding receives a drop flag and is conditionally destroyed at the end of each iteration. If the binding is moved inside the loop, its flag is cleared and the cleanup becomes a no-op.

`break` and `continue` also emit per-iteration cleanup before transferring control. The iterator's own destructor starts from its current index, so already-yielded elements are never destroyed twice.

## Current boundary

Milestone 14 does not yet provide:

- named lifetime parameters (`'a`);
- lifetime bounds on generic parameters;
- reference fields in structs or enum payloads;
- arbitrary returned field/index projections;
- lifetime-bearing user-defined generic structs;
- higher-ranked lifetimes;
- raw-pointer provenance or unsafe alias escape hatches.

The implemented subset is intentionally conservative: references may escape a call only when the compiler can prove an unambiguous borrowed origin.
