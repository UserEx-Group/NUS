# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 4

The compiler bootstrap is written in C++20. The frontend now includes lexical analysis, parsing and the first semantic-analysis layer.

### Frontend

- `SourceManager` / `SourceSpan`
- lexer and token model
- recursive-descent parser for declarations and control flow
- Pratt parser for expressions
- typed function declarations
- `let`, `let mut`, `return`, `break`, `continue`
- `if`, `while`, `loop`, `for`
- arrays, indexing, slicing and ranges
- function calls, member syntax and assignments
- AST debug printer

### Semantic analysis

- lexical symbol tables and nested scopes
- forward resolution of top-level functions
- built-in type registry
- primitive types: integers, floats, `bool`, `char`, `string`, `bytes`, `unit`
- inferred literal types (`i32`, `f64`, etc.)
- contextual numeric-literal compatibility such as `let port: u16 = 8080;`
- duplicate-name detection in the same scope
- undefined-name diagnostics
- immutable versus mutable local checking
- function call arity and argument-type checking
- return-type checking
- operator type checking
- boolean-condition checking
- homogeneous array inference
- array indexing and range slicing checks
- `for` iteration over ranges, arrays and slices
- `break` / `continue` loop-context validation
- expression type map retained by the semantic analyzer

Two small built-ins currently exist so semantic tests and example programs can run before the standard library is implemented:

```nus
print(value);
assert(condition);
```

`print` accepts any number of values and returns `unit`. `assert` accepts exactly one `bool`.

## Current semantic boundary

The parser already understands member syntax such as:

```nus
packet.header.source
socket.close()
```

but structs, fields, `impl` blocks and methods are not part of the AST yet. The semantic analyzer therefore reports member access as unsupported instead of pretending that a field exists. Those features belong to the next language-model milestone.

Likewise, ownership/borrowing, `async`/`await`, user-defined types and the network DSL are still future work.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Parse and semantically check a NUS file

```bash
./build/nusc example.nus
```

The default command runs lexer, parser and semantic analysis, then prints the AST when the program is valid.

On Windows with a multi-config generator:

```powershell
.\build\Debug\nusc.exe example.nus
```

## Check without printing the AST

```bash
./build/nusc example.nus --check
```

A successful check exits with status `0` and produces no output.

## Inspect lexer tokens

```bash
./build/nusc example.nus --tokens
```

## Example semantic errors

Immutable assignment:

```nus
fn main() {
    let port = 8080;
    port = 9000;
}
```

Undefined name:

```nus
fn main() {
    print(router);
}
```

Type mismatch:

```nus
fn main() {
    let connected: bool = 10;
}
```

## Architecture

```text
.nus source
    ↓
SourceManager
    ↓
Lexer
    ↓
Token stream
    ↓
Parser
    ↓
AST
    ↓
SemanticAnalyzer
    ├── SymbolTable / lexical scopes
    ├── name resolution
    ├── Type model
    └── diagnostics
```

## Next milestone

Milestone 5 should introduce the first real user-defined type model:

1. `struct` declarations in the parser/AST
2. struct literals
3. field resolution
4. `impl` blocks and methods
5. method-call type checking
6. richer type syntax (arrays, slices and references in annotations)
7. groundwork for ownership and borrowing

That will let NUS type-check code such as `packet.source`, `socket.close()` and protocol/header structures instead of treating member access as a syntax-only feature.
