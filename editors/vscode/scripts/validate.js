'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const repo = path.resolve(root, '..', '..');

function json(relative) {
    return JSON.parse(fs.readFileSync(path.join(root, relative), 'utf8'));
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

const manifest = json('package.json');
const grammar = json('syntaxes/nus.tmLanguage.json');
const language = json('language-configuration.json');
const snippets = json('snippets/nus.json');

assert(manifest.contributes.languages[0].extensions.includes('.nus'), 'package.json must associate .nus');
assert(manifest.contributes.grammars[0].scopeName === 'source.nus', 'grammar scope must be source.nus');
assert(grammar.scopeName === 'source.nus', 'TextMate grammar scope mismatch');
assert(language.comments.lineComment === '//', 'line comment must be //');
assert(language.comments.blockComment[0] === '/*' && language.comments.blockComment[1] === '*/', 'block comments missing');
assert(Object.keys(snippets).length >= 8, 'expected the core NUS snippets');

const lexerPath = path.join(repo, 'src', 'lexer', 'Lexer.cpp');
if (fs.existsSync(lexerPath)) {
    const lexer = fs.readFileSync(lexerPath, 'utf8');
    const keywords = [...lexer.matchAll(/Keyword\{"([A-Za-z_][A-Za-z0-9_]*)"/g)].map((m) => m[1]);
    const grammarText = JSON.stringify(grammar);
    const missing = keywords.filter((keyword) => !grammarText.includes(keyword));
    assert(missing.length === 0, `keywords missing from syntax grammar: ${missing.join(', ')}`);
}

const commandIds = new Set(manifest.contributes.commands.map((command) => command.command));
for (const id of ['nus.checkCurrentFile', 'nus.buildCurrentFile', 'nus.showCompilerOutput']) {
    assert(commandIds.has(id), `missing command ${id}`);
}

console.log(`NUS VS Code extension validation passed (${manifest.version}).`);
