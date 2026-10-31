declare module 'stream' {
    import EventEmitter = require('events');

    type Callback = (error?: Error | null) => void;

    // `require('stream')` is the Stream class, with the other classes as its
    // properties (the same shape as Node.js's declarations)
    class internal extends EventEmitter {
        constructor();
    }

    namespace internal {
        class Stream extends internal {}

        interface StreamOptions {
            highWaterMark?: number;
            objectMode?: boolean;
            /** Emit 'close' after the stream is destroyed (default true). */
            emitClose?: boolean;
            /** Destroy the stream once it has ended / finished (default true). */
            autoDestroy?: boolean;
        }

        interface ReadableOptions extends StreamOptions {
            /** Decode chunks to strings: `'utf8'`, `'latin1'`/`'binary'`, `'ascii'` or `'hex'`. */
            encoding?: BufferEncoding;
            read?(this: Readable, size: number): void;
            destroy?(this: Readable, error: Error | null, callback: Callback): void;
        }

        interface WritableOptions extends StreamOptions {
            defaultEncoding?: BufferEncoding;
            write?(this: Writable, chunk: any, encoding: BufferEncoding, callback: Callback): void;
            writev?(this: Writable, chunks: Array<{ chunk: any; encoding: BufferEncoding }>, callback: Callback): void;
            final?(this: Writable, callback: Callback): void;
            destroy?(this: Writable, error: Error | null, callback: Callback): void;
        }

        interface DuplexOptions extends StreamOptions {
            encoding?: BufferEncoding;
            defaultEncoding?: BufferEncoding;
            /** Keep the writable side open after the readable side ends (default true). */
            allowHalfOpen?: boolean;
            readable?: boolean;
            writable?: boolean;
            readableObjectMode?: boolean;
            writableObjectMode?: boolean;
            read?(this: Duplex, size: number): void;
            write?(this: Duplex, chunk: any, encoding: BufferEncoding, callback: Callback): void;
            writev?(this: Duplex, chunks: Array<{ chunk: any; encoding: BufferEncoding }>, callback: Callback): void;
            final?(this: Duplex, callback: Callback): void;
            destroy?(this: Duplex, error: Error | null, callback: Callback): void;
        }

        interface TransformOptions extends DuplexOptions {
            transform?(this: Transform, chunk: any, encoding: BufferEncoding, callback: TransformCallback): void;
            flush?(this: Transform, callback: TransformCallback): void;
        }

        type TransformCallback = (error?: Error | null, data?: any) => void;

        class Readable extends internal implements AsyncIterable<any> {
            constructor(options?: ReadableOptions);
            /** A Readable that produces the values of an iterable or async iterable. */
            static from(iterable: Iterable<any> | AsyncIterable<any>, options?: ReadableOptions): Readable;

            readable: boolean;
            readonly readableEncoding: BufferEncoding | null;
            readonly readableEnded: boolean;
            /** null: no consumer yet; true: flowing; false: paused. */
            readonly readableFlowing: boolean | null;
            readonly readableHighWaterMark: number;
            readonly readableLength: number;
            readonly readableObjectMode: boolean;
            readonly readableAborted: boolean;
            readonly readableDidRead: boolean;
            readonly destroyed: boolean;
            readonly closed: boolean;
            readonly errored: Error | null;

            _read(size: number): void;
            _destroy(error: Error | null, callback: Callback): void;
            read(size?: number): any;
            setEncoding(encoding: BufferEncoding): this;
            pause(): this;
            resume(): this;
            isPaused(): boolean;
            pipe<T extends Writable>(destination: T, options?: { end?: boolean }): T;
            unpipe(destination?: Writable): this;
            unshift(chunk: any, encoding?: BufferEncoding): void;
            /** Add a chunk (null ends the stream). False: stop pushing until _read() is called. */
            push(chunk: any, encoding?: BufferEncoding): boolean;
            destroy(error?: Error): this;
            [Symbol.asyncIterator](): AsyncIterableIterator<any>;

            on(event: 'data', listener: (chunk: any) => void): this;
            on(event: 'end' | 'close' | 'readable' | 'pause' | 'resume', listener: () => void): this;
            on(event: 'error', listener: (err: Error) => void): this;
            on(event: string | symbol, listener: (...args: any[]) => void): this;
            once(event: 'data', listener: (chunk: any) => void): this;
            once(event: 'end' | 'close' | 'readable' | 'pause' | 'resume', listener: () => void): this;
            once(event: 'error', listener: (err: Error) => void): this;
            once(event: string | symbol, listener: (...args: any[]) => void): this;
        }

        class Writable extends internal {
            constructor(options?: WritableOptions);

            writable: boolean;
            readonly writableEnded: boolean;
            readonly writableFinished: boolean;
            readonly writableHighWaterMark: number;
            readonly writableLength: number;
            readonly writableObjectMode: boolean;
            readonly writableCorked: number;
            readonly writableNeedDrain: boolean;
            readonly writableAborted: boolean;
            readonly destroyed: boolean;
            readonly closed: boolean;
            readonly errored: Error | null;

            _write(chunk: any, encoding: BufferEncoding, callback: Callback): void;
            _writev?(chunks: Array<{ chunk: any; encoding: BufferEncoding }>, callback: Callback): void;
            _final?(callback: Callback): void;
            _destroy(error: Error | null, callback: Callback): void;
            /** False: the buffer is full, wait for 'drain'. */
            write(chunk: any, callback?: Callback): boolean;
            write(chunk: any, encoding: BufferEncoding, callback?: Callback): boolean;
            end(callback?: () => void): this;
            end(chunk: any, callback?: () => void): this;
            end(chunk: any, encoding: BufferEncoding, callback?: () => void): this;
            setDefaultEncoding(encoding: BufferEncoding): this;
            cork(): void;
            uncork(): void;
            destroy(error?: Error): this;

            on(event: 'drain' | 'finish' | 'close', listener: () => void): this;
            on(event: 'pipe' | 'unpipe', listener: (source: Readable) => void): this;
            on(event: 'error', listener: (err: Error) => void): this;
            on(event: string | symbol, listener: (...args: any[]) => void): this;
            once(event: 'drain' | 'finish' | 'close', listener: () => void): this;
            once(event: 'pipe' | 'unpipe', listener: (source: Readable) => void): this;
            once(event: 'error', listener: (err: Error) => void): this;
            once(event: string | symbol, listener: (...args: any[]) => void): this;
        }

        /** Readable and Writable at once. */
        class Duplex extends Readable implements Writable {
            constructor(options?: DuplexOptions);

            writable: boolean;
            readonly writableEnded: boolean;
            readonly writableFinished: boolean;
            readonly writableHighWaterMark: number;
            readonly writableLength: number;
            readonly writableObjectMode: boolean;
            readonly writableCorked: number;
            readonly writableNeedDrain: boolean;
            readonly writableAborted: boolean;
            allowHalfOpen: boolean;

            _write(chunk: any, encoding: BufferEncoding, callback: Callback): void;
            _writev?(chunks: Array<{ chunk: any; encoding: BufferEncoding }>, callback: Callback): void;
            _final?(callback: Callback): void;
            write(chunk: any, callback?: Callback): boolean;
            write(chunk: any, encoding: BufferEncoding, callback?: Callback): boolean;
            end(callback?: () => void): this;
            end(chunk: any, callback?: () => void): this;
            end(chunk: any, encoding: BufferEncoding, callback?: () => void): this;
            setDefaultEncoding(encoding: BufferEncoding): this;
            cork(): void;
            uncork(): void;
        }

        /** A Duplex whose output is computed from its input. */
        class Transform extends Duplex {
            constructor(options?: TransformOptions);
            _transform(chunk: any, encoding: BufferEncoding, callback: TransformCallback): void;
            _flush?(callback: TransformCallback): void;
        }

        /** A Transform that passes its input through unchanged. */
        class PassThrough extends Transform {}

        type PipelineStream = Readable | Writable | Iterable<any> | AsyncIterable<any>;

        /** Pipe streams into each other; destroy them all on error. Returns the last stream. */
        function pipeline<T extends Writable>(source: PipelineStream, ...rest: [...PipelineStream[], T, (err: Error | null | undefined) => void]): T;
        function pipeline(...streams: Array<PipelineStream | ((err: Error | null | undefined) => void)>): Writable;

        interface FinishedOptions {
            readable?: boolean;
            writable?: boolean;
            error?: boolean;
        }
        /** Call back once the stream has ended, finished or failed. Returns a function that stops listening. */
        function finished(stream: Readable | Writable, callback: (err?: Error | null) => void): () => void;
        function finished(stream: Readable | Writable, options: FinishedOptions, callback: (err?: Error | null) => void): () => void;

        function isReadable(stream: unknown): boolean;
        function isErrored(stream: unknown): boolean;

        const promises: {
            pipeline(...streams: PipelineStream[]): Promise<void>;
            finished(stream: Readable | Writable, options?: FinishedOptions): Promise<void>;
        };
    }

    export = internal;
}
