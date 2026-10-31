// Crypto module self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked.
const crypto = require('crypto');
const { PassThrough } = require('stream');

let passed = 0;
let failed = 0;

function check(name, actual, expected) {
    if (actual === expected) {
        passed++;
        console.log(`  ✓ ${name}`);
    } else {
        failed++;
        console.error(`  ✗ ${name}\n      expected: ${JSON.stringify(expected)}\n      got:      ${JSON.stringify(actual)}`);
    }
}

// "Code: message" of the error fn throws, or "no error"
function thrown(fn) {
    try {
        fn();
        return 'no error';
    } catch (err) {
        return `${err.name} ${err.code}: ${err.message}`;
    }
}


async function run() {
    console.log('Buffer encodings');
    check('toString hex', Buffer.from('hello').toString('hex'), '68656c6c6f');
    check('toString base64', Buffer.from('hello!?').toString('base64'), 'aGVsbG8hPw==');
    check('toString base64url', Buffer.from([255, 250, 251]).toString('base64url'), '__r7');
    check('toString with start/end', Buffer.from('hello').toString('utf8', 1, 3), 'el');
    check('from hex', Buffer.from('68656c6C6f', 'hex').toString(), 'hello');
    check('from hex stops at invalid pair', Buffer.from('6869zz41', 'hex').toString(), 'hi');
    check('from base64 ignores whitespace', Buffer.from('aGVs\nbG8=', 'base64').toString(), 'hello');
    check('from base64url', Buffer.from('__r7', 'base64url').toString('hex'), 'fffafb');
    check('latin1 round trip', Buffer.from('café', 'latin1').toString('latin1'), 'café');
    check('ascii strips the high bit', Buffer.from([0xc8]).toString('ascii').charCodeAt(0), 0x48);
    check('from Uint8Array', Buffer.from(new Uint8Array([1, 2, 255])).toString('hex'), '0102ff');
    check('byteLength with encoding', Buffer.byteLength('aGVsbG8=', 'base64'), 5);
    check('unknown encoding', thrown(() => Buffer.from('a', 'nope')), 'TypeError ERR_UNKNOWN_ENCODING: Unknown encoding: nope');

    console.log('createHash');
    const abc = {
        md5: '900150983cd24fb0d6963f7d28e17f72',
        sha1: 'a9993e364706816aba3e25717850c26c9cd0d89d',
        sha224: '23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7',
        sha256: 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
        sha384: 'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7',
        sha512: 'ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f',
    };
    for (const [name, digest] of Object.entries(abc)) {
        check(`${name}("abc")`, crypto.createHash(name).update('abc').digest('hex'), digest);
    }
    check('empty input', crypto.createHash('sha256').digest('hex'),
        'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
    check('two-block message', crypto.createHash('sha256')
        .update('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq').digest('hex'),
        '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
    const million = crypto.createHash('sha1');
    for (let i = 0; i < 1000; i++) million.update('a'.repeat(1000));
    check('one million "a" in 1000 updates', million.digest('hex'), '34aa973cd4c4daa4f61eeb2bdbad27316534016f');
    check('update with encoding', crypto.createHash('md5').update('616263', 'hex').digest('hex'), abc.md5);
    check('Buffer and TypedArray input', crypto.createHash('md5').update(Buffer.from('a')).update(new Uint8Array([98, 99])).digest('hex'), abc.md5);
    check('UTF-8 input', crypto.createHash('sha256').update('héllo wörld 🐸').digest('hex'),
        crypto.createHash('sha256').update(Buffer.from('héllo wörld 🐸')).digest('hex'));
    check('digest() returns a Buffer', Buffer.isBuffer(crypto.createHash('md5').digest()), true);
    check('digest base64', crypto.createHash('md5').update('abc').digest('base64'), 'kAFQmDzST7DWlj99KOF/cg==');
    check('algorithm names are case-insensitive', crypto.createHash('SHA-256').update('abc').digest('hex'), abc.sha256);
    const partial = crypto.createHash('sha256').update('ab');
    const copy = partial.copy();
    check('copy() keeps the state', copy.update('c').digest('hex'), abc.sha256);
    check('original is unaffected by the copy', partial.update('x').digest('hex'),
        crypto.createHash('sha256').update('abx').digest('hex'));
    check('getHashes', ['md5', 'sha1', 'sha256', 'sha512'].every((h) => crypto.getHashes().includes(h)), true);

    console.log('createHash errors');
    check('unsupported algorithm', thrown(() => crypto.createHash('nope')), 'Error undefined: Digest method not supported');
    const done = crypto.createHash('md5');
    done.digest();
    check('digest twice', thrown(() => done.digest()), 'Error ERR_CRYPTO_HASH_FINALIZED: Digest already called');
    check('update after digest', thrown(() => done.update('x')), 'Error ERR_CRYPTO_HASH_FINALIZED: Digest already called');
    check('invalid data', thrown(() => crypto.createHash('md5').update(5)),
        'TypeError ERR_INVALID_ARG_TYPE: The "data" argument must be of type string or an instance of Buffer, TypedArray, or DataView. Received type number (5)');

    console.log('Hash as a stream');
    const hashStream = crypto.createHash('sha256');
    hashStream.write('a');
    hashStream.end('bc');
    check('write() then read()', hashStream.read().toString('hex'), abc.sha256);
    const piped = await new Promise((resolve) => {
        const source = new PassThrough();
        const hash = crypto.createHash('md5');
        hash.on('data', (digest) => resolve(digest.toString('hex')));
        source.pipe(hash);
        source.end('abc');
    });
    check('pipe() into a hash', piped, abc.md5);

    console.log('createHmac');
    // RFC 4231 test case 2
    check('HMAC-SHA256 (RFC 4231)', crypto.createHmac('sha256', 'Jefe').update('what do ya want for nothing?').digest('hex'),
        '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843');
    check('HMAC-SHA512 (RFC 4231)', crypto.createHmac('sha512', 'Jefe').update('what do ya want for nothing?').digest('hex'),
        '164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737');
    // RFC 2202 test case 2
    check('HMAC-MD5 (RFC 2202)', crypto.createHmac('md5', 'Jefe').update('what do ya want for nothing?').digest('hex'),
        '750c783e6ab0b503eaa86e310a5db738');
    check('key longer than a block', crypto.createHmac('sha256', 'k'.repeat(200)).update('data').digest('hex'),
        crypto.createHmac('sha256', crypto.createHash('sha256').update('k'.repeat(200)).digest()).update('data').digest('hex'));
    check('Buffer key', crypto.createHmac('sha1', Buffer.from('Jefe')).update('what do ya want for nothing?').digest('hex'),
        'effcdf6ae5eb2fa2d27416d5f184df9c259a7c79');
    const hmac = crypto.createHmac('md5', 'k');
    hmac.digest();
    check('digest twice returns empty', hmac.digest('hex'), '');
    check('update after digest', thrown(() => hmac.update('x')), 'Error ERR_CRYPTO_HASH_FINALIZED: Digest already called');
    check('invalid digest', thrown(() => crypto.createHmac('nope', 'k')), 'TypeError ERR_CRYPTO_INVALID_DIGEST: Invalid digest: nope');
    check('missing key', thrown(() => crypto.createHmac('md5')),
        'TypeError ERR_INVALID_ARG_TYPE: The "key" argument must be of type string or an instance of ArrayBuffer, Buffer, TypedArray, DataView, KeyObject, or CryptoKey. Received undefined');

    console.log('pbkdf2');
    // RFC 6070 test vectors
    check('PBKDF2-SHA1, 1 iteration', crypto.pbkdf2Sync('password', 'salt', 1, 20, 'sha1').toString('hex'),
        '0c60c80f961f0e71f3a9b524af6012062fe037a6');
    check('PBKDF2-SHA1, 4096 iterations', crypto.pbkdf2Sync('password', 'salt', 4096, 20, 'sha1').toString('hex'),
        '4b007901b765489abead49d926f721d065a429c1');
    check('PBKDF2-SHA1, long password and salt', crypto.pbkdf2Sync('passwordPASSWORDpassword',
        'saltSALTsaltSALTsaltSALTsaltSALTsalt', 4096, 25, 'sha1').toString('hex'),
        '3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038');
    check('PBKDF2-SHA256, output longer than the hash', crypto.pbkdf2Sync('pw', 'salt', 2, 40, 'sha256').toString('hex'),
        '061df616343cb988307172e2c3074be63e953b28237ea96184f6a48bec7a8a55f2e6c4867cac34f3');
    const asyncKey = await new Promise((resolve, reject) => {
        crypto.pbkdf2('password', 'salt', 4096, 20, 'sha1', (err, key) => (err ? reject(err) : resolve(key)));
    });
    check('async pbkdf2', asyncKey.toString('hex'), '4b007901b765489abead49d926f721d065a429c1');
    check('iterations 0', thrown(() => crypto.pbkdf2Sync('p', 's', 0, 10, 'sha1')),
        'RangeError ERR_OUT_OF_RANGE: The value of "iterations" is out of range. It must be >= 1 && <= 2147483647. Received 0');
    check('negative keylen', thrown(() => crypto.pbkdf2Sync('p', 's', 1, -1, 'sha1')),
        'RangeError ERR_OUT_OF_RANGE: The value of "keylen" is out of range. It must be >= 0 && <= 2147483647. Received -1');
    check('invalid digest', thrown(() => crypto.pbkdf2Sync('p', 's', 1, 10, 'nope')), 'TypeError ERR_CRYPTO_INVALID_DIGEST: Invalid digest: nope');
    check('missing digest', thrown(() => crypto.pbkdf2Sync('p', 's', 1, 10)),
        'TypeError ERR_INVALID_ARG_TYPE: The "digest" argument must be of type string. Received undefined');
    check('missing callback', thrown(() => crypto.pbkdf2('p', 's', 1, 10, 'sha1')),
        'TypeError ERR_INVALID_ARG_TYPE: The "callback" argument must be of type function. Received undefined');

    console.log('Random values');
    const random = crypto.randomBytes(32);
    check('randomBytes returns a Buffer of the size', Buffer.isBuffer(random) && random.length === 32, true);
    check('randomBytes differ', crypto.randomBytes(16).toString('hex') !== crypto.randomBytes(16).toString('hex'), true);
    check('randomBytes(0)', crypto.randomBytes(0).length, 0);
    const asyncRandom = await new Promise((resolve, reject) => {
        crypto.randomBytes(12, (err, buf) => (err ? reject(err) : resolve(buf)));
    });
    check('async randomBytes', asyncRandom.length, 12);
    check('negative size', thrown(() => crypto.randomBytes(-1)),
        'RangeError ERR_OUT_OF_RANGE: The value of "size" is out of range. It must be >= 0 && <= 2147483647. Received -1');
    check('string size', thrown(() => crypto.randomBytes('a')),
        "TypeError ERR_INVALID_ARG_TYPE: The \"size\" argument must be of type number. Received type string ('a')");

    const uuid = crypto.randomUUID();
    check('randomUUID is a version 4 UUID', /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(uuid), true);
    check('randomUUIDs differ', crypto.randomUUID() !== crypto.randomUUID(), true);

    const values = crypto.getRandomValues(new Uint32Array(8));
    check('getRandomValues fills in place', values instanceof Uint32Array && values.some((v) => v !== 0), true);
    check('getRandomValues rejects float arrays', thrown(() => crypto.getRandomValues(new Float32Array(2))),
        'TypeMismatchError 17: The data argument must be an integer-type TypedArray');
    check('getRandomValues limit', thrown(() => crypto.getRandomValues(new Uint8Array(65537))),
        'QuotaExceededError 22: The requested length exceeds 65,536 bytes');

    let inRange = true;
    const seen = new Set();
    for (let i = 0; i < 2000; i++) {
        const n = crypto.randomInt(10, 15);
        if (!Number.isInteger(n) || n < 10 || n >= 15) inRange = false;
        seen.add(n);
    }
    check('randomInt stays in [min, max)', inRange, true);
    check('randomInt covers the range', seen.size, 5);
    check('randomInt(max)', crypto.randomInt(1), 0);
    check('randomInt max <= min', thrown(() => crypto.randomInt(5, 5)),
        'RangeError ERR_OUT_OF_RANGE: The value of "max" is out of range. It must be greater than the value of "min" (5). Received 5');
    check('randomInt non-integer', thrown(() => crypto.randomInt(1.5)),
        'TypeError ERR_INVALID_ARG_TYPE: The "max" argument must be a safe integer. Received type number (1.5)');

    console.log('timingSafeEqual');
    check('equal', crypto.timingSafeEqual(Buffer.from('secret'), Buffer.from('secret')), true);
    check('different', crypto.timingSafeEqual(Buffer.from('secret'), Buffer.from('secreT')), false);
    check('TypedArray inputs', crypto.timingSafeEqual(new Uint8Array([1, 2]), Buffer.from([1, 2])), true);
    check('different lengths', thrown(() => crypto.timingSafeEqual(Buffer.from('a'), Buffer.from('ab'))),
        'RangeError ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH: Input buffers must have the same byte length');
    check('string input', thrown(() => crypto.timingSafeEqual('a', 'a')),
        'TypeError ERR_INVALID_ARG_TYPE: The "buf1" argument must be an instance of ArrayBuffer, Buffer, TypedArray, or DataView.');

    console.log(`\n${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
}

run().catch((err) => {
    console.error(err);
    process.exit(1);
});

