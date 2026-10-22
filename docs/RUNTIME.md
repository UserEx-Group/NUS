# NUS Runtime Foundations — Milestone 10

## Goals

Milestone 10 turns drop elaboration into observable native behavior and introduces the first heap-owning standard runtime values.

## Bootstrap allocator

The emitted LLVM currently imports the host C ABI:

```text
malloc
realloc
free
memcpy
```

This is a bootstrap implementation. A future allocator abstraction can replace these calls without changing source-language ownership semantics.

## Drop flags

Every non-`Copy`, non-reference MIR local receives a native `i1` initialization/drop flag.

- initialization sets the flag;
- a move clears the source flag;
- reassignment sets the destination flag;
- `drop` destroys the value and clears the flag;
- `drop.if.init` tests the flag before destruction.

This makes path-dependent ownership safe in generated code instead of treating MIR drops as comments.

## Strings

String literals point to static global bytes and carry `cap = 0`.

`string_clone(&value)` allocates `len + 1` bytes, copies the UTF-8 payload, writes a NUL sentinel for C interoperability, and returns an owned `string` with nonzero capacity.

`string_len(&value)` returns the stored byte length.

Dropping an owned string frees its data. Dropping a literal-backed string performs no free.

## Vec<T>

The type system and ABI now model `Vec<T>`. Generic methods do not exist yet, so the executable bootstrap API currently includes:

```text
vec_new_i32
vec_push_i32
vec_len_i32
vec_get_i32
```

`vec_push_i32` currently reallocates to exactly `len + 1`; this is deliberately simple rather than performant. Geometric capacity growth belongs in the later standard-library implementation.

Dropping a vector frees its allocation. `free(null)` handles empty vectors safely.

## Recursive drop

The native backend recursively destroys heap-owning fields inside structs and fixed arrays. This is the first form of NUS drop glue.

User-defined destructors are not available yet.
