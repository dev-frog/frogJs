#!/usr/bin/env node
// Compare the TypeScript declarations in types/ with the running FrogJS.
//
//   node tools/check-types.js [path/to/frogjs]     (default build/frogjs)
//
// Every value a module declares (functions, constants, classes) must exist in
// the module FrogJS returns, and every method a declared class has must be on
// its prototype; so must the declared globals. Exports FrogJS has but the
// declarations leave out are listed too, as a reminder. Exits with 1 if
// anything declared is missing.
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
const kRuntimeCheck = `
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

function checkValue(label, value, info) {
  if (value === undefined) return missing.push(label);
  if (info.kind === 'class') checkClass(label, value, info);
}

for (const [name, entry] of Object.entries(modules)) {
  const module = require(name);
  if (entry.self) checkValue("require('" + name + "')", module, entry.self);
  for (const [key, info] of Object.entries(entry.exports)) {
    checkValue("require('" + name + "')." + key, module[key], info);
  }
  for (const key of Object.getOwnPropertyNames(module)) {
    if (['length', 'name', 'prototype', 'arguments', 'caller', 'init'].includes(key) || key.startsWith('_')) continue;
    if (!(key in entry.exports)) undeclared.push("require('" + name + "')." + key);
  }
}
for (const [name, info] of Object.entries(globals)) checkValue(name, globalThis[name], info);

console.log(JSON.stringify({ missing, undeclared }));
`;

function main() {
    const declared = declarations();
    const moduleCount = Object.keys(declared.modules).length;
    let output;
    try {
        output = execFileSync(frogjs, ['-e', kRuntimeCheck, JSON.stringify(declared)], { encoding: 'utf8' });
    } catch (err) {
        console.error(`Could not run ${frogjs}: ${err.message}`);
        process.exit(1);
    }
    const { missing, undeclared } = JSON.parse(output.trim().split('\n').pop());

    if (undeclared.length > 0) {
        console.log(`Not in the declarations (${undeclared.length}):`);
        for (const name of undeclared) console.log(`  ${name}`);
    }
    if (missing.length > 0) {
        console.error(`Declared but missing at run time (${missing.length}):`);
        for (const name of missing) console.error(`  ${name}`);
        process.exit(1);
    }
    console.log(`types/ matches FrogJS: ${moduleCount} modules and ${Object.keys(declared.globals).length} globals checked`);
}

if (require.main === module) main();

module.exports = { declarations };
