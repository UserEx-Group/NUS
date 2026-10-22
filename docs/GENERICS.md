# NUS Generics & Traits — Milestone 11

Milestone 11 introduces the first compile-time polymorphism model for NUS. The implementation is deliberately monomorphization-first: generic source functions are templates and only concrete specializations used by the program reach HIR, MIR and LLVM.

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

The two calls create distinct specializations, conceptually:

```text
identity$g$i32
identity$g$bool
```

The template itself is not emitted to LLVM.

## Explicit generic arguments

NUS uses turbofish syntax when inference is impossible or an explicit choice is preferred:

```nus
let values: Vec<i32> = vec_new::<i32>();
let missing: Option<i32> = none::<i32>();
```

Milestone 11 requires a complete explicit argument list when turbofish is present.

## Trait bounds

`Copy` is a built-in compiler trait:

```nus
fn duplicate<T: Copy>(value: T) -> T {
    value
}
```

Marker traits may be declared and implemented:

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

Milestone 11 marker traits have no methods, associated types, default implementations, dynamic dispatch or trait objects. They exist to establish compile-time capabilities and generic bounds without introducing vtables.

Manual implementations of `Copy` are intentionally rejected until the compiler can verify all Copy invariants for user-defined types.

## Generic `Vec<T>` runtime API

The milestone 10 `vec_*_i32` bootstrap functions remain compatibility shims, but new code should use:

```nus
let mut values: Vec<i32> = vec_new::<i32>();
vec_push(&mut values, 10);
vec_push(&mut values, 20);

print(vec_len(&values));
print(vec_get(&values, 1));
```

`vec_new<T>`, `vec_push<T>` and `vec_len<T>` work with arbitrary concrete element types. `vec_get<T>` currently requires `T: Copy`, because returning a non-Copy element by value without removing it would duplicate ownership.

`vec_push` transfers ownership of non-Copy elements into the vector. Native drop glue walks a `Vec<T>` and destroys non-Copy elements before freeing the allocation.

Example:

```nus
let literal = "owned";
let value = string_clone(&literal);

let mut values: Vec<string> = vec_new::<string>();
vec_push(&mut values, value);
```

`value` is moved into the vector, and the string is eventually destroyed by the vector's recursive drop glue.

## `Option<T>` and `Result<T, E>` foundations

The type system now understands:

```nus
Option<i32>
Result<i32, string>
```

and their generic constructors at semantic-analysis level:

```nus
let a: Option<i32> = some(42);
let b: Option<i32> = none::<i32>();

let c: Result<i32, string> = ok::<i32, string>(7);
let d: Result<i32, string> = err::<i32, string>("network error");
```

Their native constructor lowering and pattern matching are intentionally deferred. Programs that use those constructors can be type-checked in Milestone 11, but should not yet be passed to `--build`.

## Monomorphization boundary

The semantic analyzer records concrete generic instantiations. HIR then lowers only those instances and substitutes generic type parameters with concrete types before MIR creation.

This keeps later compiler stages free of unresolved `T` values:

```text
AST template
   ↓
semantic inference + bounds
   ↓
instantiation registry
   ↓
typed specialized HIR
   ↓
MIR
   ↓
LLVM
```

LLVM code generation treats an unresolved type parameter as an internal compiler error.

## Current limitations

Milestone 11 does not yet implement:

- generic structs defined by users;
- generic methods;
- trait methods;
- associated types or constants;
- `where` clauses;
- specialization/overlapping impl resolution;
- trait objects or dynamic dispatch;
- generic calls that must remain abstract across another generic template;
- native `Option`/`Result` constructors and pattern matching.

These restrictions keep the first generic model deterministic and monomorphization-friendly.
