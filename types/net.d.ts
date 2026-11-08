// FrogJS sockets are Duplex streams and servers are EventEmitters, as in Node.js.

declare module 'net' {
    import { Duplex } from 'stream';
    import EventEmitter = require('events');

    interface AddressInfo {
        address: string;
        family: string;
        port: number;
    }

    /** A failed connection: `code` is e.g. `'ECONNREFUSED'` or `'ENOTFOUND'`. */
    interface NetError extends Error {
        code: string;
        errno?: number;
        syscall: string;
        address?: string;
        port?: number;
        hostname?: string;
    }

    interface SocketOptions {
        /** Keep the writable side open after the peer has ended its side. */
        allowHalfOpen?: boolean;
        highWaterMark?: number;
    }

    interface ConnectOptions extends SocketOptions {
        port: number;
        /** Default `'localhost'`; hostnames are resolved, trying each address. */
        host?: string;
        /** Idle timeout in ms (see `socket.setTimeout()`). */
        timeout?: number;
    }

    class Socket extends Duplex {
        constructor(options?: SocketOptions);

        readonly connecting: boolean;
        readonly pending: boolean;
        readonly readyState: 'opening' | 'open' | 'readOnly' | 'writeOnly' | 'closed';
        readonly remoteAddress?: string;
        readonly remotePort?: number;
        readonly remoteFamily?: string;
        readonly localAddress?: string;
        readonly localPort?: number;
        readonly localFamily?: string;
        readonly bytesRead: number;
        readonly bytesWritten: number;
        readonly timeout: number;

        connect(options: ConnectOptions, connectListener?: () => void): this;
        connect(port: number, host?: string, connectListener?: () => void): this;
        connect(port: number, connectListener: () => void): this;
        /** `{}` when not connected. */
        address(): AddressInfo | Record<string, never>;
        /** Emit `'timeout'` after `ms` of inactivity (0 turns it off). The socket stays open. */
        setTimeout(ms: number, callback?: () => void): this;
        setNoDelay(enable?: boolean): this;
        setKeepAlive(enable?: boolean, initialDelay?: number): this;
        /** End after pending writes, then destroy. */
        destroySoon(): void;
        ref(): this;
        unref(): this;

        on(event: 'connect' | 'ready' | 'timeout' | 'end' | 'finish' | 'drain', listener: () => void): this;
        on(event: 'data', listener: (data: any) => void): this;
        on(event: 'close', listener: (hadError: boolean) => void): this;
        on(event: 'error', listener: (err: NetError) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    interface ServerOptions {
        /** Keep a connection writable after the peer has ended its side (call socket.end() when done). */
        allowHalfOpen?: boolean;
    }

    interface ListenOptions {
        port?: number;
        /** Default `'0.0.0.0'` (Node.js: `'::'`). */
        host?: string;
        backlog?: number;
    }

    class Server extends EventEmitter {
        constructor(connectionListener?: (socket: Socket) => void);
        constructor(options: ServerOptions, connectionListener?: (socket: Socket) => void);

        /** Over this many connections, new ones are closed and `'drop'` is emitted. */
        maxConnections: number | undefined;
        readonly listening: boolean;

        /** Start listening (port 0 picks a free port). Failures are `'error'` events. */
        listen(port?: number | string, callback?: () => void): this;
        listen(port: number | string, host: string, callback?: () => void): this;
        listen(port: number | string, backlog: number, callback?: () => void): this;
        listen(port: number | string, host: string, backlog: number, callback?: () => void): this;
        listen(options: ListenOptions, callback?: () => void): this;
        /** Stop accepting connections. `'close'` follows when the existing ones have ended. */
        close(callback?: (err?: Error) => void): this;
        /** null when not listening. */
        address(): AddressInfo;
        getConnections(callback: (err: Error | null, count: number) => void): this;
        ref(): this;
        unref(): this;

        on(event: 'connection', listener: (socket: Socket) => void): this;
        on(event: 'listening' | 'close', listener: () => void): this;
        on(event: 'error', listener: (err: NetError) => void): this;
        on(event: 'drop', listener: (data: object) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    function createServer(connectionListener?: (socket: Socket) => void): Server;
    function createServer(options: ServerOptions, connectionListener?: (socket: Socket) => void): Server;

    function connect(options: ConnectOptions, connectListener?: () => void): Socket;
    function connect(port: number, host?: string, connectListener?: () => void): Socket;
    function connect(port: number, connectListener: () => void): Socket;
    const createConnection: typeof connect;

    /** 4, 6, or 0 if `input` isn't an IP address. */
    function isIP(input: unknown): 0 | 4 | 6;
    function isIPv4(input: unknown): boolean;
    function isIPv6(input: unknown): boolean;
}
