// ESLint configuration: `npm run lint` (or `make lint`).
//
// Besides the examples and tools, this lints the JavaScript embedded in the
// C++ sources: every R"JS( ... )JS" raw string in src/ is linted as its own
// script, and problems are reported at their line in the .cpp file.
'use strict';

const js = require('@eslint/js');
const globals = require('globals');

const kEmbeddedStart = 'R"JS(';
const kEmbeddedEnd = ')JS"';

// Lines before each block of the file most recently preprocessed
const blockOffsets = new Map();

const embeddedJs = {
    meta: { name: 'frogjs-embedded-js' },
    preprocess(text, filename) {
        const blocks = [];
        const offsets = [];
        let from = 0;
        for (;;) {
            const start = text.indexOf(kEmbeddedStart, from);
            if (start < 0) break;
            const codeStart = start + kEmbeddedStart.length;
            const end = text.indexOf(kEmbeddedEnd, codeStart);
            if (end < 0) break;
            offsets.push(text.slice(0, codeStart).split('\n').length - 1);
            // The code starts on the line after R"JS( (or right after it)
            const prefixColumn = codeStart - (text.lastIndexOf('\n', codeStart - 1) + 1);
            blocks.push({ text: text.slice(codeStart, end), filename: `${blocks.length}.js`, prefixColumn });
            from = end + kEmbeddedEnd.length;
        }
        blockOffsets.set(filename, { offsets, blocks });
        return blocks.map(({ text: code, filename: name }) => ({ text: code, filename: name }));
    },
    postprocess(messagesPerBlock, filename) {
        const { offsets, blocks } = blockOffsets.get(filename) || { offsets: [], blocks: [] };
        blockOffsets.delete(filename);
        return messagesPerBlock.flatMap((messages, i) => messages.map((message) => {
            const shift = (line) => (line === undefined ? line : line + offsets[i]);
            const column = (line, col) => (line === 1 && col !== undefined ? col + blocks[i].prefixColumn : col);
            const { fix, suggestions, ...rest } = message;  // fixes would have the wrong offsets
            void fix;
            void suggestions;
            return {
                ...rest,
                column: column(message.line, message.column),
                endColumn: column(message.endLine, message.endColumn),
                line: shift(message.line),
                endLine: shift(message.endLine),
            };
        }));
    },
    supportsAutofix: false,
};

// What scripts see in FrogJS (see types/globals.d.ts)
const frogjsGlobals = {
    console: 'readonly',
    process: 'readonly',
    Buffer: 'readonly',
    setTimeout: 'readonly',
    setInterval: 'readonly',
    clearTimeout: 'readonly',
    clearInterval: 'readonly',
    require: 'readonly',
    module: 'writable',
    exports: 'writable',
    __filename: 'readonly',
    __dirname: 'readonly',
    fs: 'readonly',
    net: 'readonly',
    http: 'readonly',
    path: 'readonly',
    os: 'readonly',
    events: 'readonly',
    stream: 'readonly',
    util: 'readonly',
    crypto: 'readonly',
    child_process: 'readonly',
    worker_threads: 'readonly',
};

const rules = {
    ...js.configs.recommended.rules,
    'no-unused-vars': ['error', { args: 'none', caughtErrors: 'none' }],
    'no-var': 'error',
    'prefer-const': ['error', { destructuring: 'all' }],
    eqeqeq: ['error', 'always', { null: 'ignore' }],
    'no-throw-literal': 'error',
    'no-implicit-globals': 'error',
    'no-shadow-restricted-names': 'error',
    'no-unused-expressions': ['error', { allowShortCircuit: true, allowTernary: true }],
};

module.exports = [
    {
        ignores: ['build/**', 'node_modules/**', 'types/**/*.d.ts'],
    },
    {
        plugins: { frogjs: { processors: { 'embedded-js': embeddedJs } } },
    },
    // Embedded modules: each is a `(function (...) { ... })` expression
    {
        files: ['src/**/*.cpp'],
        processor: 'frogjs/embedded-js',
    },
    {
        files: ['src/**/*.cpp/*.js'],
        languageOptions: {
            ecmaVersion: 'latest',
            sourceType: 'script',
            globals: frogjsGlobals,
        },
        rules: {
            ...rules,
            // An embedded module is one expression statement
            'no-unused-expressions': 'off',
        },
    },
    // Scripts run by FrogJS
    {
        files: ['examples/**/*.js'],
        languageOptions: {
            ecmaVersion: 'latest',
            sourceType: 'commonjs',
            globals: frogjsGlobals,
        },
        rules,
    },
    // Development tools run by Node.js
    {
        files: ['tools/**/*.js', 'eslint.config.js'],
        languageOptions: {
            ecmaVersion: 'latest',
            sourceType: 'commonjs',
            globals: globals.node,
        },
        rules,
    },
];
