# NUS — Network Universal Syntax

NUS is an experimental network-oriented systems programming language focused on systems, networking, concurrency, simulation and safety.

> High-level by default. Low-level by choice. Safe by default.

## Milestone 2

The compiler bootstrap is written in C++20 and currently includes:

- `SourceManager` / `SourceSpan`
- lexer and complete token model for the current grammar
- nested block comments
- numeric, string, char, byte and raw-string literals
- AST nodes for functions, blocks, statements and core expressions
- recursive-descent parser for declarations and statements
- Pratt expression parser
- operator precedence and associativity
- function declarations and typed parameters
- optional function return types
- `let`, `let mut` and explicit local types
- `return`
- function calls
- `if`, `else` and `else if`
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
fn add(a: i32, b: i32) -> i32 {
    return a + b * 2;
}

fn main() {
    let result = add(10, 20);

    if result > 20 {
        print(result);
    }
}
```

## Next milestone

The next frontend milestone adds member access, indexing, assignment validation, `while` / `loop` / `for`, arrays and richer type syntax before semantic analysis begins.
