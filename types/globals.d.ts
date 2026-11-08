// FrogJS globals. Every built-in module is also a global of the same name
// (`fs`, `http`, `child_process`, ...), the object require() returns.

declare namespace FrogJS {
    /** `process.stdout` / `process.stderr`: synchronous writes to fd 1 / 2. */
    type StdioWriteStream = import('stream').Writable & {
        readonly fd: number;
        readonly isTTY: boolean;
    };

    interface ProcessVersions {
        frogjs: string;
        v8: string;
        uv: string;
    }

    interface MemoryUsage {
        rss: number;
        heapTotal: number;
        heapUsed: number;
        external: number;
    }

    interface Process extends EventEmitterType {
        /** `[execPath, absolute script path, ...arguments]` (no script entry for `-e` and the REPL). */
        argv: string[];
        /** The FrogJS options before the script name, e.g. `['-e', 'code']`. */
        execArgv: string[];
        /** Absolute path of the frogjs executable. */
        readonly execPath: string;
        env: Record<string, string | undefined>;
        /** Process ID. */
        readonly pid: number;
        /** Parent's process ID. */
        readonly ppid: number;
        /** `'darwin'`, `'linux'`, ... */
        readonly platform: string;
        /** `'arm64'`, `'x64'`, ... */
        readonly arch: string;
        /** `'v1.0.0'` */
        readonly version: string;
        readonly versions: ProcessVersions;
        readonly release: { name: string };
        title: string;
        readonly config: object;
        readonly features: object;
        readonly stdout: StdioWriteStream;
        readonly stderr: StdioWriteStream;
        /** A Readable over fd 0, created on first use. Reading it keeps the process alive. */
        readonly stdin: import('fs').ReadStream & { readonly isTTY: boolean };
        /** The exit code when the process ends by itself, or calls exit() without a code. */
        exitCode: number | undefined;
        /** Current working directory. */
        cwd(): string;
        chdir(directory: string): void;
        /** Current mask, or set it and get the previous one. */
        umask(mask?: number | string): number;
        /** Emit `'exit'` and end the process (in a worker: end only that worker). */
        exit(code?: number): never;
        /** Send a signal (default `'SIGTERM'`; 0 tests whether the process exists). */
        kill(pid: number, signal?: string | number): true;
        /** Run callback after the current operation, before I/O and timers. */
        nextTick<A extends any[]>(callback: (...args: A) => void, ...args: A): void;
        /** `[seconds, nanoseconds]` from an arbitrary point, or since `previous`. */
        hrtime: ((previous?: [number, number]) => [number, number]) & { bigint(): bigint };
        /** Seconds since the process started. */
        uptime(): number;
        memoryUsage: (() => MemoryUsage) & { rss(): number };
        /** Microseconds of CPU used. */
        cpuUsage(): { user: number; system: number };
        /** Emit `'warning'`; it is printed unless a listener exists. */
        emitWarning(warning: string | Error, type?: string | { type?: string; code?: string }, code?: string): void;

        on(event: 'exit' | 'beforeExit', listener: (code: number) => void): this;
        on(event: 'uncaughtException', listener: (error: unknown, origin: string) => void): this;
        on(event: 'unhandledRejection', listener: (reason: unknown, promise: Promise<unknown>) => void): this;
        on(event: 'warning', listener: (warning: Error) => void): this;
        on(event: NodeSignal, listener: (signal: NodeSignal, number: number) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    type EventEmitterType = import('events');
    type NodeSignal = 'SIGHUP' | 'SIGINT' | 'SIGQUIT' | 'SIGILL' | 'SIGTRAP' | 'SIGABRT' | 'SIGFPE' | 'SIGKILL' | 'SIGUSR1' |
        'SIGSEGV' | 'SIGUSR2' | 'SIGPIPE' | 'SIGALRM' | 'SIGTERM' | 'SIGCHLD' | 'SIGCONT' | 'SIGSTOP' | 'SIGTSTP' | 'SIGWINCH';

    /** What setTimeout() and setInterval() return. */
    class Timeout {
        /** Keep the process alive for this timer (the default). */
        ref(): this;
        /** Don't keep the process alive for this timer. */
        unref(): this;
        hasRef(): boolean;
        /** Start the delay over; a timeout that already ran runs again. */
        refresh(): this;
        /** Like clearTimeout(). */
        close(): this;
        /** The numeric ID, also accepted by clearTimeout(). */
        [Symbol.toPrimitive](): number;
    }

    /** What setImmediate() returns. */
    class Immediate {
        ref(): this;
        unref(): this;
        hasRef(): boolean;
    }

    interface Console {
        log(...data: any[]): void;
        info(...data: any[]): void;
        debug(...data: any[]): void;
        error(...data: any[]): void;
        warn(...data: any[]): void;
    }

    interface Module {
        exports: any;
        id: string;
        path: string;
        filename: string | null;
        loaded: boolean;
        children: Module[];
        paths: string[];
        parent: Module | null | undefined;
        require(id: string): any;
    }
}

declare var process: FrogJS.Process;
declare var console: FrogJS.Console;

/** Run callback once after `delay` ms (default 1; values outside 1..2^31-1 become 1). */
declare function setTimeout<A extends any[]>(callback: (...args: A) => void, delay?: number, ...args: A): FrogJS.Timeout;
/** Run callback every `delay` ms. */
declare function setInterval<A extends any[]>(callback: (...args: A) => void, delay?: number, ...args: A): FrogJS.Timeout;
/** Run callback after the I/O of the current loop iteration. */
declare function setImmediate<A extends any[]>(callback: (...args: A) => void, ...args: A): FrogJS.Immediate;
declare function clearTimeout(timer: FrogJS.Timeout | number | string | null | undefined): void;
declare function clearInterval(timer: FrogJS.Timeout | number | string | null | undefined): void;
declare function clearImmediate(immediate: FrogJS.Immediate | null | undefined): void;
/** Run callback as a microtask. */
declare function queueMicrotask(callback: () => void): void;
/** A deep copy by the structured clone algorithm; `transfer` moves ArrayBuffers and MessagePorts. */
declare function structuredClone<T>(value: T, options?: { transfer?: readonly any[] }): T;

/** The global object. */
declare var global: typeof globalThis;

declare var performance: {
    readonly timeOrigin: number;
    /** Milliseconds since the process started. */
    now(): number;
    toJSON(): { timeOrigin: number };
};

declare class TextEncoder {
    readonly encoding: 'utf-8';
    encode(input?: string): Uint8Array;
}

/** UTF-8 only: other encodings throw ERR_ENCODING_NOT_SUPPORTED. */
declare class TextDecoder {
    constructor(encoding?: string, options?: { fatal?: boolean; ignoreBOM?: boolean });
    readonly encoding: 'utf-8';
    readonly fatal: boolean;
    readonly ignoreBOM: boolean;
    decode(input?: ArrayBufferView | ArrayBuffer): string;
}

/** Load a built-in module by name (optionally `node:name`), a file, or a package from node_modules. */
declare var require: {
    (id: string): any;
    /** The file `id` would load, or the name of a built-in. */
    resolve(id: string): string;
    /** Loaded modules by file name; deleting an entry makes require() load the file again. */
    cache: Record<string, FrogJS.Module>;
    /** The main module (undefined for -e code). */
    main: FrogJS.Module | undefined;
};
/** The current module. */
declare var module: FrogJS.Module;
/** Shorthand for `module.exports`. */
declare var exports: any;
/** Absolute path of the current file (`'[eval]'` for `-e` code). */
declare var __filename: string;
/** Directory of the current file. */
declare var __dirname: string;

declare var fs: typeof import('fs');
declare var net: typeof import('net');
declare var http: typeof import('http');
declare var path: typeof import('path');
declare var os: typeof import('os');
declare var events: typeof import('events');
declare var stream: typeof import('stream');
declare var util: typeof import('util');
declare var crypto: typeof import('crypto');
declare var child_process: typeof import('child_process');
declare var worker_threads: typeof import('worker_threads');
