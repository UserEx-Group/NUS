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

## Milestone 11 generic runtime layer

The `vec_*_i32` entry points remain compatibility shims, while the semantic/runtime interface now exposes generic `vec_new<T>`, `vec_push<T>`, `vec_len<T>` and `vec_get<T: Copy>`. Concrete element types are propagated through monomorphized HIR/MIR into LLVM. Non-Copy elements transferred into a vector are recursively destroyed by vector drop glue.

## Milestone 13 vector methods and iterator ownership

The generic vector runtime can now be reached through instance syntax: `push`, `len`, `get`, and consuming `into_iter`. These are resolved statically by semantic analysis and lowered to the existing concrete vector runtime operations.

`Vec<T>::into_iter` transfers the backing allocation into `VecIter<T> { data, len, cap, index }`; the source vector is marked moved. Iterator drop glue destroys only elements at positions `index..len` before freeing the allocation. Direct `for value in vector` performs the same ownership transfer implicitly.

## Milestone 14 borrowed iterators and non-Copy yields

`Vec<T>::iter()` and `iter_mut()` now create borrowed runtime iterators with `{ data, len, index }`. They carry shared or exclusive MIR loan provenance respectively but do not own or free the backing allocation. NLL releases that loan after the iterator/result's last use.

Owned iteration can now move non-`Copy` elements out one at a time. Each yielded loop binding has conditional cleanup, while `VecIter<T>` destroys only elements at its current index and beyond when dropped. This divides ownership cleanly between already-yielded bindings and the still-owned iterator suffix.

## Native socket resources

Milestone 15 adds two OS-backed owning resources: `TcpStream` and `UdpSocket`. Both use the compiler's normal move/drop flags. Moving a socket clears the source drop flag; destroying the live owner closes the native handle exactly once.

Networking errors are currently stable NUS error codes rather than raw platform error numbers. This keeps `Result<_, NetworkError>` portable between POSIX and Winsock while the error model is still bootstrap-level.

Network receive buffers are ordinary owned `Vec<u8>` allocations and therefore reuse the vector allocator/drop path rather than introducing a second buffer allocator.
