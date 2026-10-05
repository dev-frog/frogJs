// Test path module
console.log('=== Path Tests ===');

// Test path.join()
console.log('path.join("/foo", "bar", "baz/asdf", "quux", ".."):', path.join('/foo', 'bar', 'baz/asdf', 'quux', '..'));
console.log('path.join() result is "/foo/bar/baz/asdf":', path.join('/foo', 'bar', 'baz/asdf', 'quux', '..') === '/foo/bar/baz/asdf');

// Test path.resolve()
console.log('path.resolve("/foo/bar", "./baz"):', path.resolve('/foo/bar', './baz'));
console.log('path.resolve() with absolute is "/foo/bar/baz":', path.resolve('/foo/bar', './baz') === '/foo/bar/baz');
console.log('path.resolve("/foo/bar", "/tmp/file/") is "/tmp/file":', path.resolve('/foo/bar', '/tmp/file/') === '/tmp/file');
console.log('path.resolve() with no args is cwd:', path.resolve() === process.cwd());

// Test path.normalize()
console.log('path.normalize("/foo/bar//baz/asdf/quux/.."):', path.normalize('/foo/bar//baz/asdf/quux/..'));
console.log('path.normalize() result is "/foo/bar/baz/asdf":', path.normalize('/foo/bar//baz/asdf/quux/..') === '/foo/bar/baz/asdf');

// Test path.dirname()
console.log('path.dirname("/foo/bar/baz/asdf/quux"):', path.dirname('/foo/bar/baz/asdf/quux'));
console.log('path.dirname() result is "/foo/bar/baz/asdf":', path.dirname('/foo/bar/baz/asdf/quux') === '/foo/bar/baz/asdf');

// Test path.basename()
console.log('path.basename("/foo/bar/quux.html"):', path.basename('/foo/bar/quux.html'));
console.log('path.basename() result is "quux.html":', path.basename('/foo/bar/quux.html') === 'quux.html');
console.log('path.basename() with ext is "quux":', path.basename('/foo/bar/quux.html', '.html') === 'quux');

// Test path.extname()
console.log('path.extname("index.html"):', path.extname('index.html'));
console.log('path.extname("index.coffee.md") is ".md":', path.extname('index.coffee.md') === '.md');
console.log('path.extname(".index") is "":', path.extname('.index') === '');

// Test path.isAbsolute()
console.log('path.isAbsolute("/foo/bar") is true:', path.isAbsolute('/foo/bar') === true);
console.log('path.isAbsolute("qux/") is false:', path.isAbsolute('qux/') === false);

// Test constants
console.log('path.sep is "/":', path.sep === '/');
console.log('path.delimiter is ":":', path.delimiter === ':');

// Test require('path')
console.log('require("path") returns the path module:', require('path') === path);

// Test type validation
try {
    path.join('foo', 42);
    console.log('path.join() with a number throws: false');
} catch (e) {
    console.log('path.join() with a number throws TypeError:', e instanceof TypeError);
}

console.log('=== All Path Tests Complete ===');
