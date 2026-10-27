# VS Code Language Support — Milestone 16.5

The official bootstrap editor integration lives in `editors/vscode`.

## Architecture

The extension deliberately has two layers:

```text
VS Code
 ├─ TextMate grammar
 │   └─ lexical syntax highlighting
 ├─ language-configuration.json
 │   └─ comments / brackets / indentation / folding
 ├─ snippets
 └─ extension.js
     ├─ nusc --check
     ├─ nusc --build
     └─ compiler diagnostics → VS Code diagnostics
```

The extension does **not** contain a second parser or type checker. `nusc` remains the source of truth for syntax and semantics.

## Highlighting

The grammar covers the keyword set currently recognized by the lexer, including reserved future words such as `network`, `service`, `protocol`, `simulate`, `node` and `link`. It also recognizes primitive types, generic runtime types, native networking types, constructors, operators, comments, strings/bytes/raw strings and numeric forms.

The grammar recursively includes its block-comment rule, so nested `/* ... */` comments follow the lexer behavior. Semantic highlighting for symbols and types should eventually move to the language server.

## Compiler commands

`NUS: Check Current File` invokes:

```text
nusc <absolute-file> --check
```

`NUS: Build Current File` invokes:

```text
nusc <absolute-file> --build
```

Compiler discovery order:

1. `nus.compilerPath`.
2. `NUSC` environment variable.
3. `build/nusc.exe` / `build/nusc` in the workspace.
4. `cmake-build-debug/nusc.exe` / `nusc`.
5. `nusc` from `PATH`.

The process is started directly (`shell: false`). This avoids the quoting and `cmd.exe` issues that NUS already removed from its native toolchain.

## Diagnostics

The extension recognizes the compiler's source diagnostic format:

```text
path/to/file.nus:12:8: error: message
```

and creates VS Code diagnostics at the corresponding source location. Warnings and notes are mapped to their matching VS Code severities.

This is intentionally a bridge until the NUS language server exists. A future LSP should provide incremental diagnostics, completion, hover, go-to-definition, references, rename and semantic tokens.

## Development

No dependency installation is required:

```bash
cd editors/vscode
npm run check-js
npm run validate
```

Then open the directory in VS Code and press `F5`.
