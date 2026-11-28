declare module 'readline' {
    import EventEmitter = require('events');
    import { Readable, Writable } from 'stream';

    type Completer = (line: string) => [string[], string];
    type AsyncCompleter = (line: string, callback: (err: Error | null | undefined, result: [string[], string]) => void) => void;

    interface ReadLineOptions {
        input: Readable | NodeJSLikeReadable;
        output?: Writable;
        /** Tab completion: return `[matches, substring]`. */
        completer?: Completer | AsyncCompleter;
        /** Line editing and history (default: whether output is a terminal). */
        terminal?: boolean;
        /** Initial history, newest first. */
        history?: string[];
        /** Lines kept in history (default 30; 0 turns it off). */
        historySize?: number;
        removeHistoryDuplicates?: boolean;
        /** Default `'> '`. */
        prompt?: string;
        /** A `\r` followed by `\n` within this many ms is one line ending (default 100). */
        crlfDelay?: number;
        /** Closing the signal closes the interface. */
        signal?: AbortSignal;
    }

    /** Anything readable that emits 'data' and 'end'. */
    interface NodeJSLikeReadable {
        on(event: string, listener: (...args: any[]) => void): unknown;
    }

    interface CursorPos {
        rows: number;
        cols: number;
    }

    /** Reads lines from `input`; with a terminal, also edits them (history, completion, Ctrl+C). */
    class Interface extends EventEmitter implements AsyncIterable<string> {
        protected constructor(input: Readable, output?: Writable, completer?: Completer | AsyncCompleter, terminal?: boolean);
        protected constructor(options: ReadLineOptions);
        readonly terminal: boolean;
        /** The line being edited. */
        readonly line: string;
        /** Cursor position in `line`. */
        readonly cursor: number;
        readonly closed: boolean;
        readonly columns: number;
        setPrompt(prompt: string): void;
        getPrompt(): string;
        /** Write the prompt and resume input. `preserveCursor` keeps the cursor where it was. */
        prompt(preserveCursor?: boolean): void;
        /** Show `query` and call back with the next line. */
        question(query: string, callback: (answer: string) => void): void;
        question(query: string, options: { signal?: AbortSignal }, callback: (answer: string) => void): void;
        pause(): this;
        resume(): this;
        /** Write as if typed (or a key: `{ ctrl: true, name: 'u' }`). */
        write(data: string | Buffer | null | undefined, key?: Key): void;
        close(): void;
        getCursorPos(): CursorPos;
        [Symbol.asyncIterator](): AsyncIterableIterator<string>;

        on(event: 'line', listener: (line: string) => void): this;
        on(event: 'history', listener: (history: string[]) => void): this;
        on(event: 'close' | 'pause' | 'resume' | 'SIGINT' | 'SIGTSTP' | 'SIGCONT', listener: () => void): this;
        on(event: string | symbol, listener: (...args: any[]) => void): this;
        once(event: 'line', listener: (line: string) => void): this;
        once(event: string | symbol, listener: (...args: any[]) => void): this;
    }

    interface Key {
        sequence?: string;
        name?: string;
        ctrl?: boolean;
        meta?: boolean;
        shift?: boolean;
    }

    type Direction = -1 | 0 | 1;

    function createInterface(input: Readable, output?: Writable, completer?: Completer | AsyncCompleter, terminal?: boolean): Interface;
    function createInterface(options: ReadLineOptions): Interface;
    /** Make `stream` emit 'keypress' events `(string, key)`. */
    function emitKeypressEvents(stream: Readable, interface?: Interface): void;
    /** -1: to the left of the cursor, 1: to the right, 0: the whole line. */
    function clearLine(stream: Writable, dir: Direction, callback?: () => void): boolean;
    function clearScreenDown(stream: Writable, callback?: () => void): boolean;
    function cursorTo(stream: Writable, x: number, y?: number, callback?: () => void): boolean;
    function cursorTo(stream: Writable, x: number, callback: () => void): boolean;
    function moveCursor(stream: Writable, dx: number, dy: number, callback?: () => void): boolean;

    /** `require('readline/promises')` */
    const promises: typeof import('readline/promises');
}

declare module 'readline/promises' {
    import { Interface as CallbackInterface, ReadLineOptions, Completer, AsyncCompleter, Direction } from 'readline';
    import { Readable, Writable } from 'stream';

    /** An Interface whose question() returns a promise. */
    interface Interface extends Omit<CallbackInterface, 'question'> {
        question(query: string, options?: { signal?: AbortSignal }): Promise<string>;
    }
    const Interface: {
        new (options: ReadLineOptions): Interface;
        new (input: Readable, output?: Writable, completer?: Completer | AsyncCompleter, terminal?: boolean): Interface;
        readonly prototype: Interface;
    };

    /** Collects cursor moves and clears, and writes them on commit(). */
    class Readline {
        constructor(stream: Writable, options?: { autoCommit?: boolean });
        cursorTo(x: number, y?: number): this;
        moveCursor(dx: number, dy: number): this;
        clearLine(dir: Direction): this;
        clearScreenDown(): this;
        commit(): Promise<void>;
        /** Forget the actions not yet committed. */
        rollback(): this;
    }

    function createInterface(input: Readable, output?: Writable, completer?: Completer | AsyncCompleter, terminal?: boolean): Interface;
    function createInterface(options: ReadLineOptions): Interface;
}
