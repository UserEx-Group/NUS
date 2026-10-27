# NUS Enums and Pattern Matching — Milestone 12

Milestone 12 introduces nominal enums with positional payloads, exhaustive `match`, native `Option<T>` / `Result<T, E>`, and postfix `?` propagation.

## User enums

```nus
enum NetworkError {
    Timeout,
    Code(i32),
}

let a = NetworkError::Timeout;
let b = NetworkError::Code(503);
```

Variants receive declaration-order discriminants starting at zero. Enum identity is nominal: two enums with the same variants are still different types.

## Exhaustive matching

```nus
fn classify(error: NetworkError) -> i32 {
    return match error {
        NetworkError::Timeout => 1,
        NetworkError::Code(code) => code,
    };
}
```

Every variant must be covered unless an irrefutable `_` or whole-value binding is present. Match arms must produce compatible result types.

Payload bindings currently require `Copy` payloads. This restriction avoids implicitly moving a field out of an enum while the enum itself remains responsible for drop. Owned payloads can still be ignored safely:

```nus
enum Message {
    Ping,
    Text(string),
}

match message {
    Message::Ping => print("ping"),
    Message::Text(_) => print("text"),
}
```

Nested variant patterns and pattern guards are intentionally deferred.

## `Option<T>`

```nus
let value = some(42);
let missing = none::<i32>();

let number = match value {
    Some(v) => v,
    None => 0,
};
```

Native representation uses a boolean discriminant plus payload storage. `None = 0`, `Some = 1`.

## `Result<T, E>`

```nus
fn parse(okay: bool) -> Result<i32, string> {
    let message = "invalid";

    return if okay {
        ok::<i32, string>(42)
    } else {
        err::<i32, string>(string_clone(&message))
    };
}
```

Native representation uses `Ok = 0`, `Err = 1`. Drop glue destroys only the active payload.

## `?` propagation

```nus
fn increment(okay: bool) -> Result<i32, string> {
    let value = parse(okay)?;
    ok::<i32, string>(value + 1)
}
```

`?` is lowered before LLVM to explicit CFG. On success the payload is extracted; on failure the containing `Option`/`Result` is transferred into an early return. The source value is marked consumed so drop elaboration cannot double-destroy an owned error payload.

`Option<T>?` requires the enclosing function to return a compatible `Option`; `Result<T,E>?` requires a compatible `Result<_,E>`.

## Native lowering

MIR introduces operations equivalent to:

```text
make.variant
get.discriminant
get.variant.field
consume
```

`match` becomes discriminant tests and branches. User-enum LLVM layout currently uses an `i32` tag followed by flattened, non-overlapping payload fields. This is not yet a compact union layout, but it keeps drop and field access deterministic while the ABI remains pre-1.0.

## Current boundaries

Milestone 12 does not yet implement generic user enums, nested variant patterns, pattern guards, non-Copy payload extraction bindings, reference-return lifetime inference, or a compact overlapping enum payload ABI.
