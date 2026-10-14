// HTTP module self-test: starts a server on a free port, exercises it with the
// client, and exits with code 1 if any check fails.
const http = require('http');

let passed = 0;
let failed = 0;

function check(name, condition, detail) {
    if (condition) {
        passed++;
        console.log(`  ✓ ${name}`);
    } else {
        failed++;
        console.error(`  ✗ ${name}${detail !== undefined ? ' - got: ' + JSON.stringify(detail) : ''}`);
    }
}

// Make a request and collect the whole response
function fetch(port, options, body) {
    return new Promise((resolve, reject) => {
        const req = http.request(Object.assign({ port, hostname: '127.0.0.1' }, options), (res) => {
            let data = '';
            res.on('data', (chunk) => data += chunk);
            res.on('end', () => resolve({ status: res.statusCode, message: res.statusMessage, headers: res.headers, body: data }));
        });
        req.on('error', reject);
        if (Array.isArray(body)) {
            body.forEach((chunk) => req.write(chunk));
            req.end();
        } else {
            req.end(body);
        }
    });
}

const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (chunk) => body += chunk);
    req.on('end', () => {
        switch (req.url) {
            case '/echo':
                res.setHeader('Content-Type', 'application/json');
                res.end(JSON.stringify({ method: req.method, headers: req.headers, body }));
                break;
            case '/chunked':
                res.write('part1,');
                res.write('pärt2,');
                res.end('end');
                break;
            case '/status':
                res.writeHead(201, 'Made It', { 'X-Custom': 'yes', 'Set-Cookie': ['a=1', 'b=2'] });
                res.end();
                break;
            case '/empty':
                res.statusCode = 204;
                res.end();
                break;
            case '/throw':
                throw new Error('handler failure (expected by test)');
            case '/buffer':
                res.end(Buffer.from('from buffer'));
                break;
            default:
                res.statusCode = 404;
                res.end('not found');
        }
    });
});

async function run(port) {
    console.log('Server');
    let r = await fetch(port, { path: '/echo', method: 'POST', headers: { 'X-Test': 'abc' } }, 'héllo wörld');
    let json = JSON.parse(r.body);
    check('POST body round-trips UTF-8', json.body === 'héllo wörld', json.body);
    check('request method', json.method === 'POST');
    check('request headers are lower-cased', json.headers['x-test'] === 'abc');
    check('Content-Length counts bytes', json.headers['content-length'] === '13', json.headers['content-length']);
    check('response Content-Type', r.headers['content-type'] === 'application/json');

    r = await fetch(port, { path: '/echo', method: 'PUT' }, ['chunk-a ', 'chunk-b']);
    json = JSON.parse(r.body);
    check('chunked request body', json.body === 'chunk-a chunk-b', json.body);
    check('chunked request header', json.headers['transfer-encoding'] === 'chunked');

    r = await fetch(port, { path: '/chunked' });
    check('chunked response body', r.body === 'part1,pärt2,end', r.body);
    check('chunked response header', r.headers['transfer-encoding'] === 'chunked');

    r = await fetch(port, { path: '/status' });
    check('writeHead status and message', r.status === 201 && r.message === 'Made It', [r.status, r.message]);
    check('custom header', r.headers['x-custom'] === 'yes');
    check('multiple Set-Cookie headers', JSON.stringify(r.headers['set-cookie']) === '["a=1","b=2"]', r.headers['set-cookie']);

    r = await fetch(port, { path: '/empty' });
    check('204 has no body', r.status === 204 && r.body === '');

    r = await fetch(port, { path: '/echo', method: 'HEAD' });
    check('HEAD has no body', r.status === 200 && r.body === '');

    r = await fetch(port, { path: '/buffer' });
    check('Buffer response body', r.body === 'from buffer', r.body);

    r = await fetch(port, { path: '/missing' });
    check('404 route', r.status === 404 && r.body === 'not found');

    r = await fetch(port, { path: '/throw' });
    check('throwing handler gives 500', r.status === 500, r.status);

    console.log('Client');
    r = await new Promise((resolve) => {
        http.get(`http://localhost:${port}/echo`, (res) => {
            let data = '';
            res.on('data', (chunk) => data += chunk);
            res.on('end', () => resolve({ status: res.statusCode, body: data }));
        });
    });
    check('http.get with URL string', r.status === 200 && JSON.parse(r.body).headers.host === `localhost:${port}`, r);

    const err = await fetch(1, { path: '/' }).then(() => null, (e) => e);
    check('connection refused emits error', err !== null && err.code === 'ECONNREFUSED', err && err.code);

    let threw = null;
    try {
        http.request({ port, headers: { 'X-Bad': 'a\r\nInjected: 1' } });
    } catch (e) {
        threw = e;
    }
    check('header injection is rejected', threw !== null && threw.code === 'ERR_INVALID_CHAR');

    check('STATUS_CODES', http.STATUS_CODES[404] === 'Not Found');
    check('METHODS', http.METHODS.includes('PATCH'));
}

server.listen(0, '127.0.0.1', () => {
    const port = server.address().port;
    run(port)
        .catch((err) => {
            failed++;
            console.error('Unexpected error:', err && err.stack || err);
        })
        .then(() => {
            server.close(() => {
                console.log(`\n${passed} passed, ${failed} failed`);
                if (failed > 0) process.exit(1);
            });
        });
});
