// ES module self-test: exits with code 1 if a check fails. Also runs under
// Node.js, which is how the expected values were checked. The modules are
// written to a temporary directory and run in child processes.
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const { pathToFileURL } = require('url');
const { check, section, run } = require('./lib/check');

const root = path.join(fs.realpathSync(os.tmpdir()), `frogjs-esm-${process.pid}`);

function write(file, text) {
  const full = path.join(root, file);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, typeof text === 'string' ? text : JSON.stringify(text));
  return full;
}

// Run a file (or code with -e), and return { out, err, status }
function exec(args, input) {
  const result = spawnSync(process.execPath, args, { cwd: root, encoding: 'utf8', input });
  return { out: result.stdout.trim(), err: result.stderr.trim(), status: result.status };
}
const runFile = (file) => exec([path.join(root, file)]);
// The JSON a script prints on its last line
const json = (file) => {
  const { out, err } = runFile(file);
  try {
    return JSON.parse(out.split('\n').pop());
  } catch {
    return { out, err };
  }
};
const url = (file) => pathToFileURL(path.join(root, file)).href;

run(async () => {
  section('Static imports');
  write('lib.mjs', `
    export const a = 1;
    export let counter = 0;
    export function increment() { counter++; }
    export default class Thing {}
    export { a as alias };`);
  write('reexport.mjs', `export * from './lib.mjs'; export { default as Thing } from './lib.mjs'; export * as ns from './lib.mjs';`);
  write('main.mjs', `
    import Thing, { a, alias, counter, increment } from './lib.mjs';
    import * as lib from './lib.mjs';
    import { ns, Thing as Again } from './reexport.mjs';
    const before = counter;
    increment();
    console.log(JSON.stringify({
      a, alias, before, after: counter, live: lib.counter, thing: typeof Thing, same: Again === Thing,
      nsKeys: Object.keys(ns), tag: lib[Symbol.toStringTag], strict: (function () { return this; })() === undefined,
      thisValue: typeof this, require: typeof require, module: typeof module, filename: typeof __filename,
    }));`);
  check('named, default, namespace and re-exports', json('main.mjs'), {
    a: 1, alias: 1, before: 0, after: 1, live: 1, thing: 'function', same: true,
    nsKeys: ['a', 'alias', 'counter', 'default', 'increment'], tag: 'Module', strict: true,
    thisValue: 'undefined', require: 'undefined', module: 'undefined', filename: 'undefined',
  });

  write('cycle-a.mjs', `import { b } from './cycle-b.mjs'; export const a = 'a'; export function getB() { return b; }`);
  write('cycle-b.mjs', `import { getB } from './cycle-a.mjs'; export const b = 'b'; export function check() { return getB(); }`);
  write('cycle.mjs', `import { check } from './cycle-b.mjs'; console.log(JSON.stringify(check()));`);
  check('cycles', json('cycle.mjs'), 'b');

  write('order/one.mjs', `globalThis.order = (globalThis.order || []).concat('one');`);
  write('order/two.mjs', `globalThis.order = (globalThis.order || []).concat('two');`);
  write('order/main.mjs', `import './one.mjs'; import './two.mjs'; import './one.mjs'; console.log(JSON.stringify(globalThis.order));`);
  check('evaluated once, in order', json('order/main.mjs'), ['one', 'two']);

  write('missing-export.mjs', `import { nope } from './lib.mjs';`);
  const missing = runFile('missing-export.mjs');
  check('missing export is a SyntaxError', [missing.status, /SyntaxError: The requested module '.\/lib.mjs' does not provide an export named 'nope'/.test(missing.err)], [1, true]);

  section('import.meta');
  write('meta/meta.mjs', `
    import { helper } from './helper.mjs';
    console.log(JSON.stringify({ keys: Object.keys(import.meta), url: import.meta.url, filename: import.meta.filename,
      dirname: import.meta.dirname, main: import.meta.main, helperMain: helper(), resolved: import.meta.resolve('./x.js'),
      builtin: import.meta.resolve('fs') }));`);
  write('meta/helper.mjs', 'export const helper = () => import.meta.main;');
  check('properties', json('meta/meta.mjs'), {
    keys: ['dirname', 'filename', 'main', 'resolve', 'url'], url: url('meta/meta.mjs'), filename: path.join(root, 'meta/meta.mjs'),
    dirname: path.join(root, 'meta'), main: true, helperMain: false, resolved: url('meta/x.js'), builtin: 'node:fs',
  });

  section('Package type and extensions');
  write('typed/package.json', { type: 'module' });
  write('typed/main.js', `import { cjs } from './other.cjs'; console.log(JSON.stringify({ cjs, type: typeof require }));`);
  write('typed/other.cjs', 'exports.cjs = typeof require;');
  check('"type": "module" and .cjs', json('typed/main.js'), { cjs: 'function', type: 'undefined' });
  write('commonjs/package.json', { type: 'commonjs' });
  write('commonjs/main.mjs', `import x from './x.js'; console.log(JSON.stringify(x));`);
  write('commonjs/x.js', 'module.exports = { from: "cjs" };');
  check('"type": "commonjs" with .mjs', json('commonjs/main.mjs'), { from: 'cjs' });
  // require() reads "type" only for .js: a file without an extension is
  // CommonJS (yargs's yargs/yargs), unless it only parses as a module
  write('typed/bin-cjs', 'module.exports = { kind: typeof require };');
  write('typed/bin-esm', 'export const kind = "esm";');
  write('typed/load.cjs', `const a = require('./bin-cjs'); const b = require('./bin-esm'); console.log(JSON.stringify([a.kind, b.kind]));`);
  check('require() of files without an extension in a "type": "module" package', json('typed/load.cjs'), ['function', 'esm']);

  section('Syntax detection');
  write('detect/main.js', `import { join } from 'path'; console.log(JSON.stringify(join('a', 'b')));`);
  const detected = runFile('detect/main.js');
  check('a .js file with import runs as a module', [detected.out, detected.err], ['"a/b"', '']);
  write('detect-pkg/package.json', { name: 'untyped' });
  write('detect-pkg/main.js', `export {}; console.log(JSON.stringify('ran'));`);
  const warned = runFile('detect-pkg/main.js');
  check('with a package.json, a warning', [warned.out, warned.err.includes('MODULE_TYPELESS_PACKAGE_JSON')], ['"ran"', true]);
  write('detect/tla.js', 'await Promise.resolve(); console.log(JSON.stringify("tla"));');
  check('top-level await means a module', json('detect/tla.js'), 'tla');
  write('detect/redeclare.js', `const require = 1; console.log(JSON.stringify(require));`);
  check('redeclaring require means a module', json('detect/redeclare.js'), 1);
  write('detect/plain.js', 'console.log(JSON.stringify(typeof require));');
  check('CommonJS stays CommonJS', json('detect/plain.js'), 'function');
  check('--eval', exec(['-e', 'import { sep } from "path"; console.log(sep, import.meta.url.endsWith("[eval1]"))']).out, '/ true');
  check('stdin', exec(['-'], 'import os from "os"; console.log(typeof os.cpus);').out, 'function');
  check('--check', [exec(['--check', write('check-ok.mjs', 'import x from "./nope.mjs"; export default x;')]).status,
                    exec(['--check', write('check-bad.mjs', 'export const = 1;')]).status], [0, 1]);

  section('CommonJS from ES modules');
  write('cjs/exports.js', 'exports.a = 1; exports["b"] = 2; module.exports.c = 3; var four = 4; Object.defineProperty(exports, "d", { enumerable: true, get: function () { return four; } }); Object.defineProperty(exports, "notFound", { enumerable: true, get() { return 5; } });');
  write('cjs/literal.js', 'const x = 5, y = 6; module.exports = { x, y, z: x, "quoted": y, method() {} };');
  write('cjs/reexport.js', 'module.exports = require("./exports.js");');
  write('cjs/esbuild.js', 'var exp = {}; exp.e = 8; module.exports = exp; 0 && (module.exports = { e });');
  write('cjs/babel.js', 'Object.defineProperty(exports, "__esModule", { value: true }); exports.default = "dflt"; exports.named = "n";');
  write('cjs/function.js', 'module.exports = function fn() { return "called"; }; module.exports.prop = "p";');
  write('cjs/main.mjs', `
    import exp, { a, b, c, d } from './exports.js';
    import { x, y, z, quoted } from './literal.js';
    import * as literal from './literal.js';
    import { a as ra, d as rd } from './reexport.js';
    import { e } from './esbuild.js';
    import babel, { named } from './babel.js';
    import fn, { prop } from './function.js';
    import * as ns from './exports.js';
    console.log(JSON.stringify({ exp, a, b, c, d, x, y, z, quoted, method: 'method' in literal, ra, rd, e,
      babel: babel.default, named, fn: fn(), prop, nsKeys: Object.keys(ns).sort() }));`);
  check('default and named exports', json('cjs/main.mjs'), {
    exp: { a: 1, b: 2, c: 3, d: 4, notFound: 5 }, a: 1, b: 2, c: 3, d: 4, x: 5, y: 6, z: 5, quoted: 6, method: true, ra: 1, rd: 4, e: 8,
    babel: 'dflt', named: 'n', fn: 'called', prop: 'p', nsKeys: ['a', 'b', 'c', 'd', 'default', 'module.exports'],
  });
  write('cjs/shared.js', 'module.exports = { count: (globalThis.loads = (globalThis.loads || 0) + 1) };');
  write('cjs/cache.mjs', `
    import shared from './shared.js';
    import { createRequire } from 'module';
    const require = createRequire(import.meta.url);
    console.log(JSON.stringify({ same: require('./shared.js') === shared, loads: globalThis.loads,
      cached: Object.keys(require.cache).some((key) => key.endsWith('shared.js')) }));`);
  write('cjs/not-found.mjs', `import { notFound } from './exports.js';`);
  const notFound = runFile('cjs/not-found.mjs');
  check('a name that wasn\'t found', [notFound.status, notFound.err.includes("SyntaxError: Named export 'notFound' not found. The requested module './exports.js' is a CommonJS module")],
    [1, true]);
  check('one instance, in require.cache', json('cjs/cache.mjs'), { same: true, loads: 1, cached: true });

  section('Built-ins');
  write('builtins.mjs', `
    import fs, { readFileSync } from 'fs';
    import * as pathNs from 'node:path';
    import EventEmitter, { once } from 'events';
    import { URL as U } from 'url';
    import process from 'process';
    console.log(JSON.stringify({ same: fs.readFileSync === readFileSync, join: pathNs.join('a', 'b'), defaultPath: typeof pathNs.default.join,
      emitter: typeof EventEmitter, once: typeof once, url: U === URL, process: process === globalThis.process }));`);
  check('default and named', json('builtins.mjs'), { same: true, join: 'a/b', defaultPath: 'function', emitter: 'function', once: 'function', url: true, process: true });
  write('bad-builtin.mjs', `import 'node:nope';`);
  check('unknown node: name', /ERR_UNKNOWN_BUILTIN_MODULE/.test(runFile('bad-builtin.mjs').err), true);

  section('JSON');
  write('data.json', '{"answer": 42}');
  write('json.mjs', `import data from './data.json' with { type: 'json' }; console.log(JSON.stringify(data));`);
  check('with type: json', json('json.mjs'), { answer: 42 });
  write('json-missing.mjs', `import data from './data.json';`);
  check('without the attribute', /ERR_IMPORT_ATTRIBUTE_MISSING/.test(runFile('json-missing.mjs').err), true);

  section('Dynamic import()');
  write('dynamic/main.cjs', `
    (async () => {
      const lib = await import('../lib.mjs');
      const fsNs = await import('fs');
      const cjs = await import('./value.cjs');
      const again = await import('../lib.mjs');
      const data = await import('../data.json', { with: { type: 'json' } });
      const errors = [];
      for (const [specifier, options] of [['./nope.mjs'], ['nope-package'], ['../data.json'], ['../lib.mjs', { with: { type: 'css' } }], ['http://x/y.js']]) {
        try { await import(specifier, options); errors.push('loaded'); } catch (err) { errors.push(err.code); }
      }
      console.log(JSON.stringify({ a: lib.a, fs: typeof fsNs.readFileSync, cjs: cjs.default, same: lib === again, data: data.default, errors }));
    })();`);
  write('dynamic/value.cjs', 'module.exports = "value";');
  check('from CommonJS', json('dynamic/main.cjs'), {
    a: 1, fs: 'function', cjs: 'value', same: true, data: { answer: 42 },
    errors: ['ERR_MODULE_NOT_FOUND', 'ERR_MODULE_NOT_FOUND', 'ERR_IMPORT_ATTRIBUTE_MISSING', 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED', 'ERR_UNSUPPORTED_ESM_URL_SCHEME'],
  });
  write('dynamic/main.mjs', `const { a } = await import('../lib.mjs'); const data = await import('data:text/javascript,export default 7'); console.log(JSON.stringify([a, data.default]));`);
  check('from a module, and data: URLs', json('dynamic/main.mjs'), [1, 7]);
  check('from --eval', exec(['-e', 'import("path").then((p) => console.log(p.sep))']).out, '/');
  write('dynamic/syntax.mjs', 'export const = ;');
  write('dynamic/retry.mjs', `
    const results = [];
    for (let i = 0; i < 2; i++) {
      try { await import('./syntax.mjs'); results.push('ok'); } catch (err) { results.push(err.name); }
    }
    console.log(JSON.stringify(results));`);
  check('a failed import can be tried again', json('dynamic/retry.mjs'), ['SyntaxError', 'SyntaxError']);

  section('require() of ES modules');
  write('req/esm.mjs', 'export const named = "n"; export default "d";');
  write('req/plain.mjs', 'export const only = 1;');
  write('req/tla.mjs', 'await 1; export const x = 1;');
  write('req/throws.mjs', 'throw new Error("from esm");');
  write('req/main.cjs', `
    const esm = require('./esm.mjs');
    const plain = require('./plain.mjs');
    const out = { named: esm.named, default: esm.default, esModule: esm.__esModule, plainKeys: Object.keys(plain),
      same: require('./esm.mjs') === esm };
    for (const file of ['./tla.mjs', './throws.mjs']) {
      try { require(file); out[file] = 'loaded'; } catch (err) { out[file] = err.code || err.message; }
    }
    console.log(JSON.stringify(out));`);
  check('namespace, __esModule, top-level await', json('req/main.cjs'), {
    named: 'n', default: 'd', esModule: true, plainKeys: ['only'], same: true, './tla.mjs': 'ERR_REQUIRE_ASYNC_MODULE', './throws.mjs': 'from esm',
  });

  section('package.json exports and imports');
  write('pkgs/node_modules/dual/package.json', {
    name: 'dual',
    exports: {
      '.': { import: './esm.mjs', require: './cjs.cjs' },
      './feature/*.js': './src/features/*.js',
      './feature/internal/*': null,
      './fallback': ['invalid-target', './present.js'],
      './package.json': './package.json',
    },
    imports: { '#dep': { node: 'dep-pkg', default: './browser.js' }, '#local/*': './src/*.js' },
  });
  write('pkgs/node_modules/dual/esm.mjs', `import self from 'dual/feature/a.js'; import local from '#local/local'; export default 'esm:' + self + ':' + local;`);
  write('pkgs/node_modules/dual/cjs.cjs', 'module.exports = "cjs:" + require("#dep");');
  write('pkgs/node_modules/dual/present.js', 'module.exports = "present";');
  write('pkgs/node_modules/dual/src/features/a.js', 'module.exports = "feature a";');
  write('pkgs/node_modules/dual/src/features/internal/x.js', 'module.exports = "internal";');
  write('pkgs/node_modules/dual/src/local.js', 'module.exports = "local";');
  write('pkgs/node_modules/dep-pkg/index.js', 'module.exports = "dep";');
  write('pkgs/node_modules/legacy/package.json', { main: 'lib/main' });
  write('pkgs/node_modules/legacy/lib/main.js', 'exports.legacy = true;');
  write('pkgs/main.mjs', `
    import esm from 'dual';
    import { createRequire } from 'module';
    import { legacy } from 'legacy';
    import fallback from 'dual/fallback';
    const require = createRequire(import.meta.url);
    const errors = {};
    for (const specifier of ['dual/feature/internal/x.js', 'dual/hidden.js', 'legacy/lib/main', 'nope']) {
      try { await import(specifier); errors[specifier] = 'loaded'; } catch (err) { errors[specifier] = err.code; }
    }
    console.log(JSON.stringify({ esm, cjs: require('dual'), legacy, fallback, pkg: require('dual/package.json').name, errors }));`);
  check('conditions, patterns, null, arrays, imports, self-reference', json('pkgs/main.mjs'), {
    esm: 'esm:feature a:local', cjs: 'cjs:dep', legacy: true, fallback: 'present', pkg: 'dual',
    errors: { 'dual/feature/internal/x.js': 'ERR_PACKAGE_PATH_NOT_EXPORTED', 'dual/hidden.js': 'ERR_PACKAGE_PATH_NOT_EXPORTED',
              'legacy/lib/main': 'ERR_MODULE_NOT_FOUND', nope: 'ERR_MODULE_NOT_FOUND' },
  });

  section('Errors and top-level await');
  write('errors/throw.mjs', 'const x = 1;\nthrow new Error("top level");\n');
  const thrown = runFile('errors/throw.mjs');
  check('uncaught error', [thrown.status, thrown.err.startsWith(url('errors/throw.mjs') + ':2\nthrow new Error("top level");\n      ^'),
                           thrown.err.includes('Error: top level')], [1, true, true]);
  write('errors/caught.mjs', 'process.on("uncaughtException", (err) => console.log(JSON.stringify(err.message)));\nthrow new Error("handled");');
  check('uncaughtException listeners', [runFile('errors/caught.mjs').status, json('errors/caught.mjs')], [0, 'handled']);
  write('errors/rejected.mjs', 'await Promise.reject(new Error("after await"));');
  const rejected = runFile('errors/rejected.mjs');
  check('rejected top-level await', [rejected.status, rejected.err.includes('Error: after await')], [1, true]);
  write('errors/tla.mjs', 'const value = await new Promise((resolve) => setTimeout(() => resolve(5), 10));\nconsole.log(JSON.stringify(value));');
  check('top-level await', json('errors/tla.mjs'), 5);
  write('errors/stalled.mjs', 'console.log("before");\nawait new Promise(() => {});\n');
  const stalled = runFile('errors/stalled.mjs');
  check('unsettled top-level await', [stalled.status, stalled.out, stalled.err.split('\n')[0]],
    [13, 'before', `Warning: Detected unsettled top-level await at ${url('errors/stalled.mjs')}:2`]);
  write('errors/link.mjs', `import './nope.mjs';`);
  const link = runFile('errors/link.mjs');
  check('a missing import', [link.status, link.err.includes('ERR_MODULE_NOT_FOUND'), link.err.includes(path.join(root, 'errors/nope.mjs'))], [1, true, true]);
  write('errors/dir.mjs', `import './';`);
  check('directory import', /ERR_UNSUPPORTED_DIR_IMPORT/.test(runFile('errors/dir.mjs').err), true);
  write('errors/ext.mjs', `import './data.txt';`);
  write('errors/data.txt', 'text');
  check('unknown extension', /ERR_UNKNOWN_FILE_EXTENSION/.test(runFile('errors/ext.mjs').err), true);
  write('errors/extensionless.mjs', `import '../lib';`);
  check('no extension guessing', /ERR_MODULE_NOT_FOUND/.test(runFile('errors/extensionless.mjs').err), true);

  section('Workers');
  write('worker/main.mjs', `
    import { Worker } from 'worker_threads';
    const worker = new Worker(new URL('./worker.mjs', import.meta.url));
    worker.on('message', (message) => { console.log(JSON.stringify(message)); worker.terminate(); });`);
  write('worker/worker.mjs', `import { parentPort } from 'worker_threads'; import { a } from '../lib.mjs'; parentPort.postMessage({ a, meta: import.meta.url.endsWith('worker.mjs') });`);
  check('an ES module worker', json('worker/main.mjs'), { a: 1, meta: true });

  fs.rmSync(root, { recursive: true });
});
