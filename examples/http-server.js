// HTTP Server Example with basic routing
const http = require('http');

const routes = {
    'GET /': (req, res) => {
        res.setHeader('Content-Type', 'text/html');
        res.end('<h1>Hello from FrogJS! 🐸</h1>');
    },
    'GET /json': (req, res) => {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ runtime: 'FrogJS', version: process.version }));
    },
    'POST /echo': (req, res) => {
        let body = '';
        req.on('data', (chunk) => body += chunk);
        req.on('end', () => {
            res.setHeader('Content-Type', 'text/plain');
            res.end(`You sent: ${body}`);
        });
    },
    'GET /stream': (req, res) => {
        // No Content-Length: the response is sent with chunked encoding
        res.setHeader('Content-Type', 'text/plain');
        let count = 0;
        const timer = setInterval(() => {
            res.write(`tick ${++count}\n`);
            if (count === 5) {
                clearInterval(timer);
                res.end('done\n');
            }
        }, 200);
    },
};

const server = http.createServer((req, res) => {
    console.log(`${req.method} ${req.url}`);
    const path = req.url.split('?')[0];
    const handler = routes[`${req.method} ${path}`];
    if (handler) {
        handler(req, res);
    } else {
        res.statusCode = 404;
        res.end('Not Found\n');
    }
});

server.listen(3000, () => {
    console.log('HTTP server listening on http://localhost:3000');
    console.log('Try: curl http://localhost:3000/json');
    console.log('     curl -d "hi" http://localhost:3000/echo');
    console.log('     curl http://localhost:3000/stream');
});
