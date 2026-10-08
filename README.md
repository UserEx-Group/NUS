# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 3

The compiler bootstrap is written in C++20. The frontend currently includes:

- `SourceManager` / `SourceSpan`
- lexer and token model
- nested block comments
- numeric, string, char, byte and raw-string literals
- recursive-descent parser for declarations, statements and block constructs
- Pratt expression parser with precedence and associativity
- typed function declarations
- `let`, `let mut`, `return`, `break`, `continue`
- `if` / `else` / `else if`
- function and method calls
- member access such as `packet.header.source`
- indexing such as `buffer[index]`
- slicing represented as indexing by a range: `buffer[0..count]`
- array literals: `[1, 2, 3]`
- repeated arrays: `[0; 4096]`
- exclusive and inclusive ranges: `0..10`, `0..=10`
- dedicated assignment AST nodes for `=`, `+=`, `-=`, etc.
- syntactic validation of assignment targets
- `while`, `loop` and `for`
- tail expressions in blocks
- parser diagnostics with source spans
- AST dump driver
- lexer and parser tests through CTest

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Parse a NUS file

```bash
./build/nusc example.nus
```

On Windows with a multi-config generator:

```powershell
.\build\Debug\nusc.exe example.nus
```

## Inspect lexer tokens

```bash
./build/nusc example.nus --tokens
```

## Example

```nus
fn main() {
    let mut attempts = 0;
    let buffer = [0; 4096];

    while attempts < 3 {
        attempts += 1;
    }

    for index in 0..=10 {
        print(buffer[index]);
    }

    let slice = buffer[0..10];
    runtime.poll();
}
```

## AST design note

Assignments, ranges, member access and indexing are represented by dedicated AST nodes rather than generic binary expressions. This keeps semantic analysis explicit:

```text
buffer[0..count] = value

Assignment
├── Target: Index
│   ├── Object: buffer
│   └── Subscript: Range(0, count)
└── Value: value
```

## Next milestone

Milestone 4 begins semantic analysis:

1. symbol tables and lexical scopes
2. name resolution
3. built-in type registry
4. type checking for literals and operators
5. mutability checks
6. function call arity/type checks
7. loop-context checks for `break` and `continue`

This is the point where NUS starts rejecting programs that are syntactically valid but semantically incorrect.
