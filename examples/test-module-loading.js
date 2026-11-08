// Module loader self-test: exits with code 1 if a check fails. Also runs under Node.js.
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync, spawnSync } = require('child_process');
const { check, throws, section, run } = require('./lib/check');

const root = path.join(fs.realpathSync(os.tmpdir()), `frogjs-modules-${process.pid}`);

function write(file, text) {
  const full = path.join(root, file);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, text);
  return full;
}

const load = (file) => require(path.join(root, file));
const runFile = (file, ...args) => execFileSync(process.execPath, [path.join(root, file), ...args], { encoding: 'utf8', cwd: root }).trim();

run(() => {
  section('Files');
  write('value.js', 'module.exports = 42;');
  check('module.exports', load('value.js'), 42);
  check('without the extension', load('value'), 42);
  write('exports.js', 'exports.a = 1; exports.b = this === exports;');
  check('exports and this', load('exports.js'), { a: 1, b: true });
  write('empty.js', '');
  check('empty file', load('empty.js'), {});
  write('data.json', '{"a": [1, 2]}');
  check('JSON', load('data.json'), { a: [1, 2] });
  check('JSON without the extension', load('data'), { a: [1, 2] });
  write('bom.json', '﻿{"bom": true}');
  check('JSON with a BOM', load('bom.json'), { bom: true });
  write('bom.js', '﻿module.exports = "bom";');
  check('source with a BOM', load('bom.js'), 'bom');
  write('hashbang.js', '#!/usr/bin/env frogjs\nmodule.exports = "ran";');
  check('hashbang line', load('hashbang.js'), 'ran');
  write('return.js', 'module.exports = 1; return; module.exports = 2;');
  check('top-level return', load('return.js'), 1);
  write('nul.js', 'module.exports = "a\\0b".length + "x\0y".length;');
  check('NUL in the source', load('nul.js'), 6);
  write('falsy.js', 'module.exports = 0;');
  check('falsy exports are cached', [load('falsy.js'), load('falsy.js')], [0, 0]);

  section('Scope');
  write('scope.js', 'module.exports = [__filename, __dirname, typeof require, typeof module, typeof exports]; var leaked = 1;');
  const scope = load('scope.js');
  check('__filename', scope[0], path.join(root, 'scope.js'));
  check('__dirname', scope[1], root);
  check('module variables', scope.slice(2), ['function', 'object', 'object']);
  check('var stays private', typeof leaked, 'undefined');

  section('Cache');
  write('count.js', 'global.frogCount = (global.frogCount || 0) + 1; module.exports = global.frogCount;');
  check('loaded once', [load('count.js'), load('count.js')], [1, 1]);
  const full = require.resolve(path.join(root, 'count.js'));
  check('require.resolve', full, path.join(root, 'count.js'));
  check('require.cache', require.cache[full].exports, 1);
  delete require.cache[full];
  check('deleting from the cache reloads', load('count.js'), 2);
  delete global.frogCount;

  section('Circular requires');
  write('a.js', "exports.early = 'a'; const b = require('./b'); exports.seen = b.sawEarly; exports.done = true;");
  write('b.js', "const a = require('./a'); exports.sawEarly = a.early; exports.sawDone = a.done;");
  check('partial exports', load('a.js'), { early: 'a', seen: 'a', done: true });
  check('the other side saw them partial', load('b.js'), { sawEarly: 'a', sawDone: undefined });

  section('Failures');
  write('throws.js', "global.frogTries = (global.frogTries || 0) + 1; throw new Error('boom');");
  throws('errors propagate', () => load('throws.js'), { message: 'boom' });
  throws('a failed module is not cached', () => load('throws.js'), { message: 'boom' });
  check('it ran twice', global.frogTries, 2);
  delete global.frogTries;
  write('syntax.js', 'this is not javascript');
  throws('syntax error', () => load('syntax.js'), { name: 'SyntaxError' });
  write('bad.json', '{nope}');
  throws('bad JSON', () => load('bad.json'), { name: 'SyntaxError', message: /bad\.json/ });
  throws('missing file', () => require('./no-such-file'), { code: 'MODULE_NOT_FOUND', message: /Cannot find module '\.\/no-such-file'/ });
  throws('missing package', () => require('no-such-package'), { code: 'MODULE_NOT_FOUND' });
  throws('non-string', () => require(1), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
  throws('empty string', () => require(''), { code: 'ERR_INVALID_ARG_VALUE' });

  section('Directories');
  write('dir/index.js', 'module.exports = "index";');
  check('index.js', load('dir'), 'index');
  write('jsondir/index.json', '{"index": "json"}');
  check('index.json', load('jsondir'), { index: 'json' });
  write('main/package.json', '{"main": "lib/start.js"}');
  write('main/lib/start.js', 'module.exports = "main";');
  check('package.json main', load('main'), 'main');
  write('mainext/package.json', '{"main": "start"}');
  write('mainext/start.js', 'module.exports = "main without extension";');
  check('main without an extension', load('mainext'), 'main without extension');
  write('maindir/package.json', '{"main": "lib"}');
  write('maindir/lib/index.js', 'module.exports = "main is a directory";');
  check('main is a directory', load('maindir'), 'main is a directory');
  write('nomain/package.json', '{"main": "missing.js"}');
  write('nomain/index.js', 'module.exports = "fallback";');
  check('missing main falls back to index', load('nomain'), 'fallback');
  write('badpkg/package.json', '{nope');
  throws('bad package.json', () => load('badpkg'), { message: /package\.json/ });

  section('node_modules');
  write('app/node_modules/plain/index.js', 'module.exports = "plain";');
  write('app/node_modules/@scope/pkg/index.js', 'module.exports = "scoped";');
  write('app/node_modules/withmain/package.json', '{"main": "./lib.js"}');
  write('app/node_modules/withmain/lib.js', 'module.exports = "with main";');
  write('app/node_modules/withmain/extra.js', 'module.exports = "extra";');
  write('app/node_modules/single.js', 'module.exports = "single file";');
  write('node_modules/upper/index.js', 'module.exports = "from a parent";');
  write('app/node_modules/inner/index.js', "module.exports = require('sibling');");
  write('app/node_modules/sibling/index.js', 'module.exports = "sibling";');
  write('app/node_modules/exported/package.json', JSON.stringify({ exports: { '.': { require: './cjs.js', import: './esm.mjs' }, './sub': './sub.js' } }));
  write('app/node_modules/exported/cjs.js', 'module.exports = "cjs";');
  write('app/node_modules/exported/sub.js', 'module.exports = "sub";');
  write('app/node_modules/exported/hidden.js', 'module.exports = "hidden";');
  write('app/node_modules/stringexp/package.json', JSON.stringify({ exports: './only.js' }));
  write('app/node_modules/stringexp/only.js', 'module.exports = "only";');
  write('app/node_modules/escape/package.json', JSON.stringify({ exports: '../plain/index.js' }));
  write('app/main.js', `
    const out = {};
    for (const name of ['plain', '@scope/pkg', 'withmain', 'withmain/extra', 'single', 'upper', 'inner', 'exported', 'exported/sub', 'stringexp']) out[name] = require(name);
    for (const name of ['exported/hidden', 'escape', 'stringexp/x', 'nope']) {
      try { require(name); out[name] = 'loaded'; } catch (err) { out[name] = err.code; }
    }
    console.log(JSON.stringify(out));`);
  check('lookup', JSON.parse(runFile('app/main.js')), {
    plain: 'plain', '@scope/pkg': 'scoped', withmain: 'with main', 'withmain/extra': 'extra', single: 'single file',
    upper: 'from a parent', inner: 'sibling', exported: 'cjs', 'exported/sub': 'sub', stringexp: 'only',
    'exported/hidden': 'ERR_PACKAGE_PATH_NOT_EXPORTED', escape: 'ERR_INVALID_PACKAGE_TARGET',
    'stringexp/x': 'ERR_PACKAGE_PATH_NOT_EXPORTED', nope: 'MODULE_NOT_FOUND',
  });

  section('Built-ins');
  for (const name of ['fs', 'net', 'http', 'path', 'os', 'events', 'stream', 'util', 'crypto', 'child_process', 'worker_threads', 'buffer', 'timers', 'process']) {
    check(`require('${name}')`, typeof require(name), name === 'events' || name === 'stream' ? 'function' : 'object');
    check(`node:${name}`, require('node:' + name) === require(name), true);
  }
  check('buffer.Buffer', require('buffer').Buffer, Buffer);
  throws('unknown node: name', () => require('node:nope'), { code: 'ERR_UNKNOWN_BUILTIN_MODULE' });
  check('require.resolve of a built-in', require.resolve('fs'), 'fs');
  const Module = require('module');
  check('builtinModules', Module.builtinModules.includes('fs'), true);
  write('created/x.js', 'module.exports = "created";');
  check('createRequire', Module.createRequire(path.join(root, 'created/y.js'))('./x'), 'created');
  throws('require.resolve of nothing', () => require.resolve('./nope'), { code: 'MODULE_NOT_FOUND' });

  section('The main script');
  write('main1.js', 'console.log(require.main === module, module.id, __filename === process.argv[1]);');
  check('require.main', runFile('main1.js'), 'true . true');
  write('main2.js', "console.log('before'); return; console.log('after');");
  check('top-level return', runFile('main2.js'), 'before');
  write('main3.js', '#!/usr/bin/env frogjs\nconsole.log("hashbang");');
  check('hashbang', runFile('main3.js'), 'hashbang');
  write('main4.js', 'var notGlobal = 1; let alsoNot = 2; console.log(typeof globalThis.notGlobal, typeof globalThis.alsoNot, typeof this);');
  check('own scope', runFile('main4.js'), 'undefined undefined object');
  write('main5.js', "const lib = require('./main5lib'); console.log(lib.parent);");
  write('main5lib.js', 'exports.parent = require.main.filename === module.parent.filename;');
  check('module.parent', runFile('main5.js'), 'true');
  write('main6.js', "throw new Error('in main');");
  const failed = spawnSync(process.execPath, [path.join(root, 'main6.js')], { encoding: 'utf8' });
  check('exit code', failed.status, 1);
  check('file name in the report', failed.stderr.includes(path.join(root, 'main6.js') + ':1'), true);
  check('stack in the report', failed.stderr.includes('Error: in main'), true);
  write('main7.js', 'exports.x = 1; console.log(module.exports.x, module.loaded, module.children.length, Array.isArray(module.paths));');
  check('module object', runFile('main7.js'), '1 false 0 true');

  fs.rmSync(root, { recursive: true });
});
