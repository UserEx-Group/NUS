# NUS Language Support for VS Code

Editor support for **NUS — Network Universal Syntax**.

## Included in Milestone 16.5

- `.nus` language registration.
- TextMate syntax highlighting for declarations, keywords, built-in and user types, literals, comments, operators, functions and the native networking surface.
- Bracket matching, auto-closing pairs, indentation, folding markers and comment toggling.
- Snippets for functions, structs, enums, traits, impls, `match`, `Result`, vectors and TCP/UDP bootstrap APIs.
- `NUS: Check Current File` and `NUS: Build Current File` commands.
- Compiler diagnostics from `nusc` rendered as VS Code diagnostics/squiggles.

This is intentionally **not an LSP yet**. Type-aware completion, hover, go-to-definition and semantic tokens should come from a future NUS language server rather than being reimplemented inside the extension.

## Run during development

Open `editors/vscode` as a VS Code extension project and press `F5`. In the Extension Development Host, open a `.nus` file.

The extension itself has no npm dependencies and does not need a TypeScript build step.

## Compiler discovery

Check/Build resolves the compiler in this order:

1. `nus.compilerPath` in VS Code settings.
2. `NUSC` environment variable.
3. `<workspace>/build/nusc.exe` on Windows or `<workspace>/build/nusc` elsewhere.
4. `<workspace>/cmake-build-debug/nusc(.exe)`.
5. `nusc` from `PATH`.

Example Windows setting:

```json
{
  "nus.compilerPath": "C:\\Users\\you\\NUS\\build\\nusc.exe"
}
```

## Commands

Use the Command Palette:

```text
NUS: Check Current File
NUS: Build Current File
NUS: Show Compiler Output
```

`Check` runs the equivalent of:

```text
nusc file.nus --check
```

`Build` runs:

```text
nusc file.nus --build
```

No shell is used to invoke the compiler.

## Validate the extension sources

```bash
npm run check-js
npm run validate
```

## Packaging

If `@vscode/vsce` is installed:

```bash
vsce package
```

or:

```bash
npx @vscode/vsce package
```
