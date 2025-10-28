// TCP Server Example
const server = net.createServer((socket) => {
    console.log('Client connected');

    socket.on('data', (data) => {
        console.log('Received:', data);
        socket.write('Echo: ' + data);
    });

    socket.on('end', () => {
        console.log('Client disconnected');
    });
});

server.listen(8080, () => {
    console.log('Server listening on port 8080');
    console.log('Run tcp-client.js in another terminal to test');
});
