// Simple HTTP-like Server Example
const server = net.createServer((socket) => {
    console.log('New connection');

    socket.on('data', (request) => {
        console.log('Request:\n' + request);

        // Send HTTP-like response
        const response =
            'HTTP/1.1 200 OK\r\n' +
            'Content-Type: text/html\r\n' +
            'Content-Length: 58\r\n' +
            '\r\n' +
            '<html><body><h1>Hello from FrogJS!</h1></body></html>';

        socket.write(response);
        socket.end();
    });

    socket.on('end', () => {
        console.log('Connection closed');
    });
});

server.listen(3000, () => {
    console.log('HTTP-like server listening on port 3000');
    console.log('Open http://localhost:3000 in your browser');
});
