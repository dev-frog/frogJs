#include <v8.h>
#include <uv.h>
#include "frogjs/allocator.h"
#include "frogjs/buffer.h"
#include "frogjs/encoding.h"
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/loop.h"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace v8;

// The fs module follows Node.js: argument checks, Stats, streams and promises
// are JavaScript. Native code has one function per operation. Each builds an
// FsOp and either runs it right away (the *Sync functions, no callback) or on
// the libuv thread pool, so both forms behave the same.
static const char* kFsSource = R"JS(
(function (native, stream) {
'use strict';

const { Readable, Writable } = stream;
const { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND, O_EXCL, O_SYNC } = native.constants;

// The most one read or write handles (reads return less, like the system call)
const MAX_IO_BYTES = 2147483647;

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function received(value) {
  if (value === null || value === undefined) return `Received ${value}`;
  if (typeof value === 'function') return `Received function ${value.name}`;
  if (typeof value === 'object') {
    const name = value.constructor && value.constructor.name;
    return name ? `Received an instance of ${name}` : 'Received an object';
  }
  return `Received type ${typeof value} (${typeof value === 'string' ? `'${value}'` : String(value)})`;
}

function invalidType(name, expected, value) {
  return makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. ${received(value)}`);
}

function toPath(value, name = 'path') {
  if (value instanceof Uint8Array) value = Buffer.from(value).toString();
  if (typeof value !== 'string') throw invalidType(name, 'of type string or an instance of Buffer', value);
  if (value.includes('\0')) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE',
      `The argument '${name}' must be a string or Uint8Array without null bytes. Received ${JSON.stringify(value)}`);
  }
  return value;
}

function checkCallback(callback) {
  if (typeof callback !== 'function') throw invalidType('cb', 'of type function', callback);
  return callback;
}

function checkFd(fd) {
  if (typeof fd !== 'number') throw invalidType('fd', 'of type number', fd);
  if (!Number.isInteger(fd) || fd < 0 || fd > 2147483647) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE',
      `The value of "fd" is out of range. It must be >= 0 && <= 2147483647. Received ${fd}`);
  }
  return fd;
}

function getOptions(options, defaults) {
  if (options === null || options === undefined || typeof options === 'function') return defaults;
  if (typeof options === 'string') options = { encoding: options };
  else if (typeof options !== 'object') throw invalidType('options', 'of type string or an instance of Object', options);
  const merged = Object.assign({}, defaults, options);
  const encoding = merged.encoding;
  if (encoding && encoding !== 'buffer' && !Buffer.isEncoding(encoding)) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE',
      `The argument 'encoding' is invalid encoding. Received ${JSON.stringify(encoding)}`);
  }
  return merged;
}

const FLAGS = {
  r: O_RDONLY, rs: O_RDONLY | O_SYNC, sr: O_RDONLY | O_SYNC,
  'r+': O_RDWR, 'rs+': O_RDWR | O_SYNC, 'sr+': O_RDWR | O_SYNC,
  w: O_TRUNC | O_CREAT | O_WRONLY, wx: O_TRUNC | O_CREAT | O_WRONLY | O_EXCL, xw: O_TRUNC | O_CREAT | O_WRONLY | O_EXCL,
  'w+': O_TRUNC | O_CREAT | O_RDWR, 'wx+': O_TRUNC | O_CREAT | O_RDWR | O_EXCL, 'xw+': O_TRUNC | O_CREAT | O_RDWR | O_EXCL,
  a: O_APPEND | O_CREAT | O_WRONLY, ax: O_APPEND | O_CREAT | O_WRONLY | O_EXCL, xa: O_APPEND | O_CREAT | O_WRONLY | O_EXCL,
  as: O_APPEND | O_CREAT | O_WRONLY | O_SYNC, sa: O_APPEND | O_CREAT | O_WRONLY | O_SYNC,
  'a+': O_APPEND | O_CREAT | O_RDWR, 'ax+': O_APPEND | O_CREAT | O_RDWR | O_EXCL, 'xa+': O_APPEND | O_CREAT | O_RDWR | O_EXCL,
  'as+': O_APPEND | O_CREAT | O_RDWR | O_SYNC, 'sa+': O_APPEND | O_CREAT | O_RDWR | O_SYNC,
};

function toFlags(flags, fallback = 'r') {
  if (flags === null || flags === undefined) flags = fallback;
  if (typeof flags === 'number') return flags;
  const value = FLAGS[flags];
  if (value === undefined) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'flags' is invalid. Received ${JSON.stringify(flags)}`);
  }
  return value;
}

function toMode(mode, fallback) {
  if (mode === null || mode === undefined) return fallback;
  if (typeof mode === 'string') {
    const parsed = parseInt(mode, 8);
    if (Number.isNaN(parsed)) {
      throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'mode' must be a 32-bit unsigned integer or an octal string. Received ${JSON.stringify(mode)}`);
    }
    return parsed;
  }
  if (typeof mode !== 'number') throw invalidType('mode', 'of type number', mode);
  return mode;
}

function checkWriteSize(bytes) {
  if (bytes.byteLength > MAX_IO_BYTES) {
    throw makeError(RangeError, 'ERR_FS_FILE_TOO_LARGE', `Cannot write ${bytes.byteLength} bytes at once: the limit is 2 GiB`);
  }
  return bytes;
}

function toData(data, encoding) {
  if (typeof data === 'string') return Buffer.from(data, encoding || 'utf8');
  if (data instanceof Uint8Array) return checkWriteSize(data);
  if (ArrayBuffer.isView(data)) return checkWriteSize(new Uint8Array(data.buffer, data.byteOffset, data.byteLength));
  throw invalidType('data', 'of type string or an instance of Buffer, TypedArray, or DataView', data);
}

function decode(buffer, encoding) {
  return encoding && encoding !== 'buffer' ? buffer.toString(encoding) : buffer;
}

const S_IFMT = 0o170000;

class Stats {
  constructor(v) {
    [this.dev, this.mode, this.nlink, this.uid, this.gid, this.rdev, this.blksize, this.ino, this.size, this.blocks,
      this.atimeMs, this.mtimeMs, this.ctimeMs, this.birthtimeMs] = v;
    this.atime = new Date(this.atimeMs);
    this.mtime = new Date(this.mtimeMs);
    this.ctime = new Date(this.ctimeMs);
    this.birthtime = new Date(this.birthtimeMs);
  }
  isFile() { return (this.mode & S_IFMT) === 0o100000; }
  isDirectory() { return (this.mode & S_IFMT) === 0o040000; }
  isSymbolicLink() { return (this.mode & S_IFMT) === 0o120000; }
  isFIFO() { return (this.mode & S_IFMT) === 0o010000; }
  isSocket() { return (this.mode & S_IFMT) === 0o140000; }
  isBlockDevice() { return (this.mode & S_IFMT) === 0o060000; }
  isCharacterDevice() { return (this.mode & S_IFMT) === 0o020000; }
}

// ---- Whole files ----

function readFileSync(path, options) {
  const { encoding, flag } = getOptions(options, { encoding: null, flag: 'r' });
  const target = typeof path === 'number' ? checkFd(path) : toPath(path);
  return decode(native.readFile(target, toFlags(flag)), encoding);
}

function readFile(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  const { encoding, flag } = getOptions(options, { encoding: null, flag: 'r' });
  const target = typeof path === 'number' ? checkFd(path) : toPath(path);
  native.readFile(target, toFlags(flag), (err, buffer) => {
    if (err) return callback(err);
    let result;
    try {
      result = decode(buffer, encoding);
    } catch (decodeError) {
      return callback(decodeError);
    }
    callback(null, result);
  });
}

function writeTarget(path) {
  return typeof path === 'number' ? checkFd(path) : toPath(path);
}

function writeFileSync(path, data, options) {
  const { encoding, flag, mode } = getOptions(options, { encoding: 'utf8', flag: 'w', mode: 0o666 });
  native.writeFile(writeTarget(path), toData(data, encoding), toFlags(flag), toMode(mode, 0o666));
}

function writeFile(path, data, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  const { encoding, flag, mode } = getOptions(options, { encoding: 'utf8', flag: 'w', mode: 0o666 });
  native.writeFile(writeTarget(path), toData(data, encoding), toFlags(flag), toMode(mode, 0o666), (err) => callback(err));
}

function appendFileSync(path, data, options) {
  writeFileSync(path, data, getOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'a' }));
}

function appendFile(path, data, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  writeFile(path, data, getOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'a' }), callback);
}

// ---- Directories, metadata, deletion ----

function mkdirOptions(options) {
  if (typeof options === 'number' || typeof options === 'string') return { mode: toMode(options, 0o777), recursive: false };
  const { mode, recursive } = options || {};
  return { mode: toMode(mode, 0o777), recursive: !!recursive };
}

function mkdirSync(path, options) {
  const { mode, recursive } = mkdirOptions(options);
  return native.mkdir(toPath(path), mode, recursive);
}

function mkdir(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  const { mode, recursive } = mkdirOptions(typeof options === 'function' ? undefined : options);
  native.mkdir(toPath(path), mode, recursive, (err, first) => (err ? callback(err) : callback(null, first)));
}

function readdirResult(names, base, withFileTypes) {
  if (!withFileTypes) return names;
  return names.map((name) => new Dirent(name, base));
}

// Directory entries from readdir(path, { withFileTypes: true }). The type comes
// from lstat, so a file removed since the listing reports as neither.
class Dirent {
  constructor(name, base) {
    this.name = name;
    this.parentPath = base;
    let stats = null;
    try {
      stats = lstatSync(base + '/' + name);
    } catch {
      // Removed meanwhile
    }
    Object.defineProperty(this, '_stats', { value: stats, enumerable: false });
  }
  isFile() { return !!this._stats && this._stats.isFile(); }
  isDirectory() { return !!this._stats && this._stats.isDirectory(); }
  isSymbolicLink() { return !!this._stats && this._stats.isSymbolicLink(); }
}

function readdirSync(path, options) {
  const target = toPath(path);
  const { withFileTypes } = getOptions(options, {});
  return readdirResult(native.readdir(target), target, withFileTypes);
}

function readdir(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  const target = toPath(path);
  const { withFileTypes } = getOptions(options, {});
  native.readdir(target, (err, names) => {
    if (err) return callback(err);
    callback(null, readdirResult(names, target, withFileTypes));
  });
}

function statSync(path, options) {
  const target = toPath(path);
  try {
    return new Stats(native.stat(target));
  } catch (err) {
    if (options && options.throwIfNoEntry === false && (err.code === 'ENOENT' || err.code === 'ENOTDIR')) return undefined;
    throw err;
  }
}

function stat(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  native.stat(toPath(path), (err, v) => (err ? callback(err) : callback(null, new Stats(v))));
}

function lstatSync(path, options) {
  const target = toPath(path);
  try {
    return new Stats(native.lstat(target));
  } catch (err) {
    if (options && options.throwIfNoEntry === false && (err.code === 'ENOENT' || err.code === 'ENOTDIR')) return undefined;
    throw err;
  }
}

function lstat(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  native.lstat(toPath(path), (err, v) => (err ? callback(err) : callback(null, new Stats(v))));
}

function realpathSync(path) {
  return native.realpath(toPath(path));
}

function realpath(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  native.realpath(toPath(path), (err, resolved) => (err ? callback(err) : callback(null, resolved)));
}

function fstatSync(fd) {
  return new Stats(native.fstat(checkFd(fd)));
}

function fstat(fd, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  native.fstat(checkFd(fd), (err, v) => (err ? callback(err) : callback(null, new Stats(v))));
}

function existsSync(path) {
  try {
    native.access(toPath(path), native.constants.F_OK);
    return true;
  } catch {
    return false;
  }
}

function accessSync(path, mode) {
  native.access(toPath(path), mode === undefined ? native.constants.F_OK : mode);
}

function access(path, mode, callback) {
  callback = checkCallback(typeof mode === 'function' ? mode : callback);
  native.access(toPath(path), typeof mode === 'number' ? mode : native.constants.F_OK, (err) => callback(err));
}

function unlinkSync(path) {
  native.unlink(toPath(path));
}

function unlink(path, callback) {
  checkCallback(callback);
  native.unlink(toPath(path), (err) => callback(err));
}

function rmdirSync(path) {
  native.rmdir(toPath(path));
}

function rmdir(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  native.rmdir(toPath(path), (err) => callback(err));
}

function renameSync(oldPath, newPath) {
  native.rename(toPath(oldPath, 'oldPath'), toPath(newPath, 'newPath'));
}

function rename(oldPath, newPath, callback) {
  checkCallback(callback);
  native.rename(toPath(oldPath, 'oldPath'), toPath(newPath, 'newPath'), (err) => callback(err));
}

function copyFileSync(src, dest, mode) {
  native.copyFile(toPath(src, 'src'), toPath(dest, 'dest'), mode || 0);
}

function copyFile(src, dest, mode, callback) {
  callback = checkCallback(typeof mode === 'function' ? mode : callback);
  native.copyFile(toPath(src, 'src'), toPath(dest, 'dest'), typeof mode === 'number' ? mode : 0, (err) => callback(err));
}

function isDirectoryError(path) {
  const err = makeError(Error, 'ERR_FS_EISDIR', `Path is a directory: rm returned EISDIR (is a directory) ${path}`);
  err.path = path;
  err.syscall = 'rm';
  err.errno = 21;
  return err;
}

function rmSync(path, options) {
  const target = toPath(path);
  const { recursive, force } = options || {};
  const stats = lstatSync(target, { throwIfNoEntry: !force });
  if (stats === undefined) return;
  if (!stats.isDirectory()) return unlinkSync(target);
  if (!recursive) throw isDirectoryError(target);
  // Symbolic links are removed, not followed: lstat above decides
  for (const name of readdirSync(target)) rmSync(target + '/' + name, { recursive: true, force });
  rmdirSync(target);
}

function rm(path, options, callback) {
  callback = checkCallback(typeof options === 'function' ? options : callback);
  const target = toPath(path);
  const { recursive, force } = (typeof options === 'object' && options) || {};
  lstat(target, (err, stats) => {
    if (err) return force && err.code === 'ENOENT' ? callback(null) : callback(err);
    if (!stats.isDirectory()) return unlink(target, callback);
    if (!recursive) return callback(isDirectoryError(target));
    readdir(target, (readError, names) => {
      if (readError) return callback(readError);
      let index = 0;
      const next = (removeError) => {
        if (removeError) return callback(removeError);
        if (index >= names.length) return rmdir(target, callback);
        rm(target + '/' + names[index++], { recursive: true, force }, next);
      };
      next();
    });
  });
}

// ---- File descriptors ----

function openSync(path, flags, mode) {
  return native.open(toPath(path), toFlags(flags), toMode(mode, 0o666));
}

function open(path, flags, mode, callback) {
  if (typeof flags === 'function') {
    callback = flags;
    flags = undefined;
    mode = undefined;
  } else if (typeof mode === 'function') {
    callback = mode;
    mode = undefined;
  }
  checkCallback(callback);
  native.open(toPath(path), toFlags(flags), toMode(mode, 0o666), (err, fd) => (err ? callback(err) : callback(null, fd)));
}

function closeSync(fd) {
  native.close(checkFd(fd));
}

function close(fd, callback) {
  callback = callback === undefined ? () => {} : checkCallback(callback);
  native.close(checkFd(fd), (err) => callback(err));
}

function readRange(buffer, offset, length) {
  if (!ArrayBuffer.isView(buffer)) throw invalidType('buffer', 'an instance of Buffer, TypedArray, or DataView', buffer);
  if (offset === undefined || offset === null) offset = 0;
  if (length === undefined || length === null) length = buffer.byteLength - offset;
  if (!Number.isInteger(offset) || offset < 0 || offset > buffer.byteLength) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "offset" is out of range. It must be >= 0 && <= ${buffer.byteLength}. Received ${offset}`);
  }
  if (!Number.isInteger(length) || length < 0 || length > buffer.byteLength - offset) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "length" is out of range. It must be >= 0 && <= ${buffer.byteLength - offset}. Received ${length}`);
  }
  return { offset, length };
}

function copyInto(buffer, offset, data) {
  new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength).set(data, offset);
}

function toPosition(position) {
  return typeof position === 'number' && position >= 0 ? position : -1;
}

// read(fd, buffer[, offset[, length[, position]]])
function readSync(fd, buffer, offset, length, position) {
  if (offset !== null && typeof offset === 'object') ({ offset, length, position } = offset);
  const range = readRange(buffer, offset, length);
  const data = native.read(checkFd(fd), Math.min(range.length, MAX_IO_BYTES), toPosition(position));
  copyInto(buffer, range.offset, data);
  return data.length;
}

function read(fd, buffer, offset, length, position, callback) {
  if (typeof offset === 'function') {
    callback = offset;
    offset = undefined;
  } else if (offset !== null && typeof offset === 'object') {
    callback = length;
    ({ offset, length, position } = offset);
  }
  checkCallback(callback);
  const range = readRange(buffer, offset, length);
  native.read(checkFd(fd), Math.min(range.length, MAX_IO_BYTES), toPosition(position), (err, data) => {
    if (err) return callback(err, 0, buffer);
    copyInto(buffer, range.offset, data);
    callback(null, data.length, buffer);
  });
}

// write(fd, buffer[, offset[, length[, position]]]) or write(fd, string[, position[, encoding]])
function writeArgs(data, a, b, c) {
  if (typeof data === 'string') {
    return { bytes: Buffer.from(data, typeof b === 'string' ? b : 'utf8'), position: toPosition(a) };
  }
  if (!ArrayBuffer.isView(data)) throw invalidType('buffer', 'of type string or an instance of Buffer, TypedArray, or DataView', data);
  const range = readRange(data, a, b);
  const bytes = checkWriteSize(new Uint8Array(data.buffer, data.byteOffset + range.offset, range.length));
  return { bytes, position: toPosition(c) };
}

function writeSync(fd, data, a, b, c) {
  const { bytes, position } = writeArgs(data, a, b, c);
  return native.write(checkFd(fd), bytes, position);
}

function write(fd, data, ...rest) {
  const callback = checkCallback(rest.pop());
  const { bytes, position } = writeArgs(data, rest[0], rest[1], rest[2]);
  native.write(checkFd(fd), bytes, position, (err, written) => callback(err, written || 0, data));
}

// ---- Streams ----

class ReadStream extends Readable {
  constructor(path, options) {
    const opts = getOptions(options, {});
    super({ highWaterMark: opts.highWaterMark === undefined ? 64 * 1024 : opts.highWaterMark, encoding: opts.encoding || undefined, autoDestroy: opts.autoClose !== false, emitClose: opts.emitClose });
    this.path = opts.fd === undefined || opts.fd === null ? toPath(path) : path;
    this.fd = opts.fd === undefined ? null : checkFd(opts.fd);
    this.flags = opts.flags === undefined ? 'r' : opts.flags;
    this.mode = toMode(opts.mode, 0o666);
    this.start = opts.start;
    this.end = opts.end === undefined ? Infinity : opts.end;
    this.autoClose = opts.autoClose !== false;
    this.bytesRead = 0;
    this.pos = this.start;
    for (const [name, value] of [['start', this.start], ['end', this.end]]) {
      if (value !== undefined && value !== Infinity && (!Number.isInteger(value) || value < 0)) {
        throw makeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "${name}" is out of range. It must be >= 0. Received ${value}`);
      }
    }
    if (this.start !== undefined && this.start > this.end) {
      throw makeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "start" is out of range. It must be <= "end" (here: ${this.end}). Received ${this.start}`);
    }
    if (this.fd === null) this._open();
  }

  _open() {
    open(this.path, this.flags, this.mode, (err, fd) => {
      if (err) return this.destroy(err);
      this.fd = fd;
      this.emit('open', fd);
      this.emit('ready');
    });
  }

  _read(size) {
    if (this.fd === null) {
      this.once('open', () => this._read(size));
      return;
    }
    if (this.destroyed) return;
    const remaining = this.end === Infinity ? size : Math.min(size, this.end - (this.pos === undefined ? this.bytesRead : this.pos) + 1);
    if (remaining <= 0) {
      this.push(null);
      return;
    }
    native.read(this.fd, remaining, toPosition(this.pos), (err, data) => {
      if (err) return this.destroy(err);
      if (data.length === 0) {
        this.push(null);
        return;
      }
      this.bytesRead += data.length;
      if (this.pos !== undefined) this.pos += data.length;
      this.push(data);
    });
  }

  _destroy(err, callback) {
    if (this.fd === null) {
      // Still opening: close it once it has opened
      this.once('open', () => this._closeFd(err, callback));
      if (err && err.syscall === 'open') callback(err);
      return;
    }
    this._closeFd(err, callback);
  }

  _closeFd(err, callback) {
    if (this.fd === null || !this.autoClose) return callback(err);
    const fd = this.fd;
    this.fd = null;
    native.close(fd, (closeError) => callback(err || closeError));
  }

  close(callback) {
    if (typeof callback === 'function') this.once('close', callback);
    this.destroy();
  }
}

class WriteStream extends Writable {
  constructor(path, options) {
    const opts = getOptions(options, {});
    super({ highWaterMark: opts.highWaterMark, defaultEncoding: opts.encoding || 'utf8', autoDestroy: opts.autoClose !== false, emitClose: opts.emitClose, decodeStrings: false });
    this.path = opts.fd === undefined || opts.fd === null ? toPath(path) : path;
    this.fd = opts.fd === undefined ? null : checkFd(opts.fd);
    this.flags = opts.flags === undefined ? 'w' : opts.flags;
    this.mode = toMode(opts.mode, 0o666);
    this.start = opts.start;
    this.autoClose = opts.autoClose !== false;
    this.bytesWritten = 0;
    this.pos = this.start;
    if (this.start !== undefined && (!Number.isInteger(this.start) || this.start < 0)) {
      throw makeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "start" is out of range. It must be >= 0. Received ${this.start}`);
    }
    if (this.fd === null) this._open();
  }

  _open() {
    open(this.path, this.flags, this.mode, (err, fd) => {
      if (err) return this.destroy(err);
      this.fd = fd;
      this.emit('open', fd);
      this.emit('ready');
    });
  }

  _write(chunk, encoding, callback) {
    if (this.fd === null) {
      this.once('open', () => this._write(chunk, encoding, callback));
      return;
    }
    const bytes = typeof chunk === 'string' ? Buffer.from(chunk, encoding === 'buffer' ? 'utf8' : encoding) : chunk;
    const writeAll = (offset) => {
      const part = bytes.subarray(offset, offset + MAX_IO_BYTES);
      native.write(this.fd, part, toPosition(this.pos), (err, written) => {
        if (err) return callback(err);
        this.bytesWritten += written;
        if (this.pos !== undefined) this.pos += written;
        if (offset + written < bytes.length) writeAll(offset + written);
        else callback();
      });
    };
    writeAll(0);
  }

  _destroy(err, callback) {
    if (this.fd === null) {
      this.once('open', () => this._closeFd(err, callback));
      if (err && err.syscall === 'open') callback(err);
      return;
    }
    this._closeFd(err, callback);
  }

  _closeFd(err, callback) {
    if (this.fd === null || !this.autoClose) return callback(err);
    const fd = this.fd;
    this.fd = null;
    native.close(fd, (closeError) => callback(err || closeError));
  }

  close(callback) {
    if (typeof callback === 'function') this.once('close', callback);
    this.end();
  }
}

function createReadStream(path, options) {
  return new ReadStream(path, options);
}

function createWriteStream(path, options) {
  return new WriteStream(path, options);
}

// ---- Promises ----

function promisified(fn) {
  return (...args) => new Promise((resolve, reject) => {
    fn(...args, (err, value) => (err ? reject(err) : resolve(value)));
  });
}

const promises = {
  readFile: promisified(readFile),
  writeFile: promisified(writeFile),
  appendFile: promisified(appendFile),
  mkdir: promisified(mkdir),
  readdir: promisified(readdir),
  rmdir: promisified(rmdir),
  rm: promisified(rm),
  unlink: promisified(unlink),
  stat: promisified(stat),
  lstat: promisified(lstat),
  realpath: promisified(realpath),
  rename: promisified(rename),
  copyFile: promisified(copyFile),
  access: promisified(access),
};

return {
  readFileSync, readFile, writeFileSync, writeFile, appendFileSync, appendFile,
  mkdirSync, mkdir, readdirSync, readdir, rmdirSync, rmdir, rmSync, rm,
  statSync, stat, lstatSync, lstat, fstatSync, fstat, realpathSync, realpath, existsSync, accessSync, access,
  unlinkSync, unlink, renameSync, rename, copyFileSync, copyFile,
  openSync, open, closeSync, close, readSync, read, writeSync, write,
  createReadStream, createWriteStream, ReadStream, WriteStream, Stats, Dirent,
  constants: native.constants, promises,
};
})
)JS";

namespace {

enum class Kind {
    kReadFile, kWriteFile, kMkdir, kReaddir, kRmdir, kUnlink, kStat, kLstat, kFstat,
    kAccess, kRename, kCopyFile, kOpen, kClose, kRead, kWrite, kRealpath,
};

// One file system operation. Input comes from JavaScript; Execute() runs without
// touching V8, possibly on a thread pool thread.
struct FsOp {
    Kind kind;
    std::string path;     // path is unused when fd >= 0 (kReadFile, kWriteFile)
    std::string dest;
    std::string data;     // bytes to write, or bytes read
    int fd = -1;
    int flags = 0;
    int mode = 0;
    bool recursive = false;
    int64_t position = -1;
    size_t length = 0;

    int status = 0;       // libuv error code, 0 on success
    const char* syscall = "";
    int64_t result = 0;
    uv_stat_t stat{};
    std::vector<std::string> names;
    std::string first_created;  // mkdir recursive

    // Async only
    uv_work_t work;
    Isolate* isolate = nullptr;
    Global<Function> callback;
};

// Run a blocking libuv fs call. Sets op->status on failure and returns its result.
template <typename Fn>
int64_t Run(FsOp* op, const char* syscall, Fn fn) {
    uv_fs_t req;
    int result = fn(&req);
    int64_t value = req.result;
    if (result < 0) {
        op->status = result;
        op->syscall = syscall;
    }
    uv_fs_req_cleanup(&req);
    return result < 0 ? result : value;
}

int64_t Open(FsOp* op) {
    return Run(op, "open", [&](uv_fs_t* req) {
        return uv_fs_open(nullptr, req, op->path.c_str(), op->flags, op->mode, nullptr);
    });
}

void Close(int fd) {
    uv_fs_t req;
    (void)uv_fs_close(nullptr, &req, fd, nullptr);  // the data was already read or written
    uv_fs_req_cleanup(&req);
}

void ReadWholeFile(FsOp* op) {
    bool opened = op->fd < 0;
    int fd = op->fd;
    if (opened) {
        int64_t result = Open(op);
        if (op->status != 0) return;
        fd = static_cast<int>(result);
    }

    // The size is only a hint: pipes and /proc files report 0
    uv_fs_t stat_req;
    size_t hint = 0;
    if (uv_fs_fstat(nullptr, &stat_req, fd, nullptr) == 0) hint = static_cast<size_t>(stat_req.statbuf.st_size);
    uv_fs_req_cleanup(&stat_req);

    std::string content;
    size_t chunk = hint > 0 ? hint + 1 : 65536;
    for (;;) {
        size_t used = content.size();
        // A Buffer could hold more, but this copy can't be allowed to fail
        if (used + chunk > frogjs::kMaxNativeCopy) {
            op->status = UV_EFBIG;
            op->syscall = "read";
            break;
        }
        content.resize(used + chunk);
        uv_buf_t buf = uv_buf_init(&content[used], static_cast<unsigned int>(std::min<size_t>(chunk, 1u << 30)));
        int64_t count = Run(op, "read", [&](uv_fs_t* req) {
            return uv_fs_read(nullptr, req, fd, &buf, 1, -1, nullptr);
        });
        if (op->status != 0) break;
        content.resize(used + static_cast<size_t>(count));
        if (count == 0) break;
        chunk = 65536;
    }
    if (opened) Close(fd);
    if (op->status == 0) op->data = std::move(content);
}

void WriteAll(FsOp* op, int fd, int64_t position) {
    size_t offset = 0;
    while (offset < op->data.size()) {
        size_t remaining = std::min<size_t>(op->data.size() - offset, 1u << 30);
        uv_buf_t buf = uv_buf_init(&op->data[offset], static_cast<unsigned int>(remaining));
        int64_t count = Run(op, "write", [&](uv_fs_t* req) {
            return uv_fs_write(nullptr, req, fd, &buf, 1, position, nullptr);
        });
        if (op->status != 0) return;
        offset += static_cast<size_t>(count);
        if (position >= 0) position += count;
    }
}

void WriteWholeFile(FsOp* op) {
    bool opened = op->fd < 0;
    int fd = op->fd;
    if (opened) {
        int64_t result = Open(op);
        if (op->status != 0) return;
        fd = static_cast<int>(result);
    }
    WriteAll(op, fd, -1);
    if (opened) Close(fd);
}

int MkdirOne(FsOp* op, const std::string& path) {
    uv_fs_t req;
    int result = uv_fs_mkdir(nullptr, &req, path.c_str(), op->mode, nullptr);
    uv_fs_req_cleanup(&req);
    return result;
}

// mkdir -p: create the missing parents, then the directory. An existing
// directory is fine; anything else in the way is an error.
void MkdirRecursive(FsOp* op) {
    std::vector<std::string> missing;
    std::string current = op->path;
    while (current.size() > 1 && current.back() == '/') current.pop_back();

    for (;;) {
        int result = MkdirOne(op, current);
        if (result == 0) {
            op->first_created = current;
            break;
        }
        if (result == UV_EEXIST) {
            uv_fs_t req;
            bool is_dir = uv_fs_stat(nullptr, &req, current.c_str(), nullptr) == 0 && S_ISDIR(req.statbuf.st_mode);
            uv_fs_req_cleanup(&req);
            if (!is_dir) {
                op->status = missing.empty() ? UV_EEXIST : UV_ENOTDIR;
                op->syscall = "mkdir";
                return;
            }
            break;
        }
        if (result != UV_ENOENT) {
            op->status = result;
            op->syscall = "mkdir";
            return;
        }
        size_t slash = current.rfind('/');
        if (slash == std::string::npos) {
            // Relative path whose first part doesn't exist: ENOENT can't come
            // from a missing parent, so report it
            op->status = result;
            op->syscall = "mkdir";
            return;
        }
        missing.push_back(current);
        current = current.substr(0, slash == 0 ? 1 : slash);
    }

    while (!missing.empty()) {
        int result = MkdirOne(op, missing.back());
        if (result < 0 && result != UV_EEXIST) {
            op->status = result;
            op->syscall = "mkdir";
            return;
        }
        if (result == 0 && op->first_created.empty()) op->first_created = missing.back();
        missing.pop_back();
    }
}

void Scandir(FsOp* op) {
    uv_fs_t req;
    int result = uv_fs_scandir(nullptr, &req, op->path.c_str(), 0, nullptr);
    if (result < 0) {
        op->status = result;
        op->syscall = "scandir";
    } else {
        uv_dirent_t entry;
        while (uv_fs_scandir_next(&req, &entry) != UV_EOF) op->names.emplace_back(entry.name);
    }
    uv_fs_req_cleanup(&req);
}

// Stat family: uv_fs_stat and friends leave the result in the request
template <typename Fn>
void RunStat(FsOp* op, const char* syscall, Fn fn) {
    uv_fs_t req;
    int result = fn(&req);
    if (result < 0) {
        op->status = result;
        op->syscall = syscall;
    } else {
        op->stat = req.statbuf;
    }
    uv_fs_req_cleanup(&req);
}

void Execute(FsOp* op) {
    const char* path = op->path.c_str();
    switch (op->kind) {
        case Kind::kReadFile: ReadWholeFile(op); break;
        case Kind::kWriteFile: WriteWholeFile(op); break;
        case Kind::kMkdir:
            if (op->recursive) {
                MkdirRecursive(op);
            } else {
                Run(op, "mkdir", [&](uv_fs_t* req) { return uv_fs_mkdir(nullptr, req, path, op->mode, nullptr); });
            }
            break;
        case Kind::kReaddir: Scandir(op); break;
        case Kind::kRmdir:
            Run(op, "rmdir", [&](uv_fs_t* req) { return uv_fs_rmdir(nullptr, req, path, nullptr); });
            break;
        case Kind::kUnlink:
            Run(op, "unlink", [&](uv_fs_t* req) { return uv_fs_unlink(nullptr, req, path, nullptr); });
            break;
        case Kind::kStat:
            RunStat(op, "stat", [&](uv_fs_t* req) { return uv_fs_stat(nullptr, req, path, nullptr); });
            break;
        case Kind::kLstat:
            RunStat(op, "lstat", [&](uv_fs_t* req) { return uv_fs_lstat(nullptr, req, path, nullptr); });
            break;
        case Kind::kFstat:
            RunStat(op, "fstat", [&](uv_fs_t* req) { return uv_fs_fstat(nullptr, req, op->fd, nullptr); });
            break;
        case Kind::kAccess:
            Run(op, "access", [&](uv_fs_t* req) { return uv_fs_access(nullptr, req, path, op->mode, nullptr); });
            break;
        case Kind::kRename:
            Run(op, "rename", [&](uv_fs_t* req) { return uv_fs_rename(nullptr, req, path, op->dest.c_str(), nullptr); });
            break;
        case Kind::kCopyFile:
            Run(op, "copyfile", [&](uv_fs_t* req) {
                return uv_fs_copyfile(nullptr, req, path, op->dest.c_str(), op->mode, nullptr);
            });
            break;
        case Kind::kOpen: op->result = Open(op); break;
        case Kind::kRealpath: {
            uv_fs_t req;
            int result = uv_fs_realpath(nullptr, &req, path, nullptr);
            if (result < 0) {
                op->status = result;
                op->syscall = "realpath";
            } else {
                op->data = static_cast<const char*>(req.ptr);
            }
            uv_fs_req_cleanup(&req);
            break;
        }
        case Kind::kClose:
            Run(op, "close", [&](uv_fs_t* req) { return uv_fs_close(nullptr, req, op->fd, nullptr); });
            break;
        case Kind::kRead: {
            op->data.resize(op->length);
            uv_buf_t buf = uv_buf_init(op->data.empty() ? nullptr : &op->data[0],
                static_cast<unsigned int>(std::min<size_t>(op->length, 1u << 30)));
            int64_t count = Run(op, "read", [&](uv_fs_t* req) {
                return uv_fs_read(nullptr, req, op->fd, &buf, 1, op->position, nullptr);
            });
            op->data.resize(op->status == 0 ? static_cast<size_t>(count) : 0);
            break;
        }
        case Kind::kWrite: {
            uv_buf_t buf = uv_buf_init(op->data.empty() ? nullptr : &op->data[0],
                static_cast<unsigned int>(std::min<size_t>(op->data.size(), 1u << 30)));
            op->result = Run(op, "write", [&](uv_fs_t* req) {
                return uv_fs_write(nullptr, req, op->fd, &buf, 1, op->position, nullptr);
            });
            break;
        }
    }
}

double TimeMs(const uv_timespec_t& time) {
    return static_cast<double>(time.tv_sec) * 1000 + static_cast<double>(time.tv_nsec) / 1000000.0;
}

// The value JavaScript gets back on success
Local<Value> Result(Isolate* isolate, Local<Context> context, FsOp* op) {
    switch (op->kind) {
        case Kind::kReadFile:
        case Kind::kRead:
            return NewBuffer(isolate, context, op->data.data(), op->data.size());
        case Kind::kMkdir:
            if (!op->first_created.empty()) {
                // The first directory created, as an absolute path
                std::string first = op->first_created;
                char cwd[4096];
                size_t size = sizeof(cwd);
                if (first[0] != '/' && uv_cwd(cwd, &size) == 0) first = std::string(cwd) + "/" + first;
                return String::NewFromUtf8(isolate, first.c_str()).ToLocalChecked();
            }
            return Undefined(isolate);
        case Kind::kReaddir: {
            std::vector<Local<Value>> names;
            names.reserve(op->names.size());
            for (const std::string& name : op->names) {
                names.push_back(String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked());
            }
            return Array::New(isolate, names.data(), names.size());
        }
        case Kind::kStat:
        case Kind::kLstat:
        case Kind::kFstat: {
            const uv_stat_t& s = op->stat;
            double values[] = {
                static_cast<double>(s.st_dev), static_cast<double>(s.st_mode), static_cast<double>(s.st_nlink),
                static_cast<double>(s.st_uid), static_cast<double>(s.st_gid), static_cast<double>(s.st_rdev),
                static_cast<double>(s.st_blksize), static_cast<double>(s.st_ino), static_cast<double>(s.st_size),
                static_cast<double>(s.st_blocks), TimeMs(s.st_atim), TimeMs(s.st_mtim), TimeMs(s.st_ctim),
                TimeMs(s.st_birthtim),
            };
            Local<Value> items[14];
            for (int i = 0; i < 14; i++) items[i] = Number::New(isolate, values[i]);
            return Array::New(isolate, items, 14);
        }
        case Kind::kOpen:
            return Integer::New(isolate, static_cast<int>(op->result));
        case Kind::kRealpath:
            return String::NewFromUtf8(isolate, op->data.c_str()).ToLocalChecked();
        case Kind::kWrite:
            return Number::New(isolate, static_cast<double>(op->result));
        default:
            return Undefined(isolate);
    }
}

// The path is only part of the error for operations that took one
Local<Value> OpError(Isolate* isolate, FsOp* op) {
    bool has_path = !op->path.empty() && strcmp(op->syscall, "read") != 0 && strcmp(op->syscall, "write") != 0;
    return frogjs::UVException(isolate, op->status, op->syscall, has_path ? op->path.c_str() : nullptr,
                               op->dest.empty() ? nullptr : op->dest.c_str());
}

void AfterWork(uv_work_t* work, int /*status*/) {
    FsOp* op = static_cast<FsOp*>(work->data);
    Isolate* isolate = op->isolate;
    {
        HandleScope handle_scope(isolate);
        Local<Context> context = isolate->GetCurrentContext();
        Local<Value> argv[2];
        argv[0] = Null(isolate);
        argv[1] = Undefined(isolate);
        if (op->status != 0) {
            argv[0] = OpError(isolate, op);
        } else {
            argv[1] = Result(isolate, context, op);
        }
        (void)frogjs::MakeCallback(isolate, context, op->callback.Get(isolate), Undefined(isolate), 2, argv);  // reports its own errors
    }
    delete op;
}

// Run op now if there is no callback (throwing on failure), else on the thread pool
void Dispatch(const FunctionCallbackInfo<Value>& args, std::unique_ptr<FsOp> op, Local<Value> callback) {
    Isolate* isolate = args.GetIsolate();
    if (callback->IsFunction()) {
        op->isolate = isolate;
        op->callback.Reset(isolate, callback.As<Function>());
        op->work.data = op.get();
        int result = uv_queue_work(frogjs::Loop(), &op->work, [](uv_work_t* work) {
            Execute(static_cast<FsOp*>(work->data));
        }, AfterWork);
        if (result < 0) {
            isolate->ThrowException(frogjs::UVException(isolate, result, "uv_queue_work"));
            return;
        }
        op.release();  // AfterWork frees it
        return;
    }

    Execute(op.get());
    if (op->status != 0) {
        isolate->ThrowException(OpError(isolate, op.get()));
        return;
    }
    args.GetReturnValue().Set(Result(isolate, isolate->GetCurrentContext(), op.get()));
}

std::string ToStdString(Isolate* isolate, Local<Value> value) {
    String::Utf8Value utf8(isolate, value);
    return *utf8 != nullptr ? std::string(*utf8, utf8.length()) : std::string();
}

int ToInt(const FunctionCallbackInfo<Value>& args, int index) {
    return args[index]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
}

double ToNumber(const FunctionCallbackInfo<Value>& args, int index) {
    return args[index]->NumberValue(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
}

std::unique_ptr<FsOp> NewOp(Kind kind) {
    auto op = std::make_unique<FsOp>();
    op->kind = kind;
    return op;
}

// readFile(pathOrFd, flags[, cb])
void ReadFile(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kReadFile);
    if (args[0]->IsNumber()) op->fd = ToInt(args, 0);
    else op->path = ToStdString(args.GetIsolate(), args[0]);
    op->flags = ToInt(args, 1);
    Dispatch(args, std::move(op), args[2]);
}

// writeFile(pathOrFd, bytes, flags, mode[, cb])
void WriteFile(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kWriteFile);
    if (args[0]->IsNumber()) op->fd = ToInt(args, 0);
    else op->path = ToStdString(args.GetIsolate(), args[0]);
    frogjs::GetBinaryData(args[1], &op->data);
    op->flags = ToInt(args, 2);
    op->mode = ToInt(args, 3);
    Dispatch(args, std::move(op), args[4]);
}

// mkdir(path, mode, recursive[, cb])
void Mkdir(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kMkdir);
    op->path = ToStdString(args.GetIsolate(), args[0]);
    op->mode = ToInt(args, 1);
    op->recursive = args[2]->IsTrue();
    Dispatch(args, std::move(op), args[3]);
}

template <Kind kind>
void PathOp(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(kind);
    op->path = ToStdString(args.GetIsolate(), args[0]);
    Dispatch(args, std::move(op), args[1]);
}

template <Kind kind>
void FdOp(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(kind);
    op->fd = ToInt(args, 0);
    Dispatch(args, std::move(op), args[1]);
}

// access(path, mode[, cb])
void Access(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kAccess);
    op->path = ToStdString(args.GetIsolate(), args[0]);
    op->mode = ToInt(args, 1);
    Dispatch(args, std::move(op), args[2]);
}

// rename(from, to[, cb])
void Rename(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kRename);
    op->path = ToStdString(args.GetIsolate(), args[0]);
    op->dest = ToStdString(args.GetIsolate(), args[1]);
    Dispatch(args, std::move(op), args[2]);
}

// copyFile(from, to, mode[, cb])
void CopyFile(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kCopyFile);
    op->path = ToStdString(args.GetIsolate(), args[0]);
    op->dest = ToStdString(args.GetIsolate(), args[1]);
    op->mode = ToInt(args, 2);
    Dispatch(args, std::move(op), args[3]);
}

// open(path, flags, mode[, cb])
void OpenFile(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kOpen);
    op->path = ToStdString(args.GetIsolate(), args[0]);
    op->flags = ToInt(args, 1);
    op->mode = ToInt(args, 2);
    Dispatch(args, std::move(op), args[3]);
}

// read(fd, length, position[, cb]) -> Buffer
void Read(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kRead);
    op->fd = ToInt(args, 0);
    op->length = static_cast<size_t>(ToNumber(args, 1));
    op->position = static_cast<int64_t>(ToNumber(args, 2));
    Dispatch(args, std::move(op), args[3]);
}

// write(fd, bytes, position[, cb]) -> bytes written
void Write(const FunctionCallbackInfo<Value>& args) {
    auto op = NewOp(Kind::kWrite);
    op->fd = ToInt(args, 0);
    frogjs::GetBinaryData(args[1], &op->data);
    op->position = static_cast<int64_t>(ToNumber(args, 2));
    Dispatch(args, std::move(op), args[3]);
}

}  // namespace

// Set up the fs module (requires SetupStream first)
void SetupFileSystem(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "readFile", ReadFile);
    frogjs::SetFunction(isolate, context, native, "writeFile", WriteFile);
    frogjs::SetFunction(isolate, context, native, "mkdir", Mkdir);
    frogjs::SetFunction(isolate, context, native, "readdir", PathOp<Kind::kReaddir>);
    frogjs::SetFunction(isolate, context, native, "rmdir", PathOp<Kind::kRmdir>);
    frogjs::SetFunction(isolate, context, native, "unlink", PathOp<Kind::kUnlink>);
    frogjs::SetFunction(isolate, context, native, "stat", PathOp<Kind::kStat>);
    frogjs::SetFunction(isolate, context, native, "lstat", PathOp<Kind::kLstat>);
    frogjs::SetFunction(isolate, context, native, "realpath", PathOp<Kind::kRealpath>);
    frogjs::SetFunction(isolate, context, native, "fstat", FdOp<Kind::kFstat>);
    frogjs::SetFunction(isolate, context, native, "close", FdOp<Kind::kClose>);
    frogjs::SetFunction(isolate, context, native, "access", Access);
    frogjs::SetFunction(isolate, context, native, "rename", Rename);
    frogjs::SetFunction(isolate, context, native, "copyFile", CopyFile);
    frogjs::SetFunction(isolate, context, native, "open", OpenFile);
    frogjs::SetFunction(isolate, context, native, "read", Read);
    frogjs::SetFunction(isolate, context, native, "write", Write);

    Local<Object> constants = Object::New(isolate);
    struct { const char* name; int value; } values[] = {
        {"F_OK", F_OK}, {"R_OK", R_OK}, {"W_OK", W_OK}, {"X_OK", X_OK},
        {"O_RDONLY", O_RDONLY}, {"O_WRONLY", O_WRONLY}, {"O_RDWR", O_RDWR}, {"O_CREAT", O_CREAT},
        {"O_TRUNC", O_TRUNC}, {"O_APPEND", O_APPEND}, {"O_EXCL", O_EXCL}, {"O_SYNC", O_SYNC},
        {"COPYFILE_EXCL", UV_FS_COPYFILE_EXCL},
    };
    for (const auto& entry : values) {
        constants->Set(context, String::NewFromUtf8(isolate, entry.name).ToLocalChecked(),
            Integer::New(isolate, entry.value)).Check();
    }
    native->Set(context, String::NewFromUtf8Literal(isolate, "constants"), constants).Check();

    Local<Object> global = context->Global();
    Local<Value> argv[2] = {native, global->Get(context, String::NewFromUtf8Literal(isolate, "stream")).ToLocalChecked()};
    Local<Value> fs;
    if (!frogjs::RunJsModule(isolate, context, "fs", kFsSource, 2, argv).ToLocal(&fs)) return;
    global->Set(context, String::NewFromUtf8Literal(isolate, "fs"), fs).Check();
}
