// path self-test: exits with code 1 if a check fails. Also runs under Node.js.
const path = require('path');
const { check, throws, section, run } = require('./lib/check');

const cwd = process.cwd();

run(() => {
  section('join');
  check('segments', path.join('/foo', 'bar', 'baz/asdf', 'quux', '..'), '/foo/bar/baz/asdf');
  check('nothing', path.join(), '.');
  check('empty strings', path.join('', ''), '.');
  check('relative', path.join('a', '../..', 'b'), '../b');
  check('skips empty', path.join('a', '', 'b'), 'a/b');
  check('keeps a trailing slash', path.join('a', 'b/'), 'a/b/');
  throws('non-string', () => path.join('a', 1), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });

  section('resolve');
  check('relative', path.resolve('/foo/bar', './baz'), '/foo/bar/baz');
  check('absolute wins', path.resolve('/foo/bar', '/tmp/file/'), '/tmp/file');
  check('no arguments', path.resolve(), cwd);
  check('from the current directory', path.resolve('a'), cwd + '/a');
  check('above the root', path.resolve('/../..'), '/');
  check('empty segments', path.resolve('', '/a', ''), '/a');
  throws('non-string', () => path.resolve(null), { code: 'ERR_INVALID_ARG_TYPE' });

  section('normalize');
  check('dots and slashes', path.normalize('/foo/bar//baz/asdf/quux/..'), '/foo/bar/baz/asdf');
  check('empty', path.normalize(''), '.');
  check('root', path.normalize('/'), '/');
  check('above root', path.normalize('/../a'), '/a');
  check('relative above', path.normalize('../a/../..'), '../..');
  check('trailing slash', path.normalize('a/b/'), 'a/b/');
  check('only dots', path.normalize('./'), './');
  check('dot', path.normalize('.'), '.');
  check('collapses to nothing', path.normalize('a/..'), '.');
  check('collapses with a slash', path.normalize('a/../'), './');
  throws('non-string', () => path.normalize(1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('dirname');
  for (const [input, expected] of [['/foo/bar/baz', '/foo/bar'], ['/foo/bar/', '/foo'], ['/foo', '/'], ['/', '/'], ['', '.'], ['a', '.'], ['a/b', 'a'], ['//a', '//'], ['a//b', 'a/'], ['///', '/']]) {
    check(JSON.stringify(input), path.dirname(input), expected);
  }
  throws('non-string', () => path.dirname({}), { code: 'ERR_INVALID_ARG_TYPE' });

  section('basename');
  for (const [args, expected] of [[['/a/b.html'], 'b.html'], [['/a/b.html', '.html'], 'b'], [['/a/b/'], 'b'], [['/'], ''], [[''], ''], [['b.html', 'html'], 'b.'], [['b', 'b'], ''], [['a/', 'a'], 'a'], [['/a', '/a'], ''], [['ab', 'b'], 'a'], [['a/b///'], 'b'], [['a.js', ''], 'a.js'], [['///a'], 'a'], [['/', 'a'], '/'], [['/', '/'], '']]) {
    check(JSON.stringify(args), path.basename(...args), expected);
  }
  throws('non-string', () => path.basename(1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('non-string suffix', () => path.basename('a', 1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('function', () => path.basename(() => {}), { code: 'ERR_INVALID_ARG_TYPE' });

  section('extname');
  for (const [input, expected] of [['a.html', '.html'], ['a.b.md', '.md'], ['a.', '.'], ['a', ''], ['.bashrc', ''], ['..', ''], ['.', ''], ['', ''], ['a/.b', ''], ['a.b/', '.b'], ['/a.b/c', ''], ['a/..', '']]) {
    check(JSON.stringify(input), path.extname(input), expected);
  }
  throws('non-string', () => path.extname(undefined), { code: 'ERR_INVALID_ARG_TYPE' });

  section('isAbsolute');
  check('values', [path.isAbsolute('/a'), path.isAbsolute('a'), path.isAbsolute(''), path.isAbsolute('./a')], [true, false, false, false]);
  throws('non-string', () => path.isAbsolute(1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('relative');
  check('sibling', path.relative('/a/b/c', '/a/b/d'), '../d');
  check('child', path.relative('/a', '/a/b'), 'b');
  check('parent', path.relative('/a/b', '/a'), '..');
  check('same', path.relative('/a', '/a'), '');
  check('same after resolving', path.relative('/a/', '/a'), '');
  check('from the root', path.relative('/', '/a'), 'a');
  throws('non-string', () => path.relative(1, 'a'), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('non-string to', () => path.relative('a', 1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('parse and format');
  check('full', path.parse('/home/u/file.txt'), { root: '/', dir: '/home/u', base: 'file.txt', ext: '.txt', name: 'file' });
  check('relative', path.parse('a/b'), { root: '', dir: 'a', base: 'b', ext: '', name: 'b' });
  check('root', path.parse('/'), { root: '/', dir: '/', base: '', ext: '', name: '' });
  check('in the root', path.parse('/a'), { root: '/', dir: '/', base: 'a', ext: '', name: 'a' });
  check('empty', path.parse(''), { root: '', dir: '', base: '', ext: '', name: '' });
  check('no directory', path.parse('a.b'), { root: '', dir: '', base: 'a.b', ext: '.b', name: 'a' });
  throws('parse non-string', () => path.parse(1), { code: 'ERR_INVALID_ARG_TYPE' });
  check('format dir and base', path.format({ dir: '/a', base: 'b.c' }), '/a/b.c');
  check('format root', path.format({ root: '/', base: 'b' }), '/b');
  check('format name and ext', path.format({ name: 'b', ext: '.c' }), 'b.c');
  check('format ext without a dot', path.format({ name: 'b', ext: 'c' }), 'b.c');
  check('format nothing', path.format({}), '');
  throws('format non-object', () => path.format(1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('format null', () => path.format(null), { code: 'ERR_INVALID_ARG_TYPE' });

  section('Constants');
  check('sep', path.sep, '/');
  check('delimiter', path.delimiter, ':');
  check('posix', path.posix.join('a', 'b'), 'a/b');
  check('toNamespacedPath', path.toNamespacedPath('a'), 'a');
});
