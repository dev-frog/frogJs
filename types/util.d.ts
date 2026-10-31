declare module 'util' {
    interface InspectOptions {
        /** Show non-enumerable properties and symbols (default false). */
        showHidden?: boolean;
        /** How deep to recurse into objects; null for no limit (default 2). */
        depth?: number | null;
        /** ANSI colors (default false). */
        colors?: boolean;
        /** Array/Set/Map entries shown (default 100; null for all). */
        maxArrayLength?: number | null;
        /** Characters of a string shown (default 10000; null for all). */
        maxStringLength?: number | null;
        /** Line length at which output is split over lines (default 80). */
        breakLength?: number;
        /** false: one property per line; a number: combine up to that many short entries per line (default 3). */
        compact?: boolean | number;
        /** Sort object keys, or sort with a compare function. */
        sorted?: boolean | ((a: string, b: string) => number);
    }

    type Style = 'special' | 'number' | 'bigint' | 'boolean' | 'undefined' | 'null' | 'string' | 'symbol' | 'date' | 'regexp' | 'module' | 'name';

    /** Format any value the way console.log shows it (same output as Node.js). */
    function inspect(value: any, options?: InspectOptions): string;
    /** Legacy form. */
    function inspect(value: any, showHidden?: boolean, depth?: number | null, colors?: boolean): string;
    namespace inspect {
        /** `obj[inspect.custom](depth, options, inspect)` replaces the default output. */
        const custom: unique symbol;
        let defaultOptions: InspectOptions;
        /** ANSI codes `[open, close]` by color name. */
        let colors: Record<string, [number, number]>;
        /** Color name used for each kind of value. */
        let styles: Record<Style, string>;
    }

    /** printf-style formatting: %s %d %i %f %j %o %O %c %%. Extra arguments are appended, inspected. */
    function format(format?: any, ...args: any[]): string;
    function formatWithOptions(inspectOptions: InspectOptions, format?: any, ...args: any[]): string;

    interface CustomPromisifiedSymbol<T extends (...args: any[]) => any> extends Function {
        [promisify.custom]: T;
    }

    /** How the declarations of functions with a custom promisified form (child_process.exec) say so. */
    interface CustomPromisifiedLegacy<T extends (...args: any[]) => any> extends Function {
        __promisify__: T;
    }

    /** Turn a function that takes a Node-style `(err, value)` callback last into one that returns a Promise. */
    function promisify<T extends (...args: any[]) => any>(fn: CustomPromisifiedSymbol<T>): T;
    function promisify<T extends (...args: any[]) => any>(fn: CustomPromisifiedLegacy<T>): T;
    function promisify<R>(fn: (callback: (err: any, result: R) => void) => void): () => Promise<R>;
    function promisify(fn: (callback: (err?: any) => void) => void): () => Promise<void>;
    function promisify<A1, R>(fn: (arg1: A1, callback: (err: any, result: R) => void) => void): (arg1: A1) => Promise<R>;
    function promisify<A1>(fn: (arg1: A1, callback: (err?: any) => void) => void): (arg1: A1) => Promise<void>;
    function promisify<A1, A2, R>(fn: (arg1: A1, arg2: A2, callback: (err: any, result: R) => void) => void): (arg1: A1, arg2: A2) => Promise<R>;
    function promisify<A1, A2>(fn: (arg1: A1, arg2: A2, callback: (err?: any) => void) => void): (arg1: A1, arg2: A2) => Promise<void>;
    function promisify<A1, A2, A3, R>(fn: (arg1: A1, arg2: A2, arg3: A3, callback: (err: any, result: R) => void) => void): (arg1: A1, arg2: A2, arg3: A3) => Promise<R>;
    function promisify(fn: Function): (...args: any[]) => Promise<any>;
    namespace promisify {
        /** A function's `[promisify.custom]` property is what promisify() returns for it. */
        const custom: unique symbol;
    }

    /** Make `constructor` inherit from `superConstructor` (prefer `class ... extends`). */
    function inherits(constructor: Function, superConstructor: Function): void;
    /** Deep strict equality, as used by `assert.deepStrictEqual`. */
    function isDeepStrictEqual(a: unknown, b: unknown): boolean;
    /** Remove ANSI escape codes. */
    function stripVTControlCharacters(str: string): string;
}
