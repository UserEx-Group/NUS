# NUS Native ABI — through Milestone 14

This document records the first explicit native ABI contract used by the bootstrap compiler. It is intentionally versioned and unstable before NUS 1.0.

## Targets

- Windows x86-64: `x86_64-w64-windows-gnu` (LLVM/Clang + MinGW-w64)
- Linux x86-64: `x86_64-unknown-linux-gnu`
- Linux AArch64: `aarch64-unknown-linux-gnu`
- macOS: host Apple target

Windows MSVC is not the default ABI and is not required by the GNU target.

## Scalar layout

| NUS type | Size | Alignment |
| --- | ---: | ---: |
| `bool`, `i8`, `u8` | 1 | 1 |
| `i16`, `u16` | 2 | 2 |
| `i32`, `u32`, `f32`, `char` | 4 | 4 |
| `i64`, `u64`, `f64` | 8 | 8 |
| `isize`, `usize`, references | pointer width | pointer alignment |

## Aggregate layout

Struct fields are laid out in declaration order. Every field starts at the next offset satisfying its alignment. The final struct size is rounded up to the largest field alignment.

Arrays use the aligned element stride multiplied by their fixed element count.

## Runtime values

`string`:

```text
{ ptr data, usize len, usize cap }
```

`cap == 0` currently identifies static/borrowed literal storage. `cap > 0` identifies heap-owned storage that must be released.

`Vec<T>`:

```text
{ ptr data, usize len, usize cap }
```

Slices remain:

```text
{ ptr data, usize len }
```

## Function lowering

Top-level functions use `nus_<name>` symbols. Methods are lowered to qualified function symbols; receivers become ordinary first parameters before LLVM lowering.

`main` is wrapped by a C ABI `main` entry point.

## Inspection

Use:

```text
nusc file.nus --layout
```

The implementation lives in `codegen/Layout` so type layout is no longer an implicit property of LLVM emission.


## Tagged values — Milestone 12

`Option<T>` and `Result<T, E>` now have native tagged representations. `Option` uses a boolean discriminant (`None = 0`, `Some = 1`); `Result` uses a boolean discriminant (`Ok = 0`, `Err = 1`). Only the active payload is destroyed.

User-defined enums use an `i32` discriminant followed by storage for the payload fields of their declared variants. The current bootstrap ABI lays payload fields out sequentially rather than overlapping them as a compact union. This is deliberately simple and sound but not space-optimal; a future ABI revision may use overlapping payload storage once layout optimizations are formalized.

## Owned vector iterator — Milestone 13

Consuming iteration over `Vec<T>` uses a four-word runtime value:

```text
VecIter<T> {
    ptr data,
    usize len,
    usize cap,
    usize index,
}
```

On a 64-bit target this is 32 bytes with 8-byte alignment. `Vec<T>::into_iter` transfers the vector allocation into this iterator and clears ownership from the source vector. The index denotes the first not-yet-yielded element.

Dropping an unfinished iterator destroys remaining non-Copy elements from `index` through `len` and then releases `data`. The source vector must not drop the transferred allocation.

## Borrowed vector iterators — Milestone 14

Borrowed iterators do not own the vector allocation. Both use three words:

```text
VecRefIter<T> / VecMutIter<T> {
    ptr data,
    usize len,
    usize index,
}
```

On a 64-bit target each is 24 bytes with 8-byte alignment. Their distinction is semantic: `VecRefIter<T>` carries a shared loan and yields `&T`; `VecMutIter<T>` carries an exclusive loan and yields `&mut T`. Dropping either iterator releases only the MIR loan; it never frees `data`.

## Milestone 15 networking ABI

Native networking uses target-stable NUS wrapper values rather than exposing platform socket structs directly:

```text
IpAddr       { u32 host_order_ipv4 }              size 4, align 4
SocketAddr   { IpAddr ip, u16 host_order_port }    size 8, align 4
NetworkError { i32 code }                          size 4, align 4
TcpStream    { i64 native_handle }                 size 8, align 8
UdpSocket    { i64 native_handle }                 size 8, align 8
```

The generated backend materializes a platform `sockaddr_in` only at the system-call boundary and applies `htonl` / `htons` there. POSIX file descriptors are extended into the 64-bit NUS handle field; Windows `SOCKET` values already fit the field on x86-64.

`TcpStream` and `UdpSocket` are non-`Copy` and participate in recursive drop glue. Their active owner closes the handle with `close` on POSIX or `closesocket` on Windows.
