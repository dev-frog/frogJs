declare module 'tty' {
    import * as fs from 'fs';
    import { Writable } from 'stream';

    /** Whether file descriptor `fd` is a terminal. */
    function isatty(fd: number): boolean;

    /** A terminal to read from (process.stdin is one when fd 0 is a terminal). */
    class ReadStream extends fs.ReadStream {
        constructor(fd: number, options?: object);
        readonly isTTY: true;
        /** Whether raw mode is on. */
        isRaw: boolean;
        /** Raw mode: keys arrive one at a time, without echo or line editing. (Set on each instance.) */
        readonly setRawMode: (mode: boolean) => this;
    }

    type Direction = -1 | 0 | 1;

    /** A terminal to write to (process.stdout and stderr are ones when their fd is a terminal). */
    class WriteStream extends Writable {
        constructor(fd: number);
        readonly fd: number;
        readonly isTTY: true;
        readonly columns: number;
        readonly rows: number;
        /** `[columns, rows]` */
        getWindowSize(): [number, number];
        /** 1, 4, 8 or 24 bits, from the environment (NO_COLOR, FORCE_COLOR, TERM, COLORTERM). */
        getColorDepth(env?: Record<string, string | undefined>): number;
        hasColors(count?: number, env?: Record<string, string | undefined>): boolean;
        hasColors(env?: Record<string, string | undefined>): boolean;
        cursorTo(x: number, y?: number, callback?: () => void): boolean;
        cursorTo(x: number, callback: () => void): boolean;
        moveCursor(dx: number, dy: number, callback?: () => void): boolean;
        /** -1: to the left of the cursor, 1: to the right, 0: the whole line. */
        clearLine(dir: Direction, callback?: () => void): boolean;
        clearScreenDown(callback?: () => void): boolean;
        on(event: 'resize', listener: () => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
    }
}
