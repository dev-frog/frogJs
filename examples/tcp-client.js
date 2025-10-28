// TCP Client Example
const client = net.connect(8080, '127.0.0.1');

client.on('connect', () => {
    console.log('Connected to server');
    client.write('Hello from client!');
});

client.on('data', (data) => {
    console.log('Server response:', data);

    // Send another message after 1 second
    setTimeout(() => {
        client.write('Second message');
    }, 1000);

    // Close connection after 2 seconds
    setTimeout(() => {
        console.log('Closing connection');
        client.end();
    }, 2000);
});

client.on('end', () => {
    console.log('Disconnected from server');
});

client.on('error', (error) => {
    console.error('Connection error:', error);
});
