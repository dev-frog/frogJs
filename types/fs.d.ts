// FrogJS fs works on UTF-8 text. Errors are strings (e.g. "Error reading
// file: no such file or directory"), both thrown by the *Sync functions and
// passed to callbacks, and file contents are always strings.

declare module 'fs' {
    /** An error message, e.g. `'Error reading file: no such file or directory'`. */
    type FsError = string;

    interface Stats {
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
        atime: Date;
        mtime: Date;
        ctime: Date;
        /** A boolean in FrogJS (a method in Node.js). */
        isFile: boolean;
        /** A boolean in FrogJS (a method in Node.js). */
        isDirectory: boolean;
    }

    /** Read a file as UTF-8 text. Throws an error string. */
    function readFileSync(path: string): string;
    /** Write (replace) a file. Throws an error string. */
    function writeFileSync(path: string, data: string | Buffer): void;
    /** `data` is null when `err` is set. */
    function readFile(path: string, callback: (err: FsError | null, data: string) => void): void;
    function writeFile(path: string, data: string | Buffer, callback: (err: FsError | null) => void): void;

    /** Create a directory (`mode` default 0o777). Not recursive. */
    function mkdirSync(path: string, mode?: number): void;
    function mkdir(path: string, callback: (err: FsError | null) => void): void;
    function mkdir(path: string, mode: number, callback: (err: FsError | null) => void): void;

    /** Names of the entries in a directory (without `.` and `..`). */
    function readdirSync(path: string): string[];
    function readdir(path: string, callback: (err: FsError | null, files: string[]) => void): void;

    /** Remove an empty directory. */
    function rmdirSync(path: string): void;
    function rmdir(path: string, callback: (err: FsError | null) => void): void;

    function statSync(path: string): Stats;
    /** `stats` is null when `err` is set. */
    function stat(path: string, callback: (err: FsError | null, stats: Stats) => void): void;

    /** Delete a file. */
    function unlinkSync(path: string): void;
    function unlink(path: string, callback: (err: FsError | null) => void): void;

    function existsSync(path: string): boolean;
}
