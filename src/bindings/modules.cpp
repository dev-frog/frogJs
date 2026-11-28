#include <v8.h>
#include <limits.h>
#include <stdlib.h>
#include <string>
#include "frogjs/errors.h"
#include "frogjs/esm.h"
#include "frogjs/js_module.h"
#include "frogjs/runtime.h"

using namespace v8;

// The module loader. CommonJS follows Node.js's algorithm: built-in names,
// relative and absolute paths, node_modules lookup, package.json main,
// "exports" and "imports", .js, .cjs and .json files, and a cache that makes
// circular require() return the partial exports. ES modules are resolved here
// too (the ESM rules: full paths, "import" conditions, file URLs) and linked
// and evaluated by esm.cpp, which calls back into the functions this returns.
static const char* kModulesSource = R"JS(
(function (native, builtins, fs, path, mainFilename) {
'use strict';

const cache = Object.create(null);
const builtinNames = Object.keys(builtins);
const { pathToFileURL, fileURLToPath, URL } = builtins.url;

function codeError(Base, code, message, extra) {
  const err = new Base(message);
  err.code = code;
  if (extra) Object.assign(err, extra);
  return err;
}

function notFound(request, parent) {
  const stack = [];
  for (let m = parent; m; m = m.parent) if (m.filename) stack.push(m.filename);
  let message = `Cannot find module '${request}'`;
  if (stack.length > 0) message += '\nRequire stack:\n- ' + stack.join('\n- ');
  return codeError(Error, 'MODULE_NOT_FOUND', message, { requireStack: stack });
}

function statKind(file) {
  const stats = fs.statSync(file, { throwIfNoEntry: false });
  if (!stats) return null;
  return stats.isDirectory() ? 'dir' : 'file';
}

function tryExtensions(file) {
  for (const extension of ['.js', '.json']) {
    if (statKind(file + extension) === 'file') return file + extension;
  }
  return null;
}

function readPackage(dir) {
  const file = path.join(dir, 'package.json');
  if (statKind(file) !== 'file') return null;
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (err) {
    err.message = `Error parsing ${file}: ${err.message}`;
    err.path = file;
    throw err;
  }
}

// The nearest package.json above a file, as { dir, pkg }, or null. As in
// Node.js the search stops at a node_modules directory.
function packageScope(file) {
  let dir = path.dirname(file);
  for (;;) {
    if (path.basename(dir) === 'node_modules') return null;
    const pkg = readPackage(dir);
    if (pkg) return { dir, pkg };
    const parent = path.dirname(dir);
    if (parent === dir) return null;
    dir = parent;
  }
}

// ---- package.json "exports" and "imports" ----
// Conditions match in the order of the object's keys; "default" always does.

const requireConditions = new Set(['require', 'node', 'module-sync']);
const importConditions = new Set(['import', 'node', 'module-sync']);

function invalidTarget(target, pkgFile, key) {
  return codeError(Error, 'ERR_INVALID_PACKAGE_TARGET',
    `Invalid "${key.startsWith('#') ? 'imports' : 'exports'}" target ${JSON.stringify(target)} defined for '${key}' in ${pkgFile}`);
}

const badSegment = (segment) => {
  const lower = segment.toLowerCase().replace(/%2e/g, '.');
  return lower === '' || lower === '.' || lower === '..' || lower === 'node_modules';
};

// A file ({ file }), a package specifier from "imports" ({ bare }), null (not
// exported) or undefined (no condition matched)
function resolveTarget(packageDir, target, match, key, conditions, pkgFile) {
  if (typeof target === 'string') {
    if (!target.startsWith('./')) {
      if (key.startsWith('#') && !target.startsWith('../') && !target.startsWith('/') && !/^[a-z][a-z0-9+.-]*:/i.test(target)) {
        return { bare: match === null ? target : target.replace(/\*/g, match) };
      }
      throw invalidTarget(target, pkgFile, key);
    }
    if (target.slice(2).split(/[\\/]/).some((segment, i, all) => badSegment(segment) && !(segment === '' && i === all.length - 1))) {
      throw invalidTarget(target, pkgFile, key);
    }
    if (match !== null && match.split(/[\\/]/).some((segment) => segment !== '' && badSegment(segment))) {
      throw codeError(TypeError, 'ERR_INVALID_MODULE_SPECIFIER', `Invalid module "${key.replace('*', match)}" request is not a valid match in pattern "${key}" for the "${key.startsWith('#') ? 'imports' : 'exports'}" resolution of ${pkgFile}`);
    }
    const resolved = path.resolve(packageDir, match === null ? target : target.replace(/\*/g, match));
    if (resolved !== packageDir && !resolved.startsWith(packageDir + '/')) throw invalidTarget(target, pkgFile, key);
    return { file: resolved };
  }
  if (Array.isArray(target)) {
    if (target.length === 0) return null;
    let lastError;
    for (const item of target) {
      let result;
      try {
        result = resolveTarget(packageDir, item, match, key, conditions, pkgFile);
      } catch (err) {
        lastError = err;
        if (err.code === 'ERR_INVALID_PACKAGE_TARGET') continue;
        throw err;
      }
      if (result === undefined) continue;
      if (result === null) {
        lastError = null;
        continue;
      }
      return result;
    }
    if (lastError) throw lastError;
    return null;
  }
  if (target !== null && typeof target === 'object') {
    for (const condition of Object.keys(target)) {
      if (condition !== 'default' && !conditions.has(condition)) continue;
      const result = resolveTarget(packageDir, target[condition], match, key, conditions, pkgFile);
      if (result !== undefined) return result;
    }
    return undefined;
  }
  if (target === null) return null;
  throw invalidTarget(target, pkgFile, key);
}

function patternKeyCompare(a, b) {
  const aBase = a.indexOf('*') === -1 ? a.length : a.indexOf('*') + 1;
  const bBase = b.indexOf('*') === -1 ? b.length : b.indexOf('*') + 1;
  if (aBase > bBase) return -1;
  if (bBase > aBase) return 1;
  if (a.indexOf('*') === -1) return 1;
  if (b.indexOf('*') === -1) return -1;
  if (a.length > b.length) return -1;
  if (b.length > a.length) return 1;
  return 0;
}

function resolveMap(packageDir, matchKey, map, conditions, pkgFile) {
  if (Object.prototype.hasOwnProperty.call(map, matchKey) && !matchKey.includes('*')) {
    return resolveTarget(packageDir, map[matchKey], null, matchKey, conditions, pkgFile);
  }
  const keys = Object.keys(map).filter((key) => key.indexOf('*') !== -1 && key.indexOf('*') === key.lastIndexOf('*'));
  keys.sort(patternKeyCompare);
  for (const key of keys) {
    const base = key.slice(0, key.indexOf('*'));
    if (!matchKey.startsWith(base) || matchKey === base) continue;
    const trailer = key.slice(key.indexOf('*') + 1);
    if (trailer === '' || (matchKey.endsWith(trailer) && matchKey.length >= key.length)) {
      const match = matchKey.slice(base.length, matchKey.length - trailer.length);
      return resolveTarget(packageDir, map[key], match, key, conditions, pkgFile);
    }
  }
  return undefined;
}

function exportsResolve(packageDir, subpath, exportsField, conditions) {
  const pkgFile = path.join(packageDir, 'package.json');
  let map = exportsField;
  const isConditions = map !== null && typeof map === 'object' && !Array.isArray(map) && !Object.keys(map).some((key) => key.startsWith('.'));
  if (typeof map === 'string' || Array.isArray(map) || isConditions) map = { '.': map };
  const result = resolveMap(packageDir, subpath, map, conditions, pkgFile);
  if (result === undefined || result === null) {
    const message = subpath === '.' ? `No "exports" main defined in ${pkgFile}`
      : `Package subpath '${subpath}' is not defined by "exports" in ${pkgFile}`;
    throw codeError(Error, 'ERR_PACKAGE_PATH_NOT_EXPORTED', message);
  }
  return result;
}

// '#name' through the "imports" of the package that contains `from`
function importsResolve(specifier, from, conditions) {
  if (specifier === '#' || specifier.startsWith('#/')) {
    throw codeError(TypeError, 'ERR_INVALID_MODULE_SPECIFIER', `Invalid module "${specifier}" is not a valid internal imports specifier name`);
  }
  const scope = packageScope(path.join(from, 'x'));
  if (scope && scope.pkg.imports !== null && typeof scope.pkg.imports === 'object') {
    const result = resolveMap(scope.dir, specifier, scope.pkg.imports, conditions, path.join(scope.dir, 'package.json'));
    if (result) return result.bare !== undefined ? { ...result, dir: scope.dir } : result;
  }
  throw codeError(TypeError, 'ERR_PACKAGE_IMPORT_NOT_DEFINED',
    `Package import specifier "${specifier}" is not defined${scope ? ` in package ${path.join(scope.dir, 'package.json')}` : ''}`);
}

function splitPackageName(request) {
  const parts = request.split('/');
  const nameLength = request.startsWith('@') ? 2 : 1;
  const name = parts.slice(0, nameLength).join('/');
  const subpath = '.' + (parts.length > nameLength ? '/' + parts.slice(nameLength).join('/') : '');
  return { name, subpath };
}

// A package that refers to itself by name, through its own "exports"
function selfResolve(request, from, conditions) {
  const scope = packageScope(path.join(from, 'x'));
  if (!scope || typeof scope.pkg.name !== 'string' || scope.pkg.exports === undefined || scope.pkg.exports === null) return null;
  const { name, subpath } = splitPackageName(request);
  if (name !== scope.pkg.name) return null;
  return exportsResolve(scope.dir, subpath, scope.pkg.exports, conditions);
}

// ---- CommonJS resolution ----

function tryDirectory(dir) {
  const pkg = readPackage(dir);
  if (pkg && typeof pkg.main === 'string' && pkg.main !== '') {
    const main = path.resolve(dir, pkg.main);
    const found = tryPath(main, true);
    if (found) return found;
  }
  for (const name of ['index.js', 'index.json']) {
    const file = path.join(dir, name);
    if (statKind(file) === 'file') return file;
  }
  return null;
}

function tryPath(file, noDirectoryMain) {
  const kind = statKind(file);
  if (kind === 'file') return file;
  const withExtension = tryExtensions(file);
  if (withExtension) return withExtension;
  if (kind === 'dir') {
    if (noDirectoryMain) {
      for (const name of ['index.js', 'index.json']) {
        const index = path.join(file, name);
        if (statKind(index) === 'file') return index;
      }
      return null;
    }
    return tryDirectory(file);
  }
  return null;
}

function nodeModulePaths(from) {
  const paths = [];
  let dir = from;
  for (;;) {
    if (path.basename(dir) !== 'node_modules') paths.push(path.join(dir, 'node_modules'));
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return paths;
}

function resolvePackage(request, from) {
  const self = selfResolve(request, from, requireConditions);
  if (self) return tryPath(self.file);
  const { name, subpath } = splitPackageName(request);
  for (const root of nodeModulePaths(from)) {
    const packageDir = path.join(root, name);
    if (statKind(packageDir) !== 'dir') {
      const found = tryPath(path.join(root, request));
      if (found) return found;
      continue;
    }
    const pkg = readPackage(packageDir);
    if (pkg && pkg.exports !== undefined && pkg.exports !== null) {
      const found = tryPath(exportsResolve(packageDir, subpath, pkg.exports, requireConditions).file);
      if (found) return found;
      continue;
    }
    const found = tryPath(path.join(root, request));
    if (found) return found;
  }
  return null;
}

function resolveFilename(request, parent) {
  const from = parent && parent.path ? parent.path : process.cwd();
  let found;
  if (request.startsWith('./') || request.startsWith('../') || request === '.' || request === '..' || path.isAbsolute(request)) {
    found = tryPath(path.resolve(from, request));
  } else if (request.startsWith('#')) {
    const target = importsResolve(request, from, requireConditions);
    if (target.bare !== undefined) {
      const builtin = builtinName(target.bare);
      if (builtin !== null) return target.bare;
      found = resolvePackage(target.bare, target.dir);
    } else {
      found = tryPath(target.file);
    }
  } else {
    found = resolvePackage(request, from);
  }
  if (!found) throw notFound(request, parent);
  return native.realpath(found);
}

function builtinName(request) {
  if (request.startsWith('node:')) {
    const name = request.slice(5);
    if (Object.prototype.hasOwnProperty.call(builtins, name)) return name;
    throw codeError(Error, 'ERR_UNKNOWN_BUILTIN_MODULE', `No such built-in module: ${request}`);
  }
  return Object.prototype.hasOwnProperty.call(builtins, request) ? request : null;
}

function checkRequest(request) {
  if (typeof request !== 'string') {
    throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "id" argument must be of type string. Received ${request === null ? 'null' : typeof request}`);
  }
  if (request === '') {
    throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', "The argument 'id' must be a non-empty string. Received ''");
  }
}

// ---- Module formats ----

// 'module', 'commonjs', 'json', 'detect' (a .js file with no "type": its
// syntax decides) or null (an extension ES modules can't load)
function formatOf(file) {
  const extension = path.extname(file);
  if (extension === '.mjs') return 'module';
  if (extension === '.cjs') return 'commonjs';
  if (extension === '.json') return 'json';
  if (extension === '.js' || extension === '') {
    const scope = packageScope(file);
    const type = scope && scope.pkg.type;
    if (type === 'module') return 'module';
    if (type === 'commonjs') return 'commonjs';
    return 'detect';
  }
  return null;
}

const detected = new Map();

// Whether a file without a "type" is an ES module: it is if it only parses as one
function detectFormat(file, source) {
  if (detected.has(file)) return detected.get(file);
  if (source === undefined) source = fs.readFileSync(file, 'utf8');
  const format = native.containsModuleSyntax(stripBom(source), file) ? 'module' : 'commonjs';
  detected.set(file, format);
  if (format === 'module') {
    const scope = packageScope(file);
    if (scope && scope.pkg.type === undefined) {
      process.emitWarning(`Module type of ${pathToFileURL(file).href} is not specified and it doesn't parse as CommonJS.\n` +
        'Reparsing as ES module because module syntax was detected. This incurs a performance overhead.\n' +
        `To eliminate this warning, add "type": "module" to ${path.join(scope.dir, 'package.json')}.`,
        { code: 'MODULE_TYPELESS_PACKAGE_JSON' });
    }
  }
  return format;
}

// ---- CommonJS modules ----

class Module {
  constructor(id, parent) {
    this.id = id;
    this.path = path.dirname(id);
    this.exports = {};
    this.filename = null;
    this.loaded = false;
    this.children = [];
    this.paths = [];
    Object.defineProperty(this, 'parent', { value: parent, enumerable: false, writable: true });
    if (parent) parent.children.push(this);
  }

  require(request) {
    return requireFrom(this, request);
  }

  load(filename) {
    this.filename = filename;
    this.path = path.dirname(filename);
    this.paths = nodeModulePaths(this.path);
    let format = filename.endsWith('.json') ? 'json' : formatOf(filename);
    // require() reads "type" only for .js files, as in Node.js: a file with no
    // extension is CommonJS unless it only parses as an ES module (yargs's
    // yargs/yargs is CommonJS in a "type": "module" package)
    if (format === 'module' && path.extname(filename) === '') format = 'detect';
    if (format === 'json') {
      this.loadJson(filename);
    } else if (format === 'module') {
      this.exports = requireModule(filename);
    } else {
      const source = fs.readFileSync(filename, 'utf8');
      if (format === 'detect' && detected.get(filename) === 'module') {
        this.exports = requireModule(filename);
      } else {
        this.compile(source, filename, format === 'detect');
      }
    }
    this.loaded = true;
  }

  loadJson(filename) {
    try {
      this.exports = JSON.parse(stripBom(fs.readFileSync(filename, 'utf8')));
    } catch (err) {
      err.message = `${filename}: ${err.message}`;
      throw err;
    }
  }

  compile(source, filename, detect) {
    let wrapper;
    try {
      wrapper = native.compile(stripBom(source).replace(/^#!/, '//'), filename);
    } catch (err) {
      // A .js file without a "type" that only parses as an ES module is one
      if (detect && err instanceof SyntaxError && detectFormat(filename, source) === 'module') {
        this.exports = requireModule(filename);
        return;
      }
      throw err;
    }
    if (detect) detected.set(filename, 'commonjs');
    wrapper.call(this.exports, this.exports, makeRequire(this), this, filename, this.path);
  }
}

function stripBom(text) {
  return text.charCodeAt(0) === 0xFEFF ? text.slice(1) : text;
}

// require() of an ES module: its namespace, if it has no top-level await
function requireModule(filename) {
  const url = pathToFileURL(filename).href;
  const namespace = native.requireModule({ format: 'module', filename, url });
  if ('module.exports' in namespace) return namespace['module.exports'];
  if (!('default' in namespace) || '__esModule' in namespace) return namespace;
  // As in Node.js, a module with a default export is marked __esModule, so
  // transpiled code finds the default: a facade module adds the name
  const target = JSON.stringify(url);
  return native.requireModule({
    format: 'module', filename, url: `${url}#frogjs-esm-facade`,
    source: `export * from ${target}; export { default } from ${target}; export const __esModule = true;`,
  });
}

function requireFrom(parent, request) {
  checkRequest(request);
  const builtin = builtinName(request);
  if (builtin !== null) return builtins[builtin];

  const filename = resolveFilename(request, parent);
  const builtinTarget = builtinName(filename);
  if (builtinTarget !== null) return builtins[builtinTarget];
  const cached = cache[filename];
  if (cached) {
    // Circular require: the partial exports, as they are now
    return cached.exports;
  }

  const module = new Module(filename, parent);
  cache[filename] = module;
  let loaded = false;
  try {
    module.load(filename);
    loaded = true;
  } finally {
    // A failed module can be required again
    if (!loaded) {
      delete cache[filename];
      if (parent) {
        const index = parent.children.indexOf(module);
        if (index >= 0) parent.children.splice(index, 1);
      }
    }
  }
  return module.exports;
}

let mainModule;

function makeRequire(module) {
  const require = (request) => requireFrom(module, request);
  require.resolve = (request) => {
    checkRequest(request);
    return builtinName(request) !== null ? request : resolveFilename(request, module);
  };
  require.cache = cache;
  require.main = mainModule;
  return require;
}

// ---- ES module resolution (called by esm.cpp) ----

function moduleNotFound(message, url) {
  return codeError(Error, 'ERR_MODULE_NOT_FOUND', message, url ? { url } : undefined);
}

// The file a referrer stands for: a file URL, a path, or a name like [eval]
function referrerPath(referrer) {
  if (typeof referrer !== 'string' || referrer === '') return path.join(process.cwd(), '[eval]');
  if (referrer.startsWith('file:')) return fileURLToPath(referrer);
  if (path.isAbsolute(referrer)) return referrer;
  return path.join(process.cwd(), referrer);
}

// The main file of a package without "exports": "main" with extensions, then index.js
function legacyMainResolve(packageDir, pkg) {
  if (pkg && typeof pkg.main === 'string' && pkg.main !== '') {
    const main = path.resolve(packageDir, pkg.main);
    for (const candidate of [main, main + '.js', main + '.json', path.join(main, 'index.js'), path.join(main, 'index.json')]) {
      if (statKind(candidate) === 'file') return candidate;
    }
  }
  const index = path.join(packageDir, 'index.js');
  return statKind(index) === 'file' ? index : null;
}

function packageResolve(specifier, from, parentPath) {
  const self = selfResolve(specifier, from, importConditions);
  if (self) return self.file;
  const { name, subpath } = splitPackageName(specifier);
  if (name === '' || name.startsWith('.') || name.includes('%') || name.includes('\\') || (specifier.startsWith('@') && !name.includes('/'))) {
    throw codeError(TypeError, 'ERR_INVALID_MODULE_SPECIFIER', `Invalid module "${specifier}" is not a valid package name imported from ${parentPath}`);
  }
  for (const root of nodeModulePaths(from)) {
    const packageDir = path.join(root, name);
    if (statKind(packageDir) !== 'dir') continue;
    const pkg = readPackage(packageDir);
    if (pkg && pkg.exports !== undefined && pkg.exports !== null) {
      return exportsResolve(packageDir, subpath, pkg.exports, importConditions).file;
    }
    if (subpath === '.') {
      const main = legacyMainResolve(packageDir, pkg);
      if (main) return main;
      throw moduleNotFound(`Cannot find package '${packageDir}' imported from ${parentPath}`);
    }
    return path.join(packageDir, subpath);
  }
  throw moduleNotFound(`Cannot find package '${name}' imported from ${parentPath}`);
}

function checkAttributes(attributes, format, url) {
  for (const key of Object.keys(attributes)) {
    if (key !== 'type') {
      throw codeError(TypeError, 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED', `Import attribute "${key}" with value "${attributes[key]}" is not supported in ${url}`);
    }
  }
  const type = attributes.type;
  if (format === 'json') {
    if (type === undefined) throw codeError(TypeError, 'ERR_IMPORT_ATTRIBUTE_MISSING', `Module "${url}" needs an import attribute of "type: json"`);
    if (type !== 'json') throw codeError(TypeError, 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED', `Import attribute "type" with value "${type}" is not supported in ${url}`);
  } else if (type !== undefined) {
    if (type === 'json') throw codeError(TypeError, 'ERR_IMPORT_ATTRIBUTE_TYPE_INCOMPATIBLE', `Module "${url}" is not of type "json"`);
    throw codeError(TypeError, 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED', `Import attribute "type" with value "${type}" is not supported in ${url}`);
  }
}

// data: URLs with JavaScript or JSON
function resolveDataURL(specifier, attributes) {
  const match = /^data:([^,;]*)((?:;[^,;]*)*?)(;base64)?,(.*)$/s.exec(specifier);
  if (!match) throw codeError(TypeError, 'ERR_INVALID_URL', 'Invalid URL', { input: specifier });
  const mime = match[1].toLowerCase();
  const body = match[3] ? Buffer.from(decodeURIComponent(match[4]), 'base64').toString('utf8') : decodeURIComponent(match[4]);
  let format;
  if (mime === 'text/javascript' || mime === 'application/javascript') format = 'module';
  else if (mime === 'application/json') format = 'json';
  else throw codeError(TypeError, 'ERR_UNKNOWN_MODULE_FORMAT', `Unknown module format: ${mime} for URL ${specifier}`);
  checkAttributes(attributes, format, specifier);
  return { format, url: specifier, filename: '', source: body };
}

// Resolve an import: { format, url, filename, name (built-ins), source (data: URLs) }
function esmResolve(specifier, referrer, attributes) {
  attributes = attributes || {};
  const parentPath = referrerPath(referrer);
  const from = path.dirname(parentPath);

  if (specifier.startsWith('node:') || Object.prototype.hasOwnProperty.call(builtins, specifier)) {
    const name = builtinName(specifier);
    if (name === null) throw codeError(Error, 'ERR_UNKNOWN_BUILTIN_MODULE', `No such built-in module: ${specifier}`);
    checkAttributes(attributes, 'builtin', 'node:' + name);
    return { format: 'builtin', url: 'node:' + name, filename: '', name };
  }
  if (specifier.startsWith('data:')) return resolveDataURL(specifier, attributes);

  let file;
  let suffix = '';
  if (specifier.startsWith('/') || specifier.startsWith('./') || specifier.startsWith('../') || specifier === '.' || specifier === '..' ||
      specifier.startsWith('file:')) {
    const url = new URL(specifier, pathToFileURL(parentPath));
    if (url.protocol !== 'file:') throw unsupportedScheme(url.protocol);
    suffix = url.search + url.hash;
    url.search = '';
    url.hash = '';
    file = fileURLToPath(url);
  } else if (specifier.startsWith('#')) {
    const target = importsResolve(specifier, from, importConditions);
    if (target.bare !== undefined) return esmResolve(target.bare, path.join(target.dir, 'package.json'), attributes);
    file = target.file;
  } else if (/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(specifier)) {
    throw unsupportedScheme(specifier.slice(0, specifier.indexOf(':') + 1));
  } else {
    file = packageResolve(specifier, from, parentPath);
  }

  const kind = statKind(file);
  if (kind === 'dir') {
    throw codeError(Error, 'ERR_UNSUPPORTED_DIR_IMPORT', `Directory import '${file}' is not supported resolving ES modules imported from ${parentPath}`,
      { url: pathToFileURL(file).href });
  }
  if (kind !== 'file') {
    throw moduleNotFound(`Cannot find module '${file}' imported from ${parentPath}`, pathToFileURL(file).href + suffix);
  }
  const real = native.realpath(file);
  const url = pathToFileURL(real).href + suffix;
  let format = formatOf(real);
  if (format === null) throw codeError(TypeError, 'ERR_UNKNOWN_FILE_EXTENSION', `Unknown file extension "${path.extname(real)}" for ${real}`);
  if (format === 'detect') format = detectFormat(real);
  checkAttributes(attributes, format, url);
  return { format, url, filename: real };
}

function unsupportedScheme(protocol) {
  return codeError(Error, 'ERR_UNSUPPORTED_ESM_URL_SCHEME',
    `Only URLs with a scheme in: file and data are supported by the default ESM loader. Received protocol '${protocol}'`);
}

// The names a CommonJS module exports, found by reading its source the way
// Node.js's cjs-module-lexer does: assignments to exports.name, defineProperty,
// object literals assigned to module.exports, and re-exports of other modules
function objectLiteralKeys(source, start, names, reexports) {
  // The rules of cjs-module-lexer's tryParseLiteralExports: an identifier key
  // counts unless `key: value` has a value that isn't an identifier; a string
  // key needs `: identifier`; anything else ends the scan
  let i = start;
  const skipSpace = () => {
    for (;;) {
      while (i < source.length && /\s/.test(source[i])) i++;
      if (source.startsWith('//', i)) i = source.indexOf('\n', i) === -1 ? source.length : source.indexOf('\n', i);
      else if (source.startsWith('/*', i)) i = source.indexOf('*/', i) === -1 ? source.length : source.indexOf('*/', i) + 2;
      else return;
    }
  };
  const valueIdentifier = () => {
    const value = /^[A-Za-z_$][\w$]*(?:\s*\.\s*[A-Za-z_$][\w$]*)*/.exec(source.slice(i, i + 300));
    if (!value) return false;
    i += value[0].length;
    return true;
  };
  for (;;) {
    skipSpace();
    const rest = source.slice(i, i + 300);
    const spread = /^\.\.\.\s*require\(\s*(['"])([^'"]+)\1\s*\)/.exec(rest);
    const name = /^[A-Za-z_$][\w$]*/.exec(rest);
    const quoted = /^(?:'([^'\\]*)'|"([^"\\]*)")/.exec(rest);
    if (name) {
      i += name[0].length;
      skipSpace();
      if (source[i] === ':') {
        i++;
        skipSpace();
        if (!valueIdentifier()) return;
      }
      names.add(name[0]);
    } else if (spread) {
      reexports.push(spread[2]);
      i += spread[0].length;
    } else if (quoted) {
      i += quoted[0].length;
      skipSpace();
      if (source[i] === ':') {
        i++;
        skipSpace();
        if (!valueIdentifier()) return;
        names.add(quoted[1] !== undefined ? quoted[1] : quoted[2]);
      }
    } else {
      return;
    }
    skipSpace();
    if (source[i] !== ',') return;
    i++;
  }
}

function cjsExportNames(filename, seen = new Set()) {
  if (seen.has(filename)) return [];
  seen.add(filename);
  let source;
  try {
    source = fs.readFileSync(filename, 'utf8');
  } catch {
    return [];
  }
  const names = new Set();
  const reexports = [];
  const start = '(?:^|[^.\\w$])';
  for (const match of source.matchAll(new RegExp(start + '(?:module\\s*\\.\\s*)?exports\\s*\\.\\s*([A-Za-z_$][\\w$]*)\\s*=(?!=)', 'g'))) names.add(match[1]);
  for (const match of source.matchAll(new RegExp(start + '(?:module\\s*\\.\\s*)?exports\\s*\\[\\s*([\'"])([^\'"]*)\\1\\s*\\]\\s*=(?!=)', 'g'))) names.add(match[2]);
  // defineProperty with a value, or a getter that returns an identifier or a
  // member expression (Babel and TypeScript re-exports)
  const getter = '(?:get\\s*:\\s*function\\s*\\w*\\s*\\(\\s*\\)|get\\s*\\(\\s*\\))\\s*\\{\\s*return\\s+[A-Za-z_$][\\w$]*(?:\\s*\\.\\s*[A-Za-z_$][\\w$]*)*\\s*;?\\s*\\}';
  const defineProperty = new RegExp('Object\\.defineProperty\\(\\s*(?:module\\s*\\.\\s*)?exports\\s*,\\s*([\'"])([^\'"]*)\\1\\s*,\\s*\\{\\s*' +
    '(?:enumerable\\s*:\\s*(?:true|!0)\\s*,\\s*)?(?:value\\s*:|' + getter + ')', 'g');
  for (const match of source.matchAll(defineProperty)) names.add(match[2]);
  for (const match of source.matchAll(new RegExp(start + 'module\\s*\\.\\s*exports\\s*=\\s*\\{', 'g'))) {
    objectLiteralKeys(source, match.index + match[0].length, names, reexports);
  }
  for (const match of source.matchAll(new RegExp(start + 'module\\s*\\.\\s*exports\\s*=\\s*require\\(\\s*([\'"])([^\'"]+)\\1\\s*\\)', 'g'))) reexports.push(match[2]);
  for (const match of source.matchAll(/__exportStar\(\s*require\(\s*(['"])([^'"]+)\1\s*\)/g)) reexports.push(match[2]);
  for (const match of source.matchAll(/__export\(\s*require\(\s*(['"])([^'"]+)\1\s*\)\s*\)/g)) reexports.push(match[2]);
  // Babel's export *: var _x = require('x'); Object.keys(_x).forEach(...)
  const requiredVars = new Map();
  for (const match of source.matchAll(/(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:_interopRequireWildcard\()?require\(\s*(['"])([^'"]+)\2\s*\)/g)) {
    requiredVars.set(match[1], match[3]);
  }
  for (const match of source.matchAll(/Object\.keys\(\s*([A-Za-z_$][\w$]*)\s*\)\.forEach\(\s*function\s*\(\s*[A-Za-z_$][\w$]*\s*\)/g)) {
    if (requiredVars.has(match[1])) reexports.push(requiredVars.get(match[1]));
  }

  for (const request of reexports) {
    try {
      const parent = { path: path.dirname(filename), filename };
      if (builtinName(request) !== null) {
        for (const key of Object.keys(builtins[builtinName(request)])) names.add(key);
        continue;
      }
      const target = resolveFilename(request, parent);
      if (target.endsWith('.json') || formatOf(target) === 'module') continue;
      for (const name of cjsExportNames(target, seen)) names.add(name);
    } catch {
      // A re-export that can't be resolved adds nothing
    }
  }
  names.delete('default');
  return [...names].filter((name) => typeof name === 'string');
}

// The exports of a non-ES-module record, when it is evaluated
function syntheticExports(record) {
  if (record.format === 'builtin') return builtins[record.name];
  if (record.format === 'commonjs') return requireFrom(null, record.filename);
  if (record.format === 'json') {
    const text = record.source !== undefined ? record.source : fs.readFileSync(record.filename, 'utf8');
    try {
      return JSON.parse(stripBom(text));
    } catch (err) {
      err.message = `${record.filename || record.url}: ${err.message}`;
      throw err;
    }
  }
  throw new Error(`Unknown module format: ${record.format}`);
}

function exportNames(record) {
  if (record.format === 'builtin') return Object.keys(builtins[record.name]);
  // As in Node.js, 'module.exports' names the whole object too
  if (record.format === 'commonjs') return cjsExportNames(record.filename).concat('module.exports');
  return [];
}

let mainURL = null;

function initMeta(meta, url) {
  if (url.startsWith('file:')) {
    const filename = fileURLToPath(url.replace(/[?#].*$/, ''));
    meta.dirname = path.dirname(filename);
    meta.filename = filename;
  }
  meta.main = url === mainURL;
  meta.resolve = function resolve(specifier, parent = undefined) {
    if (typeof specifier !== 'string') specifier = String(specifier);
    try {
      return esmResolve(specifier, parent === undefined ? url : String(parent)).url;
    } catch (err) {
      // A missing file still has a URL
      if (err.code === 'ERR_MODULE_NOT_FOUND' && err.url) return err.url;
      throw err;
    }
  };
  meta.url = url;
}

async function importDynamic(specifier, referrer, attributes) {
  const record = esmResolve(String(specifier), referrer, attributes);
  const [promise, namespace] = native.importModule(record);
  await promise;
  return namespace;
}

// ---- The main entry point ----

function runMain(filename) {
  const absolute = native.realpath(path.resolve(filename));
  let format = formatOf(absolute);
  if (format === 'json' || format === null) format = 'commonjs';
  if (format === 'module') return runMainModule(absolute);

  const module = new Module('.', null);
  mainModule = module;
  module.filename = absolute;
  cache[absolute] = module;
  if (format === 'detect') {
    module.filename = absolute;
    module.path = path.dirname(absolute);
    module.paths = nodeModulePaths(module.path);
    const source = fs.readFileSync(absolute, 'utf8');
    let wrapper;
    try {
      wrapper = native.compile(stripBom(source).replace(/^#!/, '//'), absolute);
    } catch (err) {
      if (err instanceof SyntaxError && detectFormat(absolute, source) === 'module') {
        delete cache[absolute];
        mainModule = undefined;
        return runMainModule(absolute);
      }
      throw err;
    }
    detected.set(absolute, 'commonjs');
    wrapper.call(module.exports, module.exports, makeRequire(module), module, absolute, module.path);
    module.loaded = true;
    return undefined;
  }
  module.load(absolute);
  return undefined;
}

function runMainModule(absolute) {
  mainURL = pathToFileURL(absolute).href;
  return native.runMainModule({ format: 'module', filename: absolute, url: mainURL });
}

// --eval and stdin code with import or export: an ES module named after the
// current directory, as in Node.js
function runMainSource(source, name) {
  mainURL = pathToFileURL(path.join(process.cwd(), name)).href;
  return native.runMainModule({ format: 'module', filename: '', url: mainURL, source });
}

builtins.module = {
  builtinModules: builtinNames.concat('module'),
  createRequire(from) {
    if (from !== null && typeof from === 'object' && typeof from.href === 'string') from = from.href;
    if (typeof from === 'string' && from.startsWith('file:')) from = fileURLToPath(from);
    const module = new Module(String(from), null);
    module.path = path.dirname(String(from));
    module.filename = String(from);
    return makeRequire(module);
  },
  isBuiltin(name) {
    try {
      return typeof name === 'string' && (name === 'module' || name === 'node:module' || builtinName(name) !== null);
    } catch {
      return false;
    }
  },
};

// For --eval, stdin, the REPL and worker eval code: relative to the current directory
const contextModule = new Module(path.join(process.cwd(), mainFilename), null);
contextModule.filename = mainFilename;
contextModule.path = process.cwd();
contextModule.paths = nodeModulePaths(contextModule.path);

return {
  runMain, runMainSource, globalRequire: makeRequire(contextModule), contextModule,
  esmResolve, exportNames, syntheticExports, initMeta, importDynamic,
};
})
)JS";

static thread_local Global<Function> tls_run_main;
static thread_local Global<Function> tls_run_main_source;

// realpath(file): the canonical path, or file itself if it can't be resolved
static void RealPath(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    String::Utf8Value file(isolate, args[0]);
    char resolved[PATH_MAX];
    if (*file != nullptr && realpath(*file, resolved) != nullptr) {
        args.GetReturnValue().Set(String::NewFromUtf8(isolate, resolved).ToLocalChecked());
    } else {
        args.GetReturnValue().Set(args[0]);
    }
}

// compile(source, filename): the module function. The wrapper's first line is
// hidden with a line offset of -1, so errors have the file's line numbers.
static void Compile(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> header = String::NewFromUtf8Literal(isolate, "(function (exports, require, module, __filename, __dirname) {\n");
    Local<String> footer = String::NewFromUtf8Literal(isolate, "\n})");
    Local<String> source = String::Concat(isolate, String::Concat(isolate, header, args[0].As<String>()), footer);

    ScriptOrigin origin(args[1], -1);
    Local<Script> script;
    Local<Value> result;
    if (!Script::Compile(context, source, &origin).ToLocal(&script) || !script->Run(context).ToLocal(&result)) return;
    args.GetReturnValue().Set(result);
}

namespace frogjs {

bool RunMainModule(Isolate* isolate, Local<Context> context, const std::string& filename) {
    HandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);
    if (tls_run_main.IsEmpty()) return false;
    Local<Value> argv[1] = {String::NewFromUtf8(isolate, filename.c_str()).ToLocalChecked()};
    if (tls_run_main.Get(isolate)->Call(context, Undefined(isolate), 1, argv).IsEmpty()) {
        return try_catch.HasCaught() && HandleUncaught(isolate, &try_catch);
    }
    return true;
}

bool RunMainSource(Isolate* isolate, Local<Context> context, const std::string& code, const std::string& name) {
    HandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);
    if (tls_run_main_source.IsEmpty()) return false;
    Local<Value> argv[2] = {String::NewFromUtf8(isolate, code.data(), NewStringType::kNormal, static_cast<int>(code.size())).ToLocalChecked(),
                            String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked()};
    if (tls_run_main_source.Get(isolate)->Call(context, Undefined(isolate), 2, argv).IsEmpty()) {
        return try_catch.HasCaught() && HandleUncaught(isolate, &try_catch);
    }
    return true;
}

void ResetModuleState() {
    tls_run_main.Reset();
    tls_run_main_source.Reset();
    ResetEsmState();
}

}  // namespace frogjs

// Set up require() (runs last: it needs every built-in module)
void SetupModules(Isolate* isolate, Local<Context> context, const std::string& mainFilePath) {
    Local<Object> global = context->Global();
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "realpath", RealPath);
    frogjs::SetFunction(isolate, context, native, "compile", Compile);
    frogjs::AddModuleNatives(isolate, context, native);

    auto get = [&](const char* name) {
        return global->Get(context, String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked();
    };
    auto set = [&](Local<Object> target, const char* name, Local<Value> value) {
        target->Set(context, String::NewFromUtf8(isolate, name).ToLocalChecked(), value).Check();
    };

    // require() keeps working when a script replaces the globals
    Local<Object> builtins = Object::New(isolate);
    for (const char* name : {"fs", "net", "http", "path", "os", "events", "stream", "util", "crypto",
                             "child_process", "worker_threads", "process"}) {
        set(builtins, name, get(name));
    }
    Local<Object> buffer_module = Object::New(isolate);
    set(buffer_module, "Buffer", get("Buffer"));
    set(buffer_module, "atob", get("atob"));
    set(buffer_module, "btoa", get("btoa"));
    set(builtins, "buffer", buffer_module);
    Local<Object> timers_module = Object::New(isolate);
    for (const char* name : {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "setImmediate", "clearImmediate"}) {
        set(timers_module, name, get(name));
    }
    set(builtins, "timers", timers_module);
    // Built-in modules that aren't globals (url, querystring, ...)
    Local<Object> registered = frogjs::RegisteredBuiltins(isolate, context);
    Local<Array> registered_names = registered->GetOwnPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < registered_names->Length(); i++) {
        Local<Value> name = registered_names->Get(context, i).ToLocalChecked();
        builtins->Set(context, name, registered->Get(context, name).ToLocalChecked()).Check();
    }
    set(timers_module, "promises", frogjs::GetBuiltin(isolate, context, "timers/promises"));
    // Other names for parts of modules, as in Node.js
    auto property = [&](const char* module, const char* name) {
        return get(module).As<Object>()->Get(context, String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked();
    };
    set(builtins, "fs/promises", property("fs", "promises"));
    set(builtins, "stream/promises", property("stream", "promises"));
    set(builtins, "path/posix", get("path"));
    Local<Object> perf_hooks = Object::New(isolate);
    set(perf_hooks, "performance", get("performance"));
    set(builtins, "perf_hooks", perf_hooks);

    // A name in brackets ([eval], [stdin], [repl], [worker eval]) isn't a file
    bool pseudo_file = mainFilePath.empty() || mainFilePath[0] == '[';
    Local<Value> argv[5] = {native, builtins, get("fs"), get("path"),
        String::NewFromUtf8(isolate, pseudo_file ? mainFilePath.c_str() : "[main]").ToLocalChecked()};
    Local<Value> loader_value;
    if (!frogjs::RunJsModule(isolate, context, "modules", kModulesSource, 5, argv).ToLocal(&loader_value)) return;
    Local<Object> loader = loader_value.As<Object>();
    frogjs::SetModuleLoader(isolate, loader);

    tls_run_main.Reset(isolate, loader->Get(context, String::NewFromUtf8Literal(isolate, "runMain")).ToLocalChecked().As<Function>());
    tls_run_main_source.Reset(isolate, loader->Get(context, String::NewFromUtf8Literal(isolate, "runMainSource")).ToLocalChecked().As<Function>());
    if (pseudo_file) {
        // The code isn't wrapped in a module function: these are globals. A
        // file has its own require (CommonJS) or none (ES modules).
        set(global, "require", loader->Get(context, String::NewFromUtf8Literal(isolate, "globalRequire")).ToLocalChecked());
        Local<Object> module = loader->Get(context, String::NewFromUtf8Literal(isolate, "contextModule")).ToLocalChecked().As<Object>();
        set(global, "module", module);
        set(global, "exports", module->Get(context, String::NewFromUtf8Literal(isolate, "exports")).ToLocalChecked());
        set(global, "__filename", String::NewFromUtf8(isolate, mainFilePath.c_str()).ToLocalChecked());
        set(global, "__dirname", String::NewFromUtf8Literal(isolate, "."));
    }
}
