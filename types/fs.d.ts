// FrogJS fs follows Node.js: without an encoding, contents are Buffers; errors
// are Error objects with code, errno, syscall and path; Stats has methods.

declare module 'fs' {
    import { Readable, Writable } from 'stream';

    /** A failed file system call, e.g. `code: 'ENOENT'`, `syscall: 'open'`, `path: 'x'`. */
    interface FsError extends Error {
        code: string;
        errno: number;
        syscall: string;
        path?: string;
        dest?: string;
    }

    type PathLike = string | Buffer | Uint8Array;
    type Data = string | ArrayBufferView;
    type Flags = string | number;
    type Mode = string | number;
    type Callback = (err: FsError | null) => void;

    interface ReadOptions { encoding?: BufferEncoding | 'buffer' | null; flag?: Flags; }
    interface WriteOptions { encoding?: BufferEncoding | null; mode?: Mode; flag?: Flags; }
    interface MkdirOptions { mode?: Mode; recursive?: boolean; }
    interface RmOptions { recursive?: boolean; force?: boolean; }
    interface StatOptions { throwIfNoEntry?: boolean; }

    class Stats {
        dev: number;
        ino: number;
        mode: number;
        nlink: number;
        uid: number;
        gid: number;
        rdev: number;
        /** Size in bytes. */
        size: number;
        blksize: number;
        blocks: number;
        atimeMs: number;
        mtimeMs: number;
        ctimeMs: number;
        birthtimeMs: number;
        atime: Date;
        mtime: Date;
        ctime: Date;
        birthtime: Date;
        isFile(): boolean;
        isDirectory(): boolean;
        isSymbolicLink(): boolean;
        isFIFO(): boolean;
        isSocket(): boolean;
        isBlockDevice(): boolean;
        isCharacterDevice(): boolean;
    }

    /** An entry from `readdir(path, { withFileTypes: true })`. */
    class Dirent {
        name: string;
        parentPath: string;
        isFile(): boolean;
        isDirectory(): boolean;
        isSymbolicLink(): boolean;
    }

    const constants: {
        readonly F_OK: number; readonly R_OK: number; readonly W_OK: number; readonly X_OK: number;
        readonly O_RDONLY: number; readonly O_WRONLY: number; readonly O_RDWR: number; readonly O_CREAT: number;
        readonly O_TRUNC: number; readonly O_APPEND: number; readonly O_EXCL: number; readonly O_SYNC: number;
        readonly COPYFILE_EXCL: number;
    };

    /** Bytes of a file (or descriptor), or a string with an encoding. */
    function readFileSync(path: PathLike | number, options?: { encoding?: null | 'buffer'; flag?: Flags } | null): Buffer;
    function readFileSync(path: PathLike | number, options: BufferEncoding | (ReadOptions & { encoding: BufferEncoding })): string;
    function readFile(path: PathLike | number, callback: (err: FsError | null, data: Buffer) => void): void;
    function readFile(path: PathLike | number, options: { encoding?: null | 'buffer'; flag?: Flags } | null, callback: (err: FsError | null, data: Buffer) => void): void;
    function readFile(path: PathLike | number, options: BufferEncoding | (ReadOptions & { encoding: BufferEncoding }), callback: (err: FsError | null, data: string) => void): void;

    function writeFileSync(path: PathLike | number, data: Data, options?: WriteOptions | BufferEncoding | null): void;
    function writeFile(path: PathLike | number, data: Data, callback: Callback): void;
    function writeFile(path: PathLike | number, data: Data, options: WriteOptions | BufferEncoding | null, callback: Callback): void;
    function appendFileSync(path: PathLike | number, data: Data, options?: WriteOptions | BufferEncoding | null): void;
    function appendFile(path: PathLike | number, data: Data, callback: Callback): void;
    function appendFile(path: PathLike | number, data: Data, options: WriteOptions | BufferEncoding | null, callback: Callback): void;

    /** Create a directory. With `recursive`, returns the first directory created, if any. */
    function mkdirSync(path: PathLike, options?: Mode | MkdirOptions): string | undefined;
    function mkdir(path: PathLike, callback: (err: FsError | null, first?: string) => void): void;
    function mkdir(path: PathLike, options: Mode | MkdirOptions, callback: (err: FsError | null, first?: string) => void): void;

    /** Names of the entries in a directory (without `.` and `..`). */
    function readdirSync(path: PathLike, options?: BufferEncoding | { encoding?: BufferEncoding; withFileTypes?: false }): string[];
    function readdirSync(path: PathLike, options: { withFileTypes: true }): Dirent[];
    function readdir(path: PathLike, callback: (err: FsError | null, files: string[]) => void): void;
    function readdir(path: PathLike, options: BufferEncoding | { encoding?: BufferEncoding; withFileTypes?: false }, callback: (err: FsError | null, files: string[]) => void): void;
    function readdir(path: PathLike, options: { withFileTypes: true }, callback: (err: FsError | null, files: Dirent[]) => void): void;

    /** Remove an empty directory. */
    function rmdirSync(path: PathLike): void;
    function rmdir(path: PathLike, callback: Callback): void;
    function rmdir(path: PathLike, options: object, callback: Callback): void;
    /** Remove a file, or a directory tree with `recursive`. Symbolic links are removed, not followed. */
    function rmSync(path: PathLike, options?: RmOptions): void;
    function rm(path: PathLike, callback: Callback): void;
    function rm(path: PathLike, options: RmOptions, callback: Callback): void;

    function statSync(path: PathLike): Stats;
    function statSync(path: PathLike, options: StatOptions): Stats | undefined;
    function stat(path: PathLike, callback: (err: FsError | null, stats: Stats) => void): void;
    function stat(path: PathLike, options: object, callback: (err: FsError | null, stats: Stats) => void): void;
    /** Like stat, but about a symbolic link itself. */
    function lstatSync(path: PathLike): Stats;
    function lstatSync(path: PathLike, options: StatOptions): Stats | undefined;
    function lstat(path: PathLike, callback: (err: FsError | null, stats: Stats) => void): void;
    function lstat(path: PathLike, options: object, callback: (err: FsError | null, stats: Stats) => void): void;
    function fstatSync(fd: number): Stats;
    function fstat(fd: number, callback: (err: FsError | null, stats: Stats) => void): void;
    function fstat(fd: number, options: object, callback: (err: FsError | null, stats: Stats) => void): void;
    function realpathSync(path: PathLike): string;
    function realpath(path: PathLike, callback: (err: FsError | null, resolved: string) => void): void;
    function realpath(path: PathLike, options: object, callback: (err: FsError | null, resolved: string) => void): void;

    function existsSync(path: PathLike): boolean;
    function accessSync(path: PathLike, mode?: number): void;
    function access(path: PathLike, callback: Callback): void;
    function access(path: PathLike, mode: number, callback: Callback): void;

    /** Delete a file. */
    function unlinkSync(path: PathLike): void;
    function unlink(path: PathLike, callback: Callback): void;
    function renameSync(oldPath: PathLike, newPath: PathLike): void;
    function rename(oldPath: PathLike, newPath: PathLike, callback: Callback): void;
    function copyFileSync(src: PathLike, dest: PathLike, mode?: number): void;
    function copyFile(src: PathLike, dest: PathLike, callback: Callback): void;
    function copyFile(src: PathLike, dest: PathLike, mode: number, callback: Callback): void;

    function openSync(path: PathLike, flags?: Flags, mode?: Mode): number;
    function open(path: PathLike, callback: (err: FsError | null, fd: number) => void): void;
    function open(path: PathLike, flags: Flags, callback: (err: FsError | null, fd: number) => void): void;
    function open(path: PathLike, flags: Flags, mode: Mode, callback: (err: FsError | null, fd: number) => void): void;
    function closeSync(fd: number): void;
    function close(fd: number, callback?: Callback): void;

    interface ReadPosition { offset?: number; length?: number; position?: number | null; }
    /** Read into `buffer`; returns the bytes read (0 at the end of the file). */
    function readSync(fd: number, buffer: ArrayBufferView, offset?: number | null, length?: number | null, position?: number | null): number;
    function readSync(fd: number, buffer: ArrayBufferView, options: ReadPosition): number;
    type ReadCallback = (err: FsError | null, bytesRead: number, buffer: ArrayBufferView) => void;
    function read(fd: number, buffer: ArrayBufferView, callback: ReadCallback): void;
    function read(fd: number, buffer: ArrayBufferView, options: ReadPosition, callback: ReadCallback): void;
    function read(fd: number, buffer: ArrayBufferView, offset: number | null, length: number | null, position: number | null, callback: ReadCallback): void;
    /** Returns the bytes written, which can be fewer than given. */
    function writeSync(fd: number, data: ArrayBufferView, offset?: number | null, length?: number | null, position?: number | null): number;
    function writeSync(fd: number, data: string, position?: number | null, encoding?: BufferEncoding): number;
    type WriteCallback = (err: FsError | null, written: number, data: string | ArrayBufferView) => void;
    function write(fd: number, data: string | ArrayBufferView, callback: WriteCallback): void;
    function write(fd: number, data: ArrayBufferView, offset: number | null, callback: WriteCallback): void;
    function write(fd: number, data: ArrayBufferView, offset: number | null, length: number | null, callback: WriteCallback): void;
    function write(fd: number, data: ArrayBufferView, offset: number | null, length: number | null, position: number | null, callback: WriteCallback): void;
    function write(fd: number, data: string, position: number | null, callback: WriteCallback): void;
    function write(fd: number, data: string, position: number | null, encoding: BufferEncoding, callback: WriteCallback): void;

    interface ReadStreamOptions {
        flags?: Flags;
        encoding?: BufferEncoding;
        fd?: number | null;
        mode?: Mode;
        autoClose?: boolean;
        emitClose?: boolean;
        /** First byte to read. */
        start?: number;
        /** Last byte to read (inclusive). */
        end?: number;
        highWaterMark?: number;
    }
    interface WriteStreamOptions {
        flags?: Flags;
        encoding?: BufferEncoding;
        fd?: number | null;
        mode?: Mode;
        autoClose?: boolean;
        emitClose?: boolean;
        start?: number;
        highWaterMark?: number;
    }

    class ReadStream extends Readable {
        constructor(path: PathLike | null, options?: BufferEncoding | ReadStreamOptions);
        path: PathLike | null;
        fd: number | null;
        flags: Flags;
        mode: number;
        start: number | undefined;
        end: number;
        autoClose: boolean;
        bytesRead: number;
        close(callback?: () => void): void;
    }
    class WriteStream extends Writable {
        constructor(path: PathLike | null, options?: BufferEncoding | WriteStreamOptions);
        path: PathLike | null;
        fd: number | null;
        flags: Flags;
        mode: number;
        start: number | undefined;
        autoClose: boolean;
        bytesWritten: number;
        close(callback?: () => void): void;
    }
    function createReadStream(path: PathLike | null, options?: BufferEncoding | ReadStreamOptions): ReadStream;
    function createWriteStream(path: PathLike | null, options?: BufferEncoding | WriteStreamOptions): WriteStream;

    namespace promises {
        function readFile(path: PathLike | number, options?: { encoding?: null | 'buffer'; flag?: Flags } | null): Promise<Buffer>;
        function readFile(path: PathLike | number, options: BufferEncoding | (ReadOptions & { encoding: BufferEncoding })): Promise<string>;
        function writeFile(path: PathLike | number, data: Data, options?: WriteOptions | BufferEncoding | null): Promise<void>;
        function appendFile(path: PathLike | number, data: Data, options?: WriteOptions | BufferEncoding | null): Promise<void>;
        function mkdir(path: PathLike, options?: Mode | MkdirOptions): Promise<string | undefined>;
        function readdir(path: PathLike, options?: BufferEncoding | { encoding?: BufferEncoding; withFileTypes?: false }): Promise<string[]>;
        function readdir(path: PathLike, options: { withFileTypes: true }): Promise<Dirent[]>;
        function rmdir(path: PathLike): Promise<void>;
        function rm(path: PathLike, options?: RmOptions): Promise<void>;
        function unlink(path: PathLike): Promise<void>;
        function stat(path: PathLike, options?: object): Promise<Stats>;
        function lstat(path: PathLike, options?: object): Promise<Stats>;
        function realpath(path: PathLike, options?: object): Promise<string>;
        function rename(oldPath: PathLike, newPath: PathLike): Promise<void>;
        function copyFile(src: PathLike, dest: PathLike, mode?: number): Promise<void>;
        function access(path: PathLike, mode?: number): Promise<void>;
    }
}
