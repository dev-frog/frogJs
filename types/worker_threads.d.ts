declare module 'worker_threads' {
    import EventEmitter = require('events');

    /** What a message may hold is checked at run time (structured clone); functions throw DataCloneError. */
    type Transferable = ArrayBuffer | MessagePort;
    type TransferList = readonly Transferable[];

    /** One end of a MessageChannel. Adding a 'message' listener starts delivery. */
    class MessagePort extends EventEmitter {
        private constructor();
        /** Send a copy of `value`; objects in `transferList` are moved instead. */
        postMessage(value: any, transferList?: TransferList | { transfer?: TransferList }): void;
        start(): void;
        /** Close this port and the one it is connected to (both emit 'close'). */
        close(): void;
        ref(): this;
        unref(): this;
        hasRef(): boolean;
        /** Web-style handler, called with `{ type: 'message', data, target }`. */
        onmessage: ((this: MessagePort, event: MessageEvent) => void) | null;

        on(event: 'message', listener: (value: any) => void): this;
        on(event: 'close', listener: () => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
        once(event: 'message', listener: (value: any) => void): this;
        once(event: 'close', listener: () => void): this;
        once(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    interface MessageEvent {
        type: 'message';
        data: any;
        target: MessagePort;
    }

    class MessageChannel {
        readonly port1: MessagePort;
        readonly port2: MessagePort;
    }

    interface WorkerOptions {
        /** `filename` is code to run instead of a path. */
        eval?: boolean;
        /** Cloned into the worker's `workerData`. */
        workerData?: any;
        /** ArrayBuffers and MessagePorts in workerData to move instead of copy. */
        transferList?: TransferList;
        /** Appended to the worker's process.argv. */
        argv?: any[];
    }

    /** JavaScript running on another thread, in its own isolate and event loop. */
    class Worker extends EventEmitter {
        /**
         * @param filename An absolute path, a path starting with './' or '../', a `file:` URL,
         *                 or code when `options.eval` is set.
         */
        constructor(filename: string | { href: string; protocol: string; pathname: string }, options?: WorkerOptions);
        /** -1 once the worker has exited. */
        readonly threadId: number;
        postMessage(value: any, transferList?: TransferList | { transfer?: TransferList }): void;
        /** Stop the worker, even mid-computation. Resolves with the exit code (1), or undefined if it had exited. */
        terminate(): Promise<number | undefined>;
        ref(): void;
        unref(): void;

        on(event: 'online', listener: () => void): this;
        on(event: 'message', listener: (value: any) => void): this;
        /** An uncaught exception in the worker, which then exits with code 1. */
        on(event: 'error', listener: (err: any) => void): this;
        on(event: 'exit', listener: (exitCode: number) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
        once(event: 'online', listener: () => void): this;
        once(event: 'message', listener: (value: any) => void): this;
        once(event: 'error', listener: (err: any) => void): this;
        once(event: 'exit', listener: (exitCode: number) => void): this;
        once(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    const isMainThread: boolean;
    /** 0 on the main thread. */
    const threadId: number;
    /** In a worker, the port connected to its Worker object; null on the main thread. */
    const parentPort: MessagePort | null;
    /** A clone of the Worker's `workerData` option; null on the main thread. */
    const workerData: any;
    /** Not enforced by FrogJS: always `{}`. */
    const resourceLimits: {};

    /** Take the next queued message synchronously. */
    function receiveMessageOnPort(port: MessagePort): { message: any } | undefined;
}
