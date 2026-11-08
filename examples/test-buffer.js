// Buffer self-test: exits with code 1 if a check fails. Also runs under Node.js.
const { check, throws, section, run } = require('./lib/check');

run(() => {
  section('Allocation');
  check('alloc is zero-filled', Array.from(Buffer.alloc(3)), [0, 0, 0]);
  check('alloc with a number fill', Array.from(Buffer.alloc(3, 7)), [7, 7, 7]);
  check('alloc with a string fill', Buffer.alloc(5, 'ab').toString(), 'ababa');
  check('alloc with an encoded fill', Array.from(Buffer.alloc(2, 'ff', 'hex')), [255, 255]);
  check('allocUnsafe has the size', Buffer.allocUnsafe(4).length, 4);
  check('allocUnsafeSlow has the size', Buffer.allocUnsafeSlow(4).length, 4);
  check('empty', Buffer.alloc(0).length, 0);
  check('empty toString', Buffer.alloc(0).toString(), '');
  throws('alloc(-1)', () => Buffer.alloc(-1), { name: 'RangeError', code: 'ERR_OUT_OF_RANGE' });
  throws('alloc(NaN)', () => Buffer.alloc(NaN), { name: 'RangeError', code: 'ERR_OUT_OF_RANGE' });
  throws('alloc(1e18)', () => Buffer.alloc(1e18), { name: 'RangeError' });
  throws('alloc("1")', () => Buffer.alloc('1'), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
  throws('allocUnsafe(-1)', () => Buffer.allocUnsafe(-1), { code: 'ERR_OUT_OF_RANGE' });

  section('Uint8Array');
  const bytes = Buffer.from([1, 2, 3]);
  check('is a Uint8Array', bytes instanceof Uint8Array, true);
  check('indexable', bytes[1], 2);
  bytes[1] = 300;
  check('writes wrap', bytes[1], 44);
  check('out of range reads undefined', bytes[9], undefined);
  check('iterable', [...Buffer.from('ab')], [97, 98]);
  check('map keeps the bytes', Array.from(Buffer.from([1, 2]).map((b) => b * 2)), [2, 4]);

  section('Buffer.from');
  check('string', Buffer.from('héllo').length, 6);
  check('array wraps modulo 256', Array.from(Buffer.from([257, -1, 1.5, 'x'])), [1, 255, 1, 0]);
  check('Buffer is copied', (() => {
    const a = Buffer.from('a');
    const b = Buffer.from(a);
    b[0] = 98;
    return a.toString();
  })(), 'a');
  const shared = new ArrayBuffer(4);
  const view = Buffer.from(shared, 1, 2);
  view[0] = 9;
  check('ArrayBuffer shares memory', new Uint8Array(shared)[1], 9);
  check('ArrayBuffer offset and length', view.length, 2);
  check('Uint16Array copies values', Array.from(Buffer.from(new Uint16Array([0x1234, 1]))), [0x34, 1]);
  check('SharedArrayBuffer', Buffer.from(new SharedArrayBuffer(2)).length, 2);
  check('toJSON round trip', Buffer.from(JSON.parse(JSON.stringify(Buffer.from('hi')))).toString(), 'hi');
  check('array-like', Buffer.from({ length: 2, 0: 5, 1: 6 }).toString('hex'), '0506');
  throws('from(number)', () => Buffer.from(1), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
  throws('from(null)', () => Buffer.from(null), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
  throws('bad encoding', () => Buffer.from('a', 'nope'), { name: 'TypeError', code: 'ERR_UNKNOWN_ENCODING' });

  section('Encodings');
  const text = 'héllo wörld €😀';
  for (const encoding of ['utf8', 'utf-8', 'UTF8']) {
    check(`round trip ${encoding}`, Buffer.from(text, encoding).toString(encoding), text);
  }
  check('hex', Buffer.from('frog').toString('hex'), '66726f67');
  check('from hex', Buffer.from('66726f67', 'hex').toString(), 'frog');
  check('hex stops at bad pair', Buffer.from('66zz67', 'hex').length, 1);
  check('hex odd length', Buffer.from('666', 'hex').length, 1);
  check('base64', Buffer.from('frog!').toString('base64'), 'ZnJvZyE=');
  check('base64url', Buffer.from([251, 255]).toString('base64url'), '-_8');
  check('from base64', Buffer.from('ZnJvZyE=', 'base64').toString(), 'frog!');
  check('from base64url', Array.from(Buffer.from('-_8', 'base64url')), [251, 255]);
  check('base64 skips junk', Buffer.from('Zn\nJv Zw==', 'base64').toString(), 'frog');
  check('latin1', Buffer.from('é', 'latin1').length, 1);
  check('binary', Buffer.from([233]).toString('binary'), 'é');
  check('ascii masks the high bit', Buffer.from([233]).toString('ascii'), 'i');
  check('invalid UTF-8 becomes U+FFFD', Buffer.from([0xff, 0x41]).toString(), '�A');
  check('isEncoding yes', Buffer.isEncoding('hex'), true);
  check('isEncoding no', Buffer.isEncoding('nope'), false);
  check('isEncoding non-string', Buffer.isEncoding(1), false);

  section('toString ranges');
  const hello = Buffer.from('hello');
  check('start', hello.toString('utf8', 1), 'ello');
  check('start and end', hello.toString('utf8', 1, 3), 'el');
  check('clamped', hello.toString('utf8', -5, 99), 'hello');
  check('empty range', hello.toString('utf8', 3, 1), '');
  throws('toString with a bad encoding', () => hello.toString('nope'), { code: 'ERR_UNKNOWN_ENCODING' });
  throws('toString on another object', () => Buffer.prototype.toString.call({}));

  section('byteLength');
  check('utf8', Buffer.byteLength('€'), 3);
  check('hex', Buffer.byteLength('abcd', 'hex'), 2);
  check('base64', Buffer.byteLength('ZnJvZyE=', 'base64'), 5);
  check('latin1', Buffer.byteLength('é', 'latin1'), 1);
  check('Buffer', Buffer.byteLength(Buffer.alloc(3)), 3);
  check('ArrayBuffer', Buffer.byteLength(new ArrayBuffer(4)), 4);
  throws('number', () => Buffer.byteLength(1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('write');
  const target = Buffer.alloc(6);
  check('returns the bytes written', target.write('hi'), 2);
  check('offset', target.write('yo', 3), 2);
  check('content', target.toString('latin1'), 'hi\0yo\0');
  check('length limit', Buffer.alloc(4).write('abcd', 0, 2), 2);
  check('truncates', Buffer.alloc(2).write('abcd'), 2);
  check('never a partial character', Buffer.alloc(2).write('€'), 0);
  check('encoding as second argument', (() => {
    const b = Buffer.alloc(2);
    b.write('ffff', 'hex');
    return b.toString('hex');
  })(), 'ffff');
  check('offset and encoding', (() => {
    const b = Buffer.alloc(2);
    b.write('ff', 1, 'hex');
    return b.toString('hex');
  })(), '00ff');
  check('offset, length, encoding', (() => {
    const b = Buffer.alloc(3);
    b.write('ffff', 1, 1, 'hex');
    return b.toString('hex');
  })(), '00ff00');
  throws('offset too large', () => Buffer.alloc(2).write('a', 3), { code: 'ERR_OUT_OF_RANGE' });
  throws('negative offset', () => Buffer.alloc(2).write('a', -1), { code: 'ERR_OUT_OF_RANGE' });
  throws('non-string', () => Buffer.alloc(2).write(1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('slice and subarray');
  const source = Buffer.from('abcdef');
  check('slice', source.slice(1, 3).toString(), 'bc');
  check('negative start', source.slice(-2).toString(), 'ef');
  check('past the end', source.slice(4, 99).toString(), 'ef');
  check('start after end', source.slice(3, 1).length, 0);
  check('result is a Buffer', Buffer.isBuffer(source.subarray(1)), true);
  const window = source.slice(0, 1);
  window[0] = 122;
  check('slice shares memory', source[0], 122);

  section('concat');
  check('joins', Buffer.concat([Buffer.from('a'), Buffer.from('b')]).toString(), 'ab');
  check('accepts Uint8Array', Buffer.concat([new Uint8Array([97])]).toString(), 'a');
  check('truncates', Buffer.concat([Buffer.from('abc')], 2).toString(), 'ab');
  check('pads with zeros', Array.from(Buffer.concat([Buffer.from('a')], 2)), [97, 0]);
  check('empty list', Buffer.concat([]).length, 0);
  throws('not a list', () => Buffer.concat('a'), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('bad element', () => Buffer.concat(['a']), { code: 'ERR_INVALID_ARG_TYPE' });

  section('Comparing');
  check('isBuffer', [Buffer.isBuffer(Buffer.alloc(0)), Buffer.isBuffer(new Uint8Array(1)), Buffer.isBuffer('a')], [true, false, false]);
  check('equals', Buffer.from('a').equals(Buffer.from('a')), true);
  check('not equal', Buffer.from('a').equals(Buffer.from('b')), false);
  check('compare', [Buffer.compare(Buffer.from('a'), Buffer.from('b')), Buffer.compare(Buffer.from('b'), Buffer.from('a')),
    Buffer.compare(Buffer.from('a'), Buffer.from('ab')), Buffer.compare(Buffer.from('ab'), Buffer.from('a')),
    Buffer.compare(Buffer.from('a'), Buffer.from('a'))], [-1, 1, -1, 1, 0]);
  check('instance compare with ranges', Buffer.from('abc').compare(Buffer.from('xbcx'), 1, 3, 1, 3), 0);
  throws('equals non-buffer', () => Buffer.alloc(1).equals('a'), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('Buffer.compare first', () => Buffer.compare('a', Buffer.alloc(1)), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('Buffer.compare second', () => Buffer.compare(Buffer.alloc(1), 'a'), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('compare non-buffer', () => Buffer.alloc(1).compare('a'), { code: 'ERR_INVALID_ARG_TYPE' });

  section('copy');
  const to = Buffer.alloc(4);
  check('returns the bytes copied', Buffer.from('abcd').copy(to, 1, 1, 3), 2);
  check('copied', to.toString('latin1'), '\0bc\0');
  check('limited by the target', Buffer.from('abcd').copy(Buffer.alloc(1)), 1);
  check('target offset past the end', Buffer.from('a').copy(Buffer.alloc(1), 5), 0);
  check('empty range', Buffer.from('abc').copy(Buffer.alloc(3), 0, 2, 1), 0);
  throws('copy to a non-buffer', () => Buffer.from('a').copy('b'), { code: 'ERR_INVALID_ARG_TYPE' });

  section('fill');
  check('number', Array.from(Buffer.alloc(2).fill(258)), [2, 2]);
  check('string pattern', Buffer.alloc(3).fill('ab').toString(), 'aba');
  check('range', Array.from(Buffer.alloc(3).fill(1, 1, 2)), [0, 1, 0]);
  check('encoding as second argument', Buffer.alloc(2).fill('ff', 'hex').toString('hex'), 'ffff');
  check('offset and encoding', Buffer.alloc(2).fill('ff', 1, 'hex').toString('hex'), '00ff');
  check('buffer pattern', Buffer.alloc(3).fill(Buffer.from('xy')).toString(), 'xyx');
  check('empty string zero-fills', Array.from(Buffer.alloc(1, 5).fill('')), [0]);
  check('start past end does nothing', Array.from(Buffer.alloc(2).fill(1, 2, 2)), [0, 0]);
  check('offset past the end does nothing', Array.from(Buffer.alloc(1).fill(1, 5)), [0]);
  throws('negative offset', () => Buffer.alloc(1).fill(1, -1), { code: 'ERR_OUT_OF_RANGE' });
  throws('end past the end', () => Buffer.alloc(1).fill(1, 0, 5), { code: 'ERR_OUT_OF_RANGE' });
  throws('empty pattern', () => Buffer.alloc(1).fill(Buffer.alloc(0)), { code: 'ERR_INVALID_ARG_VALUE' });

  section('Searching');
  const haystack = Buffer.from('abcabc');
  check('indexOf string', haystack.indexOf('c'), 2);
  check('indexOf from', haystack.indexOf('a', 1), 3);
  check('indexOf negative from', haystack.indexOf('a', -3), 3);
  check('indexOf number', haystack.indexOf(98), 1);
  check('indexOf Buffer', haystack.indexOf(Buffer.from('ca')), 2);
  check('indexOf missing', haystack.indexOf('z'), -1);
  check('indexOf encoding', haystack.indexOf('63', 0, 'hex'), 2);
  check('indexOf encoding second', haystack.indexOf('63', 'hex'), 2);
  check('indexOf empty', haystack.indexOf(''), 0);
  check('lastIndexOf', haystack.lastIndexOf('a'), 3);
  check('lastIndexOf from', haystack.lastIndexOf('a', 2), 0);
  check('lastIndexOf before the start', haystack.lastIndexOf('a', -99), -1);
  check('lastIndexOf empty', haystack.lastIndexOf(''), 6);
  check('includes', [haystack.includes('bc'), haystack.includes('zz')], [true, false]);
  throws('indexOf object', () => haystack.indexOf({}), { code: 'ERR_INVALID_ARG_TYPE' });

  section('Numbers');
  const n = Buffer.alloc(8);
  check('writeUInt8', n.writeUInt8(255, 0), 1);
  check('readUInt8', n.readUInt8(0), 255);
  check('readInt8', n.readInt8(0), -1);
  n.writeUInt16BE(0x1234, 0);
  check('writeUInt16BE', [n[0], n[1]], [0x12, 0x34]);
  check('readUInt16LE', n.readUInt16LE(0), 0x3412);
  n.writeInt32LE(-2, 0);
  check('readInt32LE', n.readInt32LE(0), -2);
  n.writeUInt32BE(0xdeadbeef, 0);
  check('readUInt32BE', n.readUInt32BE(0), 0xdeadbeef);
  n.writeFloatLE(1.5, 0);
  check('float', n.readFloatLE(0), 1.5);
  n.writeDoubleBE(Math.PI, 0);
  check('double', n.readDoubleBE(0), Math.PI);
  n.writeBigInt64LE(-5n, 0);
  check('bigint', n.readBigInt64LE(0), -5n);
  n.writeBigUInt64BE(5n, 0);
  check('big unsigned', n.readBigUInt64BE(0), 5n);
  check('Uint spelling', Buffer.from([1]).readUint8(0), 1);
  check('default offset', Buffer.from([7]).readUInt8(), 7);
  throws('read out of range', () => Buffer.alloc(1).readUInt8(1), { name: 'RangeError', code: 'ERR_OUT_OF_RANGE' });
  throws('read too small', () => Buffer.alloc(1).readUInt32LE(0), { code: 'ERR_BUFFER_OUT_OF_BOUNDS' });
  throws('read fractional offset', () => Buffer.alloc(2).readUInt8(0.5), { code: 'ERR_OUT_OF_RANGE' });
  throws('read string offset', () => Buffer.alloc(2).readUInt8('0'), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('write out of range value', () => Buffer.alloc(1).writeUInt8(256, 0), { code: 'ERR_OUT_OF_RANGE' });
  throws('write out of range offset', () => Buffer.alloc(1).writeUInt8(1, 1), { code: 'ERR_OUT_OF_RANGE' });
  check('write coerces to a number', Buffer.alloc(1).writeUInt8('1', 0), 1);
  throws('bigint write of a number', () => Buffer.alloc(8).writeBigInt64LE(1, 0), { name: 'TypeError' });

  section('Misc');
  check('toJSON', Buffer.from('a').toJSON(), { type: 'Buffer', data: [97] });
  check('toLocaleString', Buffer.from('a').toLocaleString(), 'a');
  check('deprecated constructor: size', new Buffer(2).length, 2);
  check('deprecated constructor: string', new Buffer('hi').toString(), 'hi');
  check('deprecated constructor: ArrayBuffer', new Buffer(new ArrayBuffer(3)).length, 3);
});
