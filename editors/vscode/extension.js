'use strict';

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

let output;
let diagnostics;

function activate(context) {
    output = vscode.window.createOutputChannel('NUS');
    diagnostics = vscode.languages.createDiagnosticCollection('nus');

    context.subscriptions.push(
        output,
        diagnostics,
        vscode.commands.registerCommand('nus.checkCurrentFile', () => runForActiveEditor('--check')),
        vscode.commands.registerCommand('nus.buildCurrentFile', () => runForActiveEditor('--build')),
        vscode.commands.registerCommand('nus.showCompilerOutput', () => output.show(true)),
        vscode.workspace.onDidCloseTextDocument((document) => diagnostics.delete(document.uri))
    );
}

function deactivate() {}

async function runForActiveEditor(mode) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'nus') {
        vscode.window.showWarningMessage('Open a .nus file before running the NUS compiler.');
        return;
    }

    const document = editor.document;
    if (document.isUntitled) {
        vscode.window.showWarningMessage('Save the NUS file before compiling it.');
        return;
    }

    if (document.isDirty) {
        const saved = await document.save();
        if (!saved) {
            return;
        }
    }

    await runCompiler(document, mode);
}

function workspaceFor(document) {
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    return folder ? folder.uri.fsPath : path.dirname(document.uri.fsPath);
}

function firstExisting(candidates) {
    for (const candidate of candidates) {
        if (candidate && fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return undefined;
}

function compilerFor(document) {
    const config = vscode.workspace.getConfiguration('nus', document.uri);
    const configured = config.get('compilerPath', '').trim();
    if (configured) {
        return configured;
    }

    if (process.env.NUSC && process.env.NUSC.trim()) {
        return process.env.NUSC.trim();
    }

    const root = workspaceFor(document);
    const local = firstExisting(process.platform === 'win32'
        ? [
            path.join(root, 'build', 'nusc.exe'),
            path.join(root, 'cmake-build-debug', 'nusc.exe')
        ]
        : [
            path.join(root, 'build', 'nusc'),
            path.join(root, 'cmake-build-debug', 'nusc')
        ]);

    return local || 'nusc';
}

function diagnosticSeverity(kind) {
    switch (kind) {
        case 'warning': return vscode.DiagnosticSeverity.Warning;
        case 'note': return vscode.DiagnosticSeverity.Information;
        default: return vscode.DiagnosticSeverity.Error;
    }
}

function parseDiagnostics(text, cwd) {
    const byUri = new Map();
    const regex = /^(.*):(\d+):(\d+):\s+(error|warning|note):\s+(.*)$/;

    for (const rawLine of text.split(/\r?\n/)) {
        const match = rawLine.match(regex);
        if (!match) {
            continue;
        }

        let filename = match[1];
        if (!path.isAbsolute(filename)) {
            filename = path.resolve(cwd, filename);
        }

        const line = Math.max(0, Number(match[2]) - 1);
        const column = Math.max(0, Number(match[3]) - 1);
        const range = new vscode.Range(line, column, line, column + 1);
        const diagnostic = new vscode.Diagnostic(
            range,
            match[5],
            diagnosticSeverity(match[4])
        );
        diagnostic.source = 'nusc';

        const uri = vscode.Uri.file(filename);
        const key = uri.toString();
        const existing = byUri.get(key) || { uri, entries: [] };
        existing.entries.push(diagnostic);
        byUri.set(key, existing);
    }

    return byUri;
}

async function runCompiler(document, mode) {
    const compiler = compilerFor(document);
    const cwd = workspaceFor(document);
    const args = [document.uri.fsPath, mode];
    const config = vscode.workspace.getConfiguration('nus', document.uri);
    const reveal = config.get('revealCompilerOutput', 'onError');

    diagnostics.clear();
    output.clear();
    output.appendLine(`> ${compiler} ${quoteForDisplay(document.uri.fsPath)} ${mode}`);
    output.appendLine(`cwd: ${cwd}`);
    output.appendLine('');

    const result = await execute(compiler, args, cwd);
    const combined = [result.stdout, result.stderr].filter(Boolean).join(result.stdout && result.stderr ? '\n' : '');
    if (combined) {
        output.append(combined.endsWith('\n') ? combined : `${combined}\n`);
    }

    for (const { uri, entries } of parseDiagnostics(combined, cwd).values()) {
        diagnostics.set(uri, entries);
    }

    if (result.spawnError) {
        output.appendLine(`NUS extension: ${result.spawnError.message}`);
        output.show(true);
        vscode.window.showErrorMessage(
            `Could not start nusc (${compiler}). Configure nus.compilerPath or NUSC.`
        );
        return;
    }

    const success = result.code === 0;
    if (reveal === 'always' || (reveal === 'onError' && !success)) {
        output.show(true);
    }

    const action = mode === '--build' ? 'build' : 'check';
    if (success) {
        vscode.window.setStatusBarMessage(`NUS ${action} succeeded`, 3000);
    } else {
        vscode.window.showErrorMessage(`NUS ${action} failed (exit code ${result.code}).`);
    }
}

function execute(command, args, cwd) {
    return new Promise((resolve) => {
        let stdout = '';
        let stderr = '';
        let settled = false;

        const child = spawn(command, args, {
            cwd,
            shell: false,
            windowsHide: true,
            env: process.env
        });

        child.stdout.on('data', (chunk) => { stdout += chunk.toString(); });
        child.stderr.on('data', (chunk) => { stderr += chunk.toString(); });

        child.on('error', (spawnError) => {
            if (!settled) {
                settled = true;
                resolve({ code: null, stdout, stderr, spawnError });
            }
        });

        child.on('close', (code) => {
            if (!settled) {
                settled = true;
                resolve({ code, stdout, stderr, spawnError: null });
            }
        });
    });
}

function quoteForDisplay(value) {
    return /\s/.test(value) ? `"${value}"` : value;
}

module.exports = { activate, deactivate };
