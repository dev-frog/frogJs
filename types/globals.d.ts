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

    interface Process {
        /** `[executable, script, ...arguments]` (no script entry for `-e` and the REPL). */
        argv: string[];
        /** The FrogJS options before the script name, e.g. `['-e', 'code']`. */
        execArgv: string[];
        env: Record<string, string | undefined>;
        /** Process ID. */
        readonly pid: number;
        /** `'darwin'`, `'linux'`, ... */
        readonly platform: string;
        /** `'v0.9.0'` */
        readonly version: string;
        readonly versions: ProcessVersions;
        readonly stdout: StdioWriteStream;
        readonly stderr: StdioWriteStream;
        /** Current working directory. */
        cwd(): string;
        /** End the process (in a worker: end only that worker). */
        exit(code?: number): never;
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
    }
}

declare var process: FrogJS.Process;
declare var console: FrogJS.Console;

/** Run callback once after `delay` ms. Returns an ID for clearTimeout(). The delay is required. */
declare function setTimeout(callback: () => void, delay: number): number;
/** Run callback every `delay` ms. Returns an ID for clearInterval(). */
declare function setInterval(callback: () => void, delay: number): number;
declare function clearTimeout(id: number | undefined): void;
declare function clearInterval(id: number | undefined): void;

/** Load a built-in module by name, or a file relative to the current module. */
declare function require(id: string): any;
/** The current module (inside modules loaded with require()). */
declare var module: FrogJS.Module;
/** Shorthand for `module.exports` (inside modules loaded with require()). */
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
