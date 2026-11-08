declare module 'http' {
    import EventEmitter = require('events');
    import { Readable } from 'stream';
    import { Socket, AddressInfo } from 'net';

    /** Lower-cased header names; repeated headers are joined with ', ' (set-cookie is always an array). */
    interface IncomingHttpHeaders {
        [name: string]: string | string[] | undefined;
        'set-cookie'?: string[];
    }

    type OutgoingHttpHeader = number | string | readonly string[];

    interface OutgoingHttpHeaders {
        [name: string]: OutgoingHttpHeader | undefined;
    }

    interface ServerOptions {
        /** Largest request head accepted, in bytes (default 16384). */
        maxHeaderSize?: number;
    }

    interface ListenOptions {
        port?: number;
        host?: string;
    }

    type RequestListener = (req: IncomingMessage, res: ServerResponse) => void;

    class Server extends EventEmitter {
        constructor(requestListener?: RequestListener);
        constructor(options: ServerOptions, requestListener?: RequestListener);

        readonly listening: boolean;
        /** Idle time before a keep-alive connection is closed (default 5000 ms). */
        keepAliveTimeout: number;
        /** Time allowed to receive request headers (default 60000 ms). */
        headersTimeout: number;
        /** Time to receive a whole request after its headers (default 300000 ms, 0: none). */
        requestTimeout: number;
        maxHeaderSize: number;

        /** Port 0 (the default) picks a free port; host defaults to `'0.0.0.0'`. Failures are 'error' events. */
        listen(port?: number, callback?: () => void): this;
        listen(port: number, host: string, callback?: () => void): this;
        listen(options: ListenOptions, callback?: () => void): this;
        /** Stop accepting connections; 'close' fires once every connection has closed. */
        close(callback?: (err?: Error) => void): this;
        /** null when not listening. */
        address(): AddressInfo | null;
        closeIdleConnections(): void;
        closeAllConnections(): void;

        on(event: 'request' | 'checkContinue', listener: RequestListener): this;
        on(event: 'connection', listener: (socket: Socket) => void): this;
        on(event: 'clientError', listener: (err: Error, socket: Socket) => void): this;
        on(event: 'listening' | 'close', listener: () => void): this;
        on(event: 'error', listener: (err: Error & { code?: string }) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
        once(event: 'request' | 'checkContinue', listener: RequestListener): this;
        once(event: 'listening' | 'close', listener: () => void): this;
        once(event: 'error', listener: (err: Error & { code?: string }) => void): this;
        once(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    /** A server's request, or the response a client receives. Body chunks are strings. */
    class IncomingMessage extends Readable {
        /** Request only. */
        method?: string;
        /** Request only: path and query string. */
        url?: string;
        /** Response only. */
        statusCode?: number;
        /** Response only. */
        statusMessage?: string;
        headers: IncomingHttpHeaders;
        /** `[name, value, name, value, ...]` as received. */
        rawHeaders: string[];
        /** Trailers of a chunked message, after 'end'. */
        trailers: Record<string, string>;
        /** e.g. `'1.1'` */
        httpVersion: string;
        socket: Socket;
        /** The whole message has been received. */
        complete: boolean;
        aborted: boolean;

        on(event: 'data', listener: (chunk: string) => void): this;
        on(event: 'end' | 'close' | 'aborted', listener: () => void): this;
        on(event: 'error', listener: (err: Error) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    /** Base of ServerResponse and ClientRequest. */
    class OutgoingMessage extends EventEmitter {
        readonly headersSent: boolean;

        /** Throws on invalid names or values. An array sends the header more than once. */
        setHeader(name: string, value: OutgoingHttpHeader): this;
        getHeader(name: string): OutgoingHttpHeader | undefined;
        getHeaders(): OutgoingHttpHeaders;
        getHeaderNames(): string[];
        hasHeader(name: string): boolean;
        removeHeader(name: string): void;
        /** Send part of the body. Always returns true (TCP sockets don't report backpressure yet). */
        write(chunk: string | Buffer, callback?: (err?: Error) => void): boolean;
        write(chunk: string | Buffer, encoding: BufferEncoding, callback?: (err?: Error) => void): boolean;
        end(callback?: () => void): this;
        end(chunk: string | Buffer, callback?: () => void): this;
        end(chunk: string | Buffer, encoding: BufferEncoding, callback?: () => void): this;
    }

    class ServerResponse extends OutgoingMessage {
        /** Default 200. */
        statusCode: number;
        statusMessage: string;
        readonly finished: boolean;
        /** Add a Date header (default true). */
        sendDate: boolean;

        writeHead(statusCode: number, headers?: OutgoingHttpHeaders): this;
        writeHead(statusCode: number, statusMessage: string, headers?: OutgoingHttpHeaders): this;
        /** Send `100 Continue`. */
        writeContinue(): void;
        /** Close the connection without finishing the response. */
        destroy(error?: Error): this;

        on(event: 'finish' | 'close', listener: () => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    class ClientRequest extends OutgoingMessage {
        /** Emit 'timeout' after `ms` without socket activity (the request is not aborted). */
        setTimeout(ms: number, callback?: () => void): this;
        destroy(error?: Error): this;
        abort(): void;

        on(event: 'response', listener: (res: IncomingMessage) => void): this;
        on(event: 'information', listener: (info: { statusCode: number; statusMessage: string; headers: IncomingHttpHeaders }) => void): this;
        /** `err.code` is e.g. `'ECONNREFUSED'`, `'EAI_NONAME'` or `'ECONNRESET'`. */
        on(event: 'error', listener: (err: Error & { code?: string }) => void): this;
        on(event: 'timeout' | 'finish' | 'close', listener: () => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
        once(event: 'response', listener: (res: IncomingMessage) => void): this;
        once(event: 'error', listener: (err: Error & { code?: string }) => void): this;
        once(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    interface RequestOptions {
        /** Default `'localhost'`. */
        hostname?: string;
        host?: string;
        /** Default 80. */
        port?: number;
        /** Path and query string, default `'/'`. */
        path?: string;
        /** Default `'GET'`. */
        method?: string;
        headers?: OutgoingHttpHeaders;
        /** Idle socket timeout in ms (emits 'timeout'). */
        timeout?: number;
    }

    function createServer(requestListener?: RequestListener): Server;
    function createServer(options: ServerOptions, requestListener?: RequestListener): Server;

    /** Only `http://` URLs (no HTTPS). Nothing is sent until write()/end(). */
    function request(url: string, callback?: (res: IncomingMessage) => void): ClientRequest;
    function request(url: string, options: RequestOptions, callback?: (res: IncomingMessage) => void): ClientRequest;
    function request(options: RequestOptions, callback?: (res: IncomingMessage) => void): ClientRequest;

    /** Like request() with method GET, and calls end() for you. */
    function get(url: string, callback?: (res: IncomingMessage) => void): ClientRequest;
    function get(url: string, options: RequestOptions, callback?: (res: IncomingMessage) => void): ClientRequest;
    function get(options: RequestOptions, callback?: (res: IncomingMessage) => void): ClientRequest;

    const METHODS: string[];
    const STATUS_CODES: { [code: number]: string | undefined };
    /** Default request head limit, in bytes (16384). */
    const maxHeaderSize: number;
}
