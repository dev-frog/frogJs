// HTTP Client Example
// Run examples/http-server.js first, or pass a URL: frogjs examples/http-client.js http://example.com/
const http = require('http');

const url = process.argv[2] || 'http://localhost:3000/json';

// Simple GET
http.get(url, (res) => {
    console.log(`GET ${url} -> ${res.statusCode} ${res.statusMessage}`);
    console.log('Content-Type:', res.headers['content-type']);

    let body = '';
    res.on('data', (chunk) => body += chunk);
    res.on('end', () => {
        console.log('Body:', body.length > 200 ? body.slice(0, 200) + '...' : body);
        postExample();
    });
}).on('error', (err) => {
    console.error('Request failed:', err.message);
});

// POST with a body and custom headers
function postExample() {
    if (process.argv[2]) return;

    const data = JSON.stringify({ hello: 'world' });
    const req = http.request({
        hostname: 'localhost',
        port: 3000,
        path: '/echo',
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
    }, (res) => {
        let body = '';
        res.on('data', (chunk) => body += chunk);
        res.on('end', () => console.log(`POST /echo -> ${res.statusCode}: ${body}`));
    });

    req.on('error', (err) => console.error('Request failed:', err.message));
    req.end(data);
}
