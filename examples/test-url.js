// URL, URLSearchParams, url and querystring self-test: exits with code 1 if a
// check fails. Also runs under Node.js, which is how the expected values were checked.
const url = require('url');
const qs = require('querystring');
const util = require('util');
const { spawnSync } = require('child_process');
const { check, throws, section, run } = require('./lib/check');

const href = (input, base) => new URL(input, base).href;

run(() => {
  section('Parsing');
  check('lowercases scheme and host', href('HTTP://EXAMPLE.com/A'), 'http://example.com/A');
  check('default port dropped', href('https://a.com:443/'), 'https://a.com/');
  check('dot segments', href('http://a/b/./c/../d/%2e%2E/e'), 'http://a/b/e');
  check('relative to a base', href('../x?y#z', 'http://a/b/c/d'), 'http://a/b/x?y#z');
  check('scheme-relative', href('//other/p', 'https://a/b'), 'https://other/p');
  check('query only', href('?q', 'http://a/b?old#h'), 'http://a/b?q');
  check('backslashes in special URLs', href('http:\\\\a\\b\\c'), 'http://a/b/c');
  check('tabs and newlines removed', href(' http://a\n.com/\tp '), 'http://a.com/p');
  check('path percent-encoding', href('http://a/b c^{d}`"<>'), 'http://a/b%20c%5E%7Bd%7D%60%22%3C%3E');
  check('query percent-encoding', href("http://a/?a b'c"), 'http://a/?a%20b%27c');
  check('non-special query keeps quote', href("x://a/?'"), "x://a/?'");
  check('fragment percent-encoding', href('http://a/#a b`'), 'http://a/#a%20b%60');
  check('userinfo', href('http://us er:p@ss@h/'), 'http://us%20er:p%40ss@h/');
  check('IPv4 shorthand and hex', [href('http://0x7f.1/'), href('http://2130706433/'), href('http://0300.0250.1.1/')],
    ['http://127.0.0.1/', 'http://127.0.0.1/', 'http://192.168.1.1/']);
  check('IPv6', [href('http://[0:0:0:0:0:0:0:1]:8080/'), href('http://[2001:db8::1:0:0:1]/'), href('http://[::ffff:192.168.0.1]/')],
    ['http://[::1]:8080/', 'http://[2001:db8::1:0:0:1]/', 'http://[::ffff:c0a8:1]/']);
  check('IDNA', [href('http://münchen.de/'), href('http://ＥＸＡＭＰＬＥ.com/'), href('http://xn--mnchen-3ya.de/')],
    ['http://xn--mnchen-3ya.de/', 'http://example.com/', 'http://xn--mnchen-3ya.de/']);
  check('file URLs', [href('file:///C|/x'), href('file://localhost/etc'), href('/x', 'file:///a/b')],
    ['file:///C:/x', 'file:///etc', 'file:///x']);
  check('opaque paths', [href('mailto:a@b.com'), href('sc:a b ?x'), href('data:text/plain,hi there')],
    ['mailto:a@b.com', 'sc:a b%20?x', 'data:text/plain,hi there']);
  check('non-special authority', href('foo://user@host:99/p'), 'foo://user@host:99/p');
  check('blob origin', new URL('blob:https://a.com:8/uuid').origin, 'https://a.com:8');
  check('opaque origin', [new URL('foo://a/').origin, new URL('file:///x').origin], ['null', 'null']);

  for (const bad of ['', 'http://', 'http://a b/', 'http://[::1/', 'http://1.2.3.4.5/', 'http://256.0.0.1/', 'https://exa%mple/',
                     'http://a:99999/', 'http://xn--a.com/', 'http://\u{FDD0}x/', 'http://%EF%BF%BD/', 'relative']) {
    throws(`rejects ${JSON.stringify(bad)}`, () => new URL(bad), { name: 'TypeError', code: 'ERR_INVALID_URL', message: 'Invalid URL', input: bad });
  }
  throws('error has the base', () => new URL('x', 'bad base'), { code: 'ERR_INVALID_URL', base: 'bad base' });
  throws('no arguments', () => new URL(), { code: 'ERR_MISSING_ARGS', message: 'The "url" argument must be specified' });
  check('URL.canParse', [URL.canParse('x'), URL.canParse('x', 'http://a'), URL.canParse('http://a')], [false, true, true]);
  check('URL.parse', [URL.parse('nope'), URL.parse('/p', 'http://a').href], [null, 'http://a/p']);

  section('Components');
  const u = new URL('https://user:pass@sub.example.com:8080/p/a/t/h?query=string#hash');
  check('getters', [u.protocol, u.username, u.password, u.host, u.hostname, u.port, u.pathname, u.search, u.hash, u.origin],
    ['https:', 'user', 'pass', 'sub.example.com:8080', 'sub.example.com', '8080', '/p/a/t/h', '?query=string', '#hash',
     'https://sub.example.com:8080']);
  check('toString and toJSON', [String(u), JSON.stringify({ u })], [u.href, `{"u":"${u.href}"}`]);
  const s = new URL('http://h/p');
  s.protocol = 'https';
  s.username = 'a b';
  s.password = 'c@d';
  s.hostname = 'other.org';
  s.port = '81';
  s.pathname = '/x y/../z';
  s.search = 'q=1';
  s.hash = 'frag';
  check('setters', s.href, 'https://a%20b:c%40d@other.org:81/z?q=1#frag');
  s.port = '443';
  check('default port setter', s.port, '');
  s.protocol = 'foo';
  check('protocol cannot leave special', s.protocol, 'https:');
  s.host = 'example.com:invalid';
  check('host setter keeps the valid part', s.host, 'example.com');
  s.search = '';
  s.hash = '';
  check('clearing search and hash', s.href, 'https://a%20b:c%40d@example.com/z');
  throws('href setter validates', () => { s.href = 'bad'; }, { code: 'ERR_INVALID_URL' });
  const opaque = new URL('sc:x ?q');
  check('a space before the query is encoded', opaque.href, 'sc:x%20?q');
  opaque.search = '';
  check('clearing the query of an opaque path', opaque.href, 'sc:x%20');

  section('URLSearchParams');
  const p = new URLSearchParams('?a=1&b=%20x+y&a=3&c&=e&%E2%82%AC=%');
  check('parse', [...p], [['a', '1'], ['b', ' x y'], ['a', '3'], ['c', ''], ['', 'e'], ['€', '%']]);
  check('get/getAll/has', [p.get('a'), p.get('zz'), p.getAll('a'), p.has('c'), p.has('a', '3'), p.has('a', '2')],
    ['1', null, ['1', '3'], true, true, false]);
  check('size', p.size, 6);
  p.set('a', 'x');
  p.delete('c');
  p.append('d', '~*\'()!');
  check('set/delete/append', p.toString(), 'a=x&b=+x+y&=e&%E2%82%AC=%25&d=%7E*%27%28%29%21');
  p.delete('b', 'no');
  check('delete with a value', p.has('b'), true);
  const sorted = new URLSearchParams('z=1&a=2&z=0&b=3');
  sorted.sort();
  check('sort is stable', sorted.toString(), 'a=2&b=3&z=1&z=0');
  check('from a record', new URLSearchParams({ 'a b': 'é&', n: 1 }).toString(), 'a+b=%C3%A9%26&n=1');
  check('from pairs', new URLSearchParams([['x', 1], new Set(['y', 2])]).toString(), 'x=1&y=2');
  check('from a URLSearchParams', new URLSearchParams(p).toString(), p.toString());
  throws('pairs must have two items', () => new URLSearchParams([['a']]), { code: 'ERR_INVALID_TUPLE' });
  throws('append needs a value', () => p.append('x'), { code: 'ERR_MISSING_ARGS' });
  const seen = [];
  new URLSearchParams('a=1&b=2').forEach((value, key) => seen.push(key + value));
  check('forEach', seen, ['a1', 'b2']);
  check('keys and values', [[...new URLSearchParams('a=1&b=2').keys()], [...new URLSearchParams('a=1&b=2').values()]], [['a', 'b'], ['1', '2']]);
  const linked = new URL('http://h/?x=1');
  linked.searchParams.append('y', 'a b');
  check('updates the URL', linked.href, 'http://h/?x=1&y=a+b');
  linked.searchParams.delete('x');
  linked.searchParams.delete('y');
  check('empty params clear the query', linked.href, 'http://h/');
  linked.search = '?k=v';
  check('URL updates the params', linked.searchParams.get('k'), 'v');
  check('same object each time', linked.searchParams, linked.searchParams);

  section('inspect');
  check('URL', util.inspect(new URL('http://a/?x=1')), `URL {
  href: 'http://a/?x=1',
  origin: 'http://a',
  protocol: 'http:',
  username: '',
  password: '',
  host: 'a',
  hostname: 'a',
  port: '',
  pathname: '/',
  search: '?x=1',
  searchParams: URLSearchParams { 'x' => '1' },
  hash: ''
}`);
  check('URLSearchParams', [util.inspect(new URLSearchParams('a=1&b=2')), util.inspect(new URLSearchParams())],
    ["URLSearchParams { 'a' => '1', 'b' => '2' }", 'URLSearchParams {}']);
  check('toStringTag', [Object.prototype.toString.call(u), Object.prototype.toString.call(p)], ['[object URL]', '[object URLSearchParams]']);
  check('no own enumerable keys', [Object.keys(u), Object.keys(p)], [[], []]);

  section('url module');
  check('globals are the module classes', [url.URL === URL, url.URLSearchParams === URLSearchParams], [true, true]);
  check('fileURLToPath', [url.fileURLToPath('file:///a%20b/c%23d'), url.fileURLToPath(new URL('file://localhost/x'))], ['/a b/c#d', '/x']);
  throws('fileURLToPath: scheme', () => url.fileURLToPath('http://x'), { code: 'ERR_INVALID_URL_SCHEME' });
  throws('fileURLToPath: host', () => url.fileURLToPath('file://host/x'), { code: 'ERR_INVALID_FILE_URL_HOST' });
  throws('fileURLToPath: encoded slash', () => url.fileURLToPath('file:///a%2Fb'), { code: 'ERR_INVALID_FILE_URL_PATH' });
  throws('fileURLToPath: type', () => url.fileURLToPath(5), { code: 'ERR_INVALID_ARG_TYPE' });
  check('pathToFileURL', [url.pathToFileURL('/tmp/a b#?%.js').href, url.pathToFileURL('/d/').href], ['file:///tmp/a%20b%23%3F%25.js', 'file:///d/']);
  check('pathToFileURL resolves', url.pathToFileURL('rel').href, 'file://' + process.cwd().split('/').map(encodeURIComponent).join('/') + '/rel');
  check('round trip', url.fileURLToPath(url.pathToFileURL('/x/ü %/y')), '/x/ü %/y');
  check('urlToHttpOptions', { ...url.urlToHttpOptions(new URL('http://a:b@[::1]:9/p?q#h')) },
    { protocol: 'http:', hostname: '::1', hash: '#h', search: '?q', pathname: '/p', path: '/p?q', href: 'http://a:b@[::1]:9/p?q#h', port: 9, auth: 'a:b' });
  check('domainToASCII', [url.domainToASCII('español.com'), url.domainToASCII('xn--iñvalid.com')], ['xn--espaol-zwa.com', '']);
  check('domainToUnicode', url.domainToUnicode('xn--espaol-zwa.com'), 'español.com');
  check('format(URL, options)', url.format(new URL('http://u:p@xn--mnchen-3ya.de/a?b#c'), { unicode: true, auth: false, fragment: false, search: false }),
    'http://münchen.de/a');

  section('Legacy url API');
  const legacy = url.parse('http://user:pass@Host.COM:8080/p/a?x=1&x=2#h', true);
  check('parse', [legacy.protocol, legacy.slashes, legacy.auth, legacy.host, legacy.port, legacy.hostname, legacy.hash,
                  legacy.search, legacy.pathname, legacy.path, legacy.href],
    ['http:', true, 'user:pass', 'host.com:8080', '8080', 'host.com', '#h', '?x=1&x=2', '/p/a', '/p/a?x=1&x=2',
     'http://user:pass@host.com:8080/p/a?x=1&x=2#h']);
  check('parse with a query object', { ...legacy.query }, { x: ['1', '2'] });
  check('parse a path', [url.parse('/foo?a=b').pathname, url.parse('/foo?a=b').query, url.parse('/foo').search], ['/foo', 'a=b', null]);
  check('slashes denote host', url.parse('//host/p', false, true).host, 'host');
  check('parse is a Url', url.parse('x') instanceof url.Url, true);
  check('resolve', [url.resolve('/one/two/three', 'four'), url.resolve('http://example.com/', '/one'),
                    url.resolve('http://a/b/c/d;p?q', '../../g'), url.resolve('http://a/b/c/d;p?q', '//g'),
                    url.resolve('http://a/b', 'https://c/d')],
    ['/one/two/four', 'http://example.com/one', 'http://a/g', 'http://g/', 'https://c/d']);
  check('format an object', url.format({ protocol: 'https', hostname: 'example.com', pathname: '/p', query: { page: 1, a: ['x', 'y'] } }),
    'https://example.com/p?page=1&a=x&a=y');

  section('querystring');
  check('parse', { ...qs.parse('a=1&b=2&a=3&c&d=%E2%82%AC&e=%ZZ+x') }, { a: ['1', '3'], b: '2', c: '', d: '€', e: '%ZZ x' });
  check('parse returns a null-prototype object', Object.getPrototypeOf(qs.parse('a=1')), null);
  check('custom separators', { ...qs.parse('a:1;b:2', ';', ':') }, { a: '1', b: '2' });
  check('maxKeys', Object.keys(qs.parse(Array.from({ length: 1005 }, (_, i) => `k${i}=1`).join('&'))).length, 1000);
  check('stringify', qs.stringify({ a: [1, 2], b: 'x y', c: true, d: null, e: {}, f: 1n, 'é': '€' }),
    'a=1&a=2&b=x%20y&c=true&d=&e=&f=1&%C3%A9=%E2%82%AC');
  check('stringify with separators', qs.stringify({ w: 'x', y: 'z' }, ';', ':'), 'w:x;y:z');
  check('escape and unescape', [qs.escape('a b&c'), qs.unescape('%E2%82%ACx%')], ['a%20b%26c', '€x%']);
  check('decode and encode are aliases', [qs.decode, qs.encode], [qs.parse, qs.stringify]);

  section('International domain names');
  // [domainToASCII, domainToUnicode, the hostname of http://<domain>/]
  const kDomains = ['münchen.de', 'MÜNCHEN.DE', 'xn--mnchen-3ya.de', 'example.com', 'bücher.example', 'ﬁ.com', 'a­b.com',
    'σ.com', 'Σ.com', '日本語.jp', 'xn--wgv71a119e.jp', 'faß.de', 'xn--a.com', 'xn--abc-.com', 'a。b．c｡d', '́a.com',
    'xn--ls8h.la', '💩.la', 'a​b.com', 'xn--zca.com', 'xn--1ch.com', 'é.xn--x', 'xn--', 'ab--c.com'];
  const kDomainsScript = `
    const url = require('url');
    const domains = ${JSON.stringify(kDomains)};
    console.log(JSON.stringify(domains.map((d) => [url.domainToASCII(d), url.domainToUnicode(d),
      (() => { try { return new URL('http://' + d + '/').hostname; } catch (e) { return e.code; } })()])));`;
  const kDomainsExpected = [["xn--mnchen-3ya.de","münchen.de","xn--mnchen-3ya.de"],["xn--mnchen-3ya.de","münchen.de","xn--mnchen-3ya.de"],["xn--mnchen-3ya.de","münchen.de","xn--mnchen-3ya.de"],["example.com","example.com","example.com"],["xn--bcher-kva.example","bücher.example","xn--bcher-kva.example"],["fi.com","fi.com","fi.com"],["ab.com","ab.com","ab.com"],["xn--4xa.com","σ.com","xn--4xa.com"],["xn--4xa.com","σ.com","xn--4xa.com"],["xn--wgv71a119e.jp","日本語.jp","xn--wgv71a119e.jp"],["xn--wgv71a119e.jp","日本語.jp","xn--wgv71a119e.jp"],["xn--fa-hia.de","faß.de","xn--fa-hia.de"],["","","ERR_INVALID_URL"],["","","ERR_INVALID_URL"],["a.b.c.d","a.b.c.d","a.b.c.d"],["","","ERR_INVALID_URL"],["xn--ls8h.la","💩.la","xn--ls8h.la"],["xn--ls8h.la","💩.la","xn--ls8h.la"],["ab.com","ab.com","ab.com"],["xn--zca.com","ß.com","xn--zca.com"],["xn--1ch.com","≠.com","xn--1ch.com"],["","","ERR_INVALID_URL"],["","","ERR_INVALID_URL"],["ab--c.com","ab--c.com","ab--c.com"]];
  const inProcess = JSON.parse(spawnSync(process.execPath, ['-e', kDomainsScript], { encoding: 'utf8' }).stdout);
  check('domainToASCII, domainToUnicode and hosts', inProcess, kDomainsExpected);
  if (process.versions.frogjs) {
    // Without ICU's IDNA, FrogJS uses its own Punycode and an approximation of
    // UTS #46: it must agree on these
    const fallback = spawnSync(process.execPath, ['-e', kDomainsScript], { encoding: 'utf8', env: { ...process.env, FROGJS_NO_ICU_IDNA: '1' } });
    check('the same without ICU', JSON.parse(fallback.stdout), kDomainsExpected);
  }
});
