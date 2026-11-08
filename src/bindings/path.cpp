#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// POSIX paths, following the algorithms of Node.js's path.posix. It is JavaScript
// because path functions are small and called often: a native call for each
// would cost more than the work.
static const char* kPathSource = R"JS(
(function (process) {
'use strict';

function checkString(value, name) {
  if (typeof value !== 'string') {
    let shown;
    if (value === null || value === undefined) shown = String(value);
    else if (typeof value === 'function') shown = `function ${value.name}`;
    else if (typeof value === 'object') shown = value.constructor && value.constructor.name ? `an instance of ${value.constructor.name}` : 'an object';
    else shown = `type ${typeof value} (${String(value)})`;
    const err = new TypeError(`The "${name}" argument must be of type string. Received ${shown}`);
    err.code = 'ERR_INVALID_ARG_TYPE';
    throw err;
  }
}

// Resolve . and .. segments. Above the root they are dropped, or kept when allowed.
function normalizeSegments(path, allowAboveRoot) {
  const out = [];
  for (const segment of path.split('/')) {
    if (segment === '' || segment === '.') continue;
    if (segment === '..') {
      if (out.length > 0 && out[out.length - 1] !== '..') out.pop();
      else if (allowAboveRoot) out.push('..');
    } else {
      out.push(segment);
    }
  }
  return out.join('/');
}

function normalize(path) {
  checkString(path, 'path');
  if (path.length === 0) return '.';
  const absolute = path.charCodeAt(0) === 47;
  const trailing = path.charCodeAt(path.length - 1) === 47;
  let result = normalizeSegments(path, !absolute);
  if (result.length === 0) {
    if (absolute) return '/';
    return trailing ? './' : '.';
  }
  if (trailing) result += '/';
  return absolute ? '/' + result : result;
}

function join(...parts) {
  let joined;
  for (const part of parts) {
    checkString(part, 'path');
    if (part.length > 0) joined = joined === undefined ? part : joined + '/' + part;
  }
  return joined === undefined ? '.' : normalize(joined);
}

function resolve(...parts) {
  let resolved = '';
  let absolute = false;
  for (let i = parts.length - 1; i >= -1 && !absolute; i--) {
    const part = i >= 0 ? parts[i] : process.cwd();
    checkString(part, `paths[${i}]`);
    if (part.length === 0) continue;
    resolved = part + '/' + resolved;
    absolute = part.charCodeAt(0) === 47;
  }
  resolved = normalizeSegments(resolved, !absolute);
  if (absolute) return '/' + resolved;
  return resolved.length > 0 ? resolved : '.';
}

function isAbsolute(path) {
  checkString(path, 'path');
  return path.length > 0 && path.charCodeAt(0) === 47;
}

function dirname(path) {
  checkString(path, 'path');
  if (path.length === 0) return '.';
  const rooted = path.charCodeAt(0) === 47;
  let end = -1;
  let seenSegment = false;
  for (let i = path.length - 1; i >= 1; i--) {
    if (path.charCodeAt(i) === 47) {
      if (seenSegment) {
        end = i;
        break;
      }
    } else {
      seenSegment = true;
    }
  }
  if (end === -1) return rooted ? '/' : '.';
  if (rooted && end === 1) return '//';
  return path.slice(0, end);
}

function basename(path, suffix) {
  if (suffix !== undefined) checkString(suffix, 'suffix');
  checkString(path, 'path');
  let start = 0;
  let end = -1;
  let matchedSlash = true;

  if (suffix !== undefined && suffix.length > 0 && suffix.length <= path.length) {
    if (suffix === path) return '';
    // Match the suffix from the end while looking for the start of the name.
    // A name equal to the suffix is kept, as in Node.js.
    let suffixIndex = suffix.length - 1;
    let firstNonSlashEnd = -1;
    for (let i = path.length - 1; i >= 0; i--) {
      const code = path.charCodeAt(i);
      if (code === 47) {
        if (!matchedSlash) {
          start = i + 1;
          break;
        }
      } else {
        if (firstNonSlashEnd === -1) {
          matchedSlash = false;
          firstNonSlashEnd = i + 1;
        }
        if (suffixIndex >= 0) {
          if (code === suffix.charCodeAt(suffixIndex)) {
            if (--suffixIndex === -1) end = i;
          } else {
            suffixIndex = -1;
            end = firstNonSlashEnd;
          }
        }
      }
    }
    if (start === end) end = firstNonSlashEnd;
    else if (end === -1) end = path.length;
    return path.slice(start, end);
  }

  for (let i = path.length - 1; i >= 0; i--) {
    if (path.charCodeAt(i) === 47) {
      if (!matchedSlash) {
        start = i + 1;
        break;
      }
    } else if (end === -1) {
      matchedSlash = false;
      end = i + 1;
    }
  }
  return end === -1 ? '' : path.slice(start, end);
}

function extname(path) {
  checkString(path, 'path');
  const base = basename(path);
  const dot = base.lastIndexOf('.');
  // A leading dot starts the name (.bashrc), and .. has no extension
  if (dot <= 0 || base === '..') return '';
  return base.slice(dot);
}

function relative(from, to) {
  checkString(from, 'from');
  checkString(to, 'to');
  if (from === to) return '';
  const fromParts = resolve(from).split('/').filter(Boolean);
  const toParts = resolve(to).split('/').filter(Boolean);
  let common = 0;
  while (common < fromParts.length && common < toParts.length && fromParts[common] === toParts[common]) common++;
  return [...fromParts.slice(common).map(() => '..'), ...toParts.slice(common)].join('/');
}

function parse(path) {
  checkString(path, 'path');
  const result = { root: '', dir: '', base: '', ext: '', name: '' };
  if (path.length === 0) return result;
  if (path.charCodeAt(0) === 47) result.root = '/';
  result.base = basename(path);
  result.ext = extname(path);
  result.name = result.ext.length > 0 ? result.base.slice(0, -result.ext.length) : result.base;
  let end = path.length;
  while (end > 1 && path.charCodeAt(end - 1) === 47) end--;
  const slash = path.lastIndexOf('/', end - 1);
  if (slash > 0) result.dir = path.slice(0, slash);
  else if (slash === 0) result.dir = '/';
  return result;
}

function format(object) {
  if (object === null || typeof object !== 'object') {
    const err = new TypeError(`The "pathObject" argument must be of type object. Received ${object === null ? 'null' : `type ${typeof object}`}`);
    err.code = 'ERR_INVALID_ARG_TYPE';
    throw err;
  }
  const dir = object.dir || object.root;
  const base = object.base || `${object.name || ''}${object.ext ? (object.ext[0] === '.' ? '' : '.') + object.ext : ''}`;
  if (!dir) return base;
  return dir === object.root ? `${dir}${base}` : `${dir}/${base}`;
}

const path = {
  sep: '/', delimiter: ':',
  normalize, join, resolve, isAbsolute, dirname, basename, extname, relative, parse, format,
  toNamespacedPath: (value) => value,
};
path.posix = path;
return path;
})
)JS";

// Set up the path module (requires SetupProcess first)
void SetupPath(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();
    Local<Value> argv[1] = {global->Get(context, String::NewFromUtf8Literal(isolate, "process")).ToLocalChecked()};
    Local<Value> path;
    if (!frogjs::RunJsModule(isolate, context, "path", kPathSource, 1, argv).ToLocal(&path)) return;
    global->Set(context, String::NewFromUtf8Literal(isolate, "path"), path).Check();
}
