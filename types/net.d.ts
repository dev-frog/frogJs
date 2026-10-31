// FrogJS sockets are not streams or EventEmitters: they have on() for their
// events, and write()/end()/destroy().

declare module 'net' {
    interface AddressInfo {
        address: string;
        family: string;
        port: number;
    }

    interface ServerOptions {
        /** Keep a connection writable after the peer has ended its side (call socket.end() when done). */
        allowHalfOpen?: boolean;
    }

    interface Socket {
        readonly remoteAddress?: string;
        readonly remotePort?: number;

        /** Send a string (UTF-8, or one byte per character with `'latin1'`/`'binary'`) or a Buffer. */
        write(data: string | Buffer, encoding?: 'utf8' | 'latin1' | 'binary'): boolean;
        /** Write `data` (optional), flush pending writes, then close the socket. */
        end(data?: string | Buffer): void;
        /** Close the socket at once, discarding pending writes. */
        destroy(): void;
        /** How received data is decoded: `'utf8'` (default) or `'latin1'`/`'binary'` (byte strings). */
        setEncoding(encoding: 'utf8' | 'latin1' | 'binary'): void;

        on(event: 'data', listener: (data: string) => void): this;
        on(event: 'connect' | 'end' | 'close', listener: () => void): this;
        /** `err.code` is e.g. `'ECONNREFUSED'` or `'EAI_NONAME'`. */
        on(event: 'error', listener: (err: Error & { code?: string }) => void): this;
    }

    interface Server {
        /** Start listening (port 0 picks a free port; host defaults to `'0.0.0.0'`). Throws on EADDRINUSE. */
        listen(port: number, callback?: () => void): this;
        listen(port: number, host: string, callback?: () => void): this;
        /** Stop accepting connections. */
        close(callback?: () => void): void;
        address(): AddressInfo;
    }

    function createServer(connectionListener?: (socket: Socket) => void): Server;
    function createServer(options: ServerOptions, connectionListener?: (socket: Socket) => void): Server;

    /** Connect to `host` (default `'127.0.0.1'`; hostnames are resolved). */
    function connect(port: number, host?: string, connectListener?: () => void): Socket;
    function connect(port: number, connectListener: () => void): Socket;
}
