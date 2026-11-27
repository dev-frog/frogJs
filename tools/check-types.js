#!/usr/bin/env node
// Compare the TypeScript declarations in types/ with the running FrogJS.
//
//   node tools/check-types.js [path/to/frogjs]     (default build/frogjs)
//
// Every value a module declares (functions, constants, classes) must exist in
// the module FrogJS returns, and every method a declared class has must be on
// its prototype; so must the declared globals. It is also an error the other
// way around: a built-in module, export or global FrogJS has that types/
// doesn't declare. Exits with 1 if anything is missing on either side.
'use strict';

const path = require('path');
const { execFileSync } = require('child_process');
const ts = require('typescript');

const root = path.join(__dirname, '..');
const frogjs = path.resolve(process.argv[2] || path.join(root, 'build', 'frogjs'));

// Names that exist only for the type system, or only in some contexts
const kTypeOnlyGlobals = new Set(['FrogJS', 'BufferEncoding']);

function declarations() {
    const program = ts.createProgram([path.join(root, 'types', 'index.d.ts')], {
        noEmit: true,
        lib: ['lib.es2022.d.ts'],
        types: [],
    });
    const diagnostics = ts.getPreEmitDiagnostics(program);
    if (diagnostics.length > 0) {
        const host = { getCanonicalFileName: (f) => f, getCurrentDirectory: () => root, getNewLine: () => '\n' };
        throw new Error('types/ does not compile:\n' + ts.formatDiagnostics(diagnostics, host));
    }
    const checker = program.getTypeChecker();

    // Methods of a class, including inherited ones
    function classMethods(symbol) {
        const type = checker.getDeclaredTypeOfSymbol(symbol);
        return type.getProperties()
            // Optional methods are hooks a subclass may add (_final, _flush)
            .filter((p) => (p.flags & ts.SymbolFlags.Method) && !(p.flags & ts.SymbolFlags.Optional))
            .map((p) => p.getName())
            .filter((name) => !name.startsWith('__@'));  // [Symbol.asyncIterator] and the like
    }

    function describe(symbol) {
        if (symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol);
        if (!(symbol.flags & ts.SymbolFlags.Value)) return null;
        return symbol.flags & ts.SymbolFlags.Class ? { kind: 'class', methods: classMethods(symbol) } : { kind: 'value' };
    }

    const modules = {};
    for (const moduleSymbol of checker.getAmbientModules()) {
        const name = moduleSymbol.getName().replace(/^"|"$/g, '');
        const entry = { exports: {} };
        const exportEquals = moduleSymbol.exports && moduleSymbol.exports.get('export=');
        if (exportEquals) {
            // `export = X`: the module is X itself (EventEmitter, the Stream class)
            const target = checker.getAliasedSymbol(exportEquals);
            entry.self = describe(target);
        }
        for (const symbol of checker.getExportsAndPropertiesOfModule(moduleSymbol)) {
            const info = describe(symbol);
            if (info && symbol.getName() !== 'prototype') entry.exports[symbol.getName()] = info;
        }
        modules[name] = entry;
    }

    const globals = {};
    const globalFile = program.getSourceFile(path.join(root, 'types', 'globals.d.ts'));
    for (const symbol of checker.getSymbolsInScope(globalFile, ts.SymbolFlags.Value)) {
        const declaration = symbol.declarations && symbol.declarations[0];
        if (!declaration || !declaration.getSourceFile().fileName.startsWith(path.join(root, 'types'))) continue;
        // Skip ambient module declarations ("fs") and namespaces of types
        if (kTypeOnlyGlobals.has(symbol.getName()) || symbol.getName().startsWith('"')) continue;
        const info = describe(symbol);
        if (info) globals[symbol.getName()] = info;
    }
    return { modules, globals };
}

// Runs inside FrogJS: report what is missing
// (In a function, so that its own names aren't globals)
const kRuntimeCheck = `(() => {
const { modules, globals } = JSON.parse(process.argv[process.argv.length - 1]);
const missing = [];
const undeclared = [];

// Classes whose methods are set on each instance rather than the prototype
const kSampleInstances = { Buffer: () => Buffer.alloc(1) };

function checkClass(label, value, info) {
  if (typeof value !== 'function') return missing.push(label + ' (a class)');
  const sample = kSampleInstances[label] ? kSampleInstances[label]() : value.prototype;
  for (const method of info.methods) {
    if (typeof sample[method] !== 'function') missing.push(label + '.prototype.' + method + '()');
  }
}

function checkValue(label, value, info, present = value !== undefined) {
  if (!present) return missing.push(label);
  if (info.kind === 'class') checkClass(label, value, info);
}

for (const [name, entry] of Object.entries(modules)) {
  const module = require(name);
  if (entry.self) checkValue("require('" + name + "')", module, entry.self);
  for (const [key, info] of Object.entries(entry.exports)) {
    // A property may be declared and be undefined for now (process.exitCode)
    checkValue("require('" + name + "')." + key, module[key], info, key in Object(module));
  }
  for (const key of Object.getOwnPropertyNames(module)) {
    if (['length', 'name', 'prototype', 'arguments', 'caller', 'init'].includes(key) || key.startsWith('_')) continue;
    if (!(key in entry.exports)) undeclared.push("require('" + name + "')." + key);
  }
}
for (const [name, info] of Object.entries(globals)) checkValue(name, globalThis[name], info);

// Every built-in module needs a declaration
for (const name of require('module').builtinModules) {
  if (!(name in modules)) undeclared.push("require('" + name + "') (the whole module)");
}

// So does every global that isn't part of the JavaScript language
const language = new Set(JSON.parse(process.argv[process.argv.length - 2]));
for (const name of Object.getOwnPropertyNames(globalThis)) {
  if (!language.has(name) && !(name in globals) && !(name in modules)) undeclared.push(name + ' (a global)');
}

console.log(JSON.stringify({ missing, undeclared }));
})();
`;

// The globals the JavaScript language has. FrogJS's V8 can be newer than the
// Node.js running this tool (CI: Homebrew's latest V8, Node.js 22), so Node.js's
// own globals aren't enough: TypeScript's ES library (without the DOM) declares
// new built-ins such as Float16Array and DisposableStack, usually before V8
// ships them. Both are used, so a global counts as FrogJS's only if neither has it.
function languageGlobals() {
    const names = new Set(Object.getOwnPropertyNames(require('vm').runInNewContext('globalThis')));
    // TypeScript loads its library only for a program with a file: an empty one
    const empty = path.join(root, 'build', 'language-globals.ts');
    const host = ts.createCompilerHost({});
    const getSourceFile = host.getSourceFile;
    host.getSourceFile = (name, ...rest) =>
        (name === empty ? ts.createSourceFile(name, '', ts.ScriptTarget.ESNext) : getSourceFile.call(host, name, ...rest));
    const program = ts.createProgram([empty], { noEmit: true, lib: ['lib.esnext.d.ts'], types: [] }, host);
    const checker = program.getTypeChecker();
    for (const symbol of checker.getSymbolsInScope(program.getSourceFile(empty), ts.SymbolFlags.Value)) {
        const declarations = symbol.declarations || [];
        if (declarations.length > 0 && declarations.every((d) => program.isSourceFileDefaultLibrary(d.getSourceFile()))) {
            names.add(symbol.getName());
        }
    }
    return [...names];
}

function main() {
    const declared = declarations();
    const moduleCount = Object.keys(declared.modules).length;
    // Each global FrogJS adds must be in types/; the JavaScript language's own
    // globals needn't be
    const language = languageGlobals();
    let output;
    try {
        output = execFileSync(frogjs, ['-e', kRuntimeCheck, JSON.stringify(language), JSON.stringify(declared)], { encoding: 'utf8' });
    } catch (err) {
        console.error(`Could not run ${frogjs}: ${err.message}`);
        process.exit(1);
    }
    const { missing, undeclared } = JSON.parse(output.trim().split('\n').pop());

    if (undeclared.length > 0) {
        console.error(`In FrogJS but not declared in types/ (${undeclared.length}):`);
        for (const name of undeclared) console.error(`  ${name}`);
    }
    if (missing.length > 0) {
        console.error(`Declared but missing at run time (${missing.length}):`);
        for (const name of missing) console.error(`  ${name}`);
    }
    if (undeclared.length > 0 || missing.length > 0) process.exit(1);
    console.log(`types/ matches FrogJS: ${moduleCount} modules and ${Object.keys(declared.globals).length} globals checked`);
}

if (require.main === module) main();

module.exports = { declarations };
