# NUS Generics & Static Traits — through Milestone 13

NUS uses monomorphization-first compile-time polymorphism. Generic source functions and generic impl methods are templates; only concrete specializations used by the program reach HIR, MIR and LLVM.

## Generic functions

```nus
fn identity<T>(value: T) -> T {
    value
}

fn main() {
    let number = identity(42);
    let flag = identity(true);
}
```

The calls produce distinct concrete specializations such as:

```text
identity$g$i32
identity$g$bool
```

The unresolved template itself is not emitted to LLVM.

## Explicit generic arguments

NUS uses turbofish syntax when inference is impossible or an explicit choice is desired:

```nus
let values: Vec<i32> = vec_new::<i32>();
let missing: Option<i32> = none::<i32>();
```

A complete explicit type-argument list is currently required whenever turbofish is present.

## Trait bounds

`Copy` is a built-in compiler trait:

```nus
fn duplicate<T: Copy>(value: T) -> T {
    value
}
```

User traits may also be used as generic capabilities:

```nus
trait NetworkValue {}

struct Packet {
    value: i32,
}

impl NetworkValue for Packet {}

fn accept<T: NetworkValue>(value: &T) -> i32 {
    1
}
```

Manual implementations of `Copy` remain rejected until the compiler can verify all Copy invariants recursively.

## Trait methods and associated types

Milestone 13 extends traits beyond marker-only declarations:

```nus
trait Readable {
    type Item;
    fn read(&self) -> Self::Item;
}
```

An impl binds the associated type and implements the required method:

```nus
impl Readable for Box {
    type Item = i32;
    fn read(&self) -> i32 { self.value }
}
```

`Self` and `Self::Item` are substituted while the compiler checks the impl contract. Dispatch is static; trait objects and vtables are not implemented.

## Generic impl blocks

Generic implementation patterns are supported:

```nus
impl<T> Vec<T> {
    fn count(&self) -> usize {
        self.len()
    }
}
```

Generic trait impls are supported as well:

```nus
trait Counted {
    type Item;
    fn count(&self) -> usize;
}

impl<T> Counted for Vec<T> {
    type Item = T;
    fn count(&self) -> usize { self.len() }
}
```

When the semantic analyzer sees a call on a concrete receiver, it matches that receiver against the generic impl target, infers impl type parameters, checks bounds, substitutes the concrete signature, and records an on-demand method instantiation. HIR emits only those concrete instances.

## Generic `Vec<T>` runtime API

Construction is still:

```nus
let mut values: Vec<i32> = vec_new::<i32>();
```

Instance syntax is now preferred for operations:

```nus
values.push(10);
values.push(20);
print(values.len());
print(values.get(1));
```

`push<T>` transfers ownership of non-Copy elements into the vector. Vector drop glue destroys non-Copy elements before freeing the allocation. `get` currently requires `T: Copy` because returning an element by value without removing it would otherwise duplicate ownership.

The older `vec_push`, `vec_len`, `vec_get` and type-specific bootstrap functions remain compatibility shims.

## `Option<T>` and `Result<T, E>`

These generic algebraic types lower to native tagged values:

```nus
let a: Option<i32> = some(42);
let b: Option<i32> = none::<i32>();

let c: Result<i32, string> = ok::<i32, string>(7);
let d: Result<i32, string> = err::<i32, string>("network error");
```

`match` is exhaustive and postfix `?` lowers to explicit CFG with ownership-aware early return. See `ENUMS.md`.

## Monomorphization boundary

```text
AST template
   ↓
semantic inference + bounds / impl matching
   ↓
function + method instantiation registry
   ↓
typed specialized HIR
   ↓
MIR
   ↓
LLVM
```

LLVM code generation treats any unresolved source type parameter as an internal compiler error.

## Current limitations

The generic/trait model does not yet implement:

- user-defined generic structs or enums;
- default trait method bodies;
- generic methods declared directly inside traits;
- `where` clauses;
- supertraits;
- associated constants;
- associated-type equality constraints such as `Iterator<Item = T>`;
- trait objects or dynamic dispatch;
- complete overlap/coherence checking for generic impls;
- generic calls that must remain abstract through unsupported forms of nested generic dispatch.

See `TRAITS_ITERATORS.md` for method/iterator semantics and ownership boundaries introduced in Milestone 13.
