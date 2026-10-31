declare module 'child_process' {
    import EventEmitter = require('events');
    import { Readable, Writable } from 'stream';

    type Signal =
        | 'SIGHUP' | 'SIGINT' | 'SIGQUIT' | 'SIGILL' | 'SIGTRAP' | 'SIGABRT' | 'SIGIOT' | 'SIGBUS' | 'SIGFPE'
        | 'SIGKILL' | 'SIGUSR1' | 'SIGSEGV' | 'SIGUSR2' | 'SIGPIPE' | 'SIGALRM' | 'SIGTERM' | 'SIGCHLD'
        | 'SIGCONT' | 'SIGSTOP' | 'SIGTSTP' | 'SIGTTIN' | 'SIGTTOU' | 'SIGURG' | 'SIGXCPU' | 'SIGXFSZ'
        | 'SIGVTALRM' | 'SIGPROF' | 'SIGWINCH' | 'SIGIO' | 'SIGSYS' | 'SIGINFO' | 'SIGPWR' | 'SIGSTKFLT';

    /** `'pipe'` (a stream in the parent), `'inherit'` (the parent's), `'ignore'`, a file descriptor, or a stream with an `fd`. */
    type IOType = 'pipe' | 'overlapped' | 'inherit' | 'ignore';
    type StdioEntry = IOType | number | { fd: number } | null | undefined;
    type StdioOptions = IOType | StdioEntry[];

    /** An error from spawning or killing: `spawn ls ENOENT`. */
    interface ErrnoException extends Error {
        errno?: number;
        code?: string;
        syscall?: string;
        path?: string;
        spawnargs?: string[];
    }

    interface ProcessOptions {
        /** Working directory (string or `file:` URL). */
        cwd?: string | URLLike;
        /** Default `process.env`. */
        env?: Record<string, string | undefined>;
        /** `argv[0]` seen by the child (default: the command). */
        argv0?: string;
        uid?: number;
        gid?: number;
        /** Ignored (Windows only in Node.js). */
        windowsHide?: boolean;
    }

    interface URLLike {
        href: string;
        pathname: string;
    }

    interface SpawnOptions extends ProcessOptions {
        stdio?: StdioOptions;
        /** Run the command through `/bin/sh` (true) or this shell. */
        shell?: boolean | string;
        /** Make the child the leader of a new process group. */
        detached?: boolean;
        /** Send `killSignal` after this many ms. */
        timeout?: number;
        /** Default `'SIGTERM'`. */
        killSignal?: Signal | number;
    }

    interface BufferedOptions extends ProcessOptions {
        /** Kill the child after this many ms (0: never). */
        timeout?: number;
        /** Bytes allowed on stdout or stderr before the child is killed (default 1048576). */
        maxBuffer?: number;
        killSignal?: Signal | number;
    }

    interface ExecOptions extends BufferedOptions {
        /** Default `'/bin/sh'`. */
        shell?: string;
    }

    interface ExecOptionsWithStringEncoding extends ExecOptions {
        /** Default `'utf8'`. */
        encoding?: BufferEncoding;
    }

    interface ExecOptionsWithBufferEncoding extends ExecOptions {
        encoding: 'buffer' | null;
    }

    interface ExecFileOptions extends BufferedOptions {
        /** Run the file through a shell (default false). */
        shell?: boolean | string;
    }

    interface ExecFileOptionsWithStringEncoding extends ExecFileOptions {
        encoding?: BufferEncoding;
    }

    interface ExecFileOptionsWithBufferEncoding extends ExecFileOptions {
        encoding: 'buffer' | null;
    }

    /** The error exec() and execFile() call back with. */
    interface ExecException extends Error {
        /** The exit code, or an error name such as `'ENOENT'` or `'ERR_CHILD_PROCESS_STDIO_MAXBUFFER'`. */
        code?: number | string;
        killed?: boolean;
        signal?: Signal | null;
        cmd?: string;
    }

    interface SpawnSyncOptions extends ProcessOptions {
        stdio?: StdioOptions;
        shell?: boolean | string;
        /** Written to the child's stdin. */
        input?: string | Buffer | ArrayBufferView;
        timeout?: number;
        killSignal?: Signal | number;
        /** Default 1048576. */
        maxBuffer?: number;
        /** Default `'buffer'`. */
        encoding?: BufferEncoding | 'buffer' | null;
    }

    interface SpawnSyncReturns<T> {
        pid: number;
        /** `[null, stdout, stderr]`; null when the child could not be started. */
        output: Array<T | null> | null;
        stdout: T;
        stderr: T;
        /** Exit code; null if the child was killed by a signal. */
        status: number | null;
        signal: Signal | null;
        /** Set when the child could not be started (ENOENT), timed out (ETIMEDOUT) or exceeded maxBuffer (ENOBUFS). */
        error?: ErrnoException;
    }

    /** The error execSync() and execFileSync() throw: `Command failed: <command>` plus stderr. */
    interface ExecSyncError extends Error, Omit<SpawnSyncReturns<Buffer | string>, 'error'> {}

    class ChildProcess extends EventEmitter {
        /** null unless stdio[0] is 'pipe'. */
        stdin: Writable | null;
        stdout: Readable | null;
        stderr: Readable | null;
        readonly stdio: Array<Readable | Writable | null>;
        /** undefined if spawning failed. */
        readonly pid?: number;
        readonly exitCode: number | null;
        readonly signalCode: Signal | null;
        /** A signal was sent successfully with kill(). */
        readonly killed: boolean;
        /** Always false: FrogJS has no IPC channel. */
        readonly connected: boolean;
        readonly spawnfile: string;
        readonly spawnargs: string[];

        /** Send a signal (default 'SIGTERM'). False if the child has already exited. */
        kill(signal?: Signal | number): boolean;
        ref(): void;
        unref(): void;

        on(event: 'spawn', listener: () => void): this;
        on(event: 'exit' | 'close', listener: (code: number | null, signal: Signal | null) => void): this;
        on(event: 'error', listener: (err: ErrnoException) => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
        once(event: 'spawn', listener: () => void): this;
        once(event: 'exit' | 'close', listener: (code: number | null, signal: Signal | null) => void): this;
        once(event: 'error', listener: (err: ErrnoException) => void): this;
        once(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    interface ChildProcessWithoutNullStreams extends ChildProcess {
        stdin: Writable;
        stdout: Readable;
        stderr: Readable;
    }

    function spawn(command: string, options?: SpawnOptions & { stdio?: 'pipe' | undefined }): ChildProcessWithoutNullStreams;
    function spawn(command: string, args: readonly string[], options?: SpawnOptions & { stdio?: 'pipe' | undefined }): ChildProcessWithoutNullStreams;
    function spawn(command: string, options: SpawnOptions): ChildProcess;
    function spawn(command: string, args: readonly string[], options: SpawnOptions): ChildProcess;

    type ExecCallback<T> = (error: ExecException | null, stdout: T, stderr: T) => void;

    /** Run a command in /bin/sh and buffer its output. */
    function exec(command: string, callback?: ExecCallback<string>): ChildProcess;
    function exec(command: string, options: ExecOptionsWithBufferEncoding, callback?: ExecCallback<Buffer>): ChildProcess;
    function exec(command: string, options: ExecOptionsWithStringEncoding, callback?: ExecCallback<string>): ChildProcess;
    namespace exec {
        function __promisify__(command: string, options?: ExecOptionsWithStringEncoding): Promise<{ stdout: string; stderr: string }>;
        function __promisify__(command: string, options: ExecOptionsWithBufferEncoding): Promise<{ stdout: Buffer; stderr: Buffer }>;
    }

    /** Run a file directly (no shell) and buffer its output. */
    function execFile(file: string, callback?: ExecCallback<string>): ChildProcess;
    function execFile(file: string, args: readonly string[], callback?: ExecCallback<string>): ChildProcess;
    function execFile(file: string, options: ExecFileOptionsWithBufferEncoding, callback?: ExecCallback<Buffer>): ChildProcess;
    function execFile(file: string, options: ExecFileOptionsWithStringEncoding, callback?: ExecCallback<string>): ChildProcess;
    function execFile(file: string, args: readonly string[], options: ExecFileOptionsWithBufferEncoding, callback?: ExecCallback<Buffer>): ChildProcess;
    function execFile(file: string, args: readonly string[], options: ExecFileOptionsWithStringEncoding, callback?: ExecCallback<string>): ChildProcess;

    /** Run the child and wait for it. */
    function spawnSync(command: string, options: SpawnSyncOptions & { encoding: BufferEncoding }): SpawnSyncReturns<string>;
    function spawnSync(command: string, args: readonly string[], options: SpawnSyncOptions & { encoding: BufferEncoding }): SpawnSyncReturns<string>;
    function spawnSync(command: string, options?: SpawnSyncOptions): SpawnSyncReturns<Buffer>;
    function spawnSync(command: string, args?: readonly string[], options?: SpawnSyncOptions): SpawnSyncReturns<Buffer>;

    /** Run a command in /bin/sh, wait, and return its stdout. Throws an ExecSyncError if it fails. */
    function execSync(command: string, options: SpawnSyncOptions & { encoding: BufferEncoding }): string;
    function execSync(command: string, options?: SpawnSyncOptions): Buffer;

    /** Run a file, wait, and return its stdout. Throws an ExecSyncError if it fails. */
    function execFileSync(file: string, options: SpawnSyncOptions & { encoding: BufferEncoding }): string;
    function execFileSync(file: string, args: readonly string[], options: SpawnSyncOptions & { encoding: BufferEncoding }): string;
    function execFileSync(file: string, options?: SpawnSyncOptions): Buffer;
    function execFileSync(file: string, args?: readonly string[], options?: SpawnSyncOptions): Buffer;
}
