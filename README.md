# NUS Language — compiler bootstrap

Temporary bootstrap name for the network-oriented systems language.

## Current milestone

- C++20 project
- `SourceManager` / `SourceSpan`
- token model
- lexer
- nested block comments
- keywords and reserved words
- integer, float, string, char, byte and raw-string literals
- operators from Grammar v0.1
- CTest lexer tests
- `uxc` token-dump driver

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/uxc example.nus
```

On Windows with a multi-config generator, the executable may be under `build/Debug/`.

## Next milestone

AST + recursive-descent declarations/statements + Pratt expression parser.
