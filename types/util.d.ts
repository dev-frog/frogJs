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

    /** The reverse of promisify: call back `(err, value)` with what the async function's promise settles to. */
    function callbackify<R>(fn: () => Promise<R>): (callback: (err: any, result: R) => void) => void;
    function callbackify<A1, R>(fn: (arg1: A1) => Promise<R>): (arg1: A1, callback: (err: any, result: R) => void) => void;
    function callbackify<A1, A2, R>(fn: (arg1: A1, arg2: A2) => Promise<R>): (arg1: A1, arg2: A2, callback: (err: any, result: R) => void) => void;
    function callbackify(fn: (...args: any[]) => Promise<any>): (...args: any[]) => void;

    interface DebugLogger {
        (format: string, ...args: any[]): void;
        /** Whether NODE_DEBUG names this section. */
        readonly enabled: boolean;
    }
    /** A logger that writes to stderr when NODE_DEBUG lists `section` (wildcards allowed). */
    function debuglog(section: string, callback?: (logger: DebugLogger) => void): DebugLogger;
    /** Same as debuglog(). */
    const debug: typeof debuglog;
    /** Wrap `fn` so that its first call prints a DeprecationWarning (once per `code`). */
    function deprecate<T extends Function>(fn: T, message: string, code?: string): T;

    type ForegroundColors = 'black' | 'red' | 'green' | 'yellow' | 'blue' | 'magenta' | 'cyan' | 'white' | 'gray' | 'grey' |
        'blackBright' | 'redBright' | 'greenBright' | 'yellowBright' | 'blueBright' | 'magentaBright' | 'cyanBright' | 'whiteBright';
    type BackgroundColors = 'bgBlack' | 'bgRed' | 'bgGreen' | 'bgYellow' | 'bgBlue' | 'bgMagenta' | 'bgCyan' | 'bgWhite' | 'bgGray' | 'bgGrey' |
        'bgBlackBright' | 'bgRedBright' | 'bgGreenBright' | 'bgYellowBright' | 'bgBlueBright' | 'bgMagentaBright' | 'bgCyanBright' | 'bgWhiteBright';
    type Modifiers = 'reset' | 'bold' | 'dim' | 'italic' | 'underline' | 'blink' | 'inverse' | 'hidden' | 'strikethrough' | 'doubleunderline' |
        'framed' | 'overlined';
    /** `text` in ANSI styles; by default only when `stream` (process.stdout) shows colors. */
    function styleText(format: ForegroundColors | BackgroundColors | Modifiers | Array<ForegroundColors | BackgroundColors | Modifiers>,
        text: string, options?: { validateStream?: boolean; stream?: import('stream').Writable }): string;

    /** The name of a negative errno, e.g. -2 → 'ENOENT'. */
    function getSystemErrorName(errno: number): string;
    /** Deprecated: use Array.isArray. */
    function isArray(value: unknown): value is unknown[];
    /** Replace lone surrogates with U+FFFD. */
    function toUSVString(string: string): string;

    interface ParseArgsOptionConfig {
        type: 'string' | 'boolean';
        /** A one-letter alias: `-f`. */
        short?: string;
        /** Allow the option more than once; its value is then an array. */
        multiple?: boolean;
        default?: string | boolean | string[] | boolean[];
    }
    interface ParseArgsConfig {
        /** Default: process.argv without the executable and script. */
        args?: string[];
        options?: Record<string, ParseArgsOptionConfig>;
        /** Throw on unknown options and wrong types (default true). */
        strict?: boolean;
        /** Default: the opposite of `strict`. */
        allowPositionals?: boolean;
        /** Accept `--no-flag` for boolean options. */
        allowNegative?: boolean;
        /** Also return how each argument was read. */
        tokens?: boolean;
    }
    type ParseArgsToken =
        | { kind: 'option'; index: number; name: string; rawName: string; value: string | undefined; inlineValue: boolean | undefined }
        | { kind: 'positional'; index: number; value: string }
        | { kind: 'option-terminator'; index: number };
    /** Parse command line arguments (Node.js's util.parseArgs). */
    function parseArgs(config?: ParseArgsConfig): {
        values: Record<string, string | boolean | Array<string | boolean> | undefined>;
        positionals: string[];
        tokens?: ParseArgsToken[];
    };

    /** The global TextEncoder and TextDecoder. */
    const TextEncoder: FrogJSTextEncoderClass;
    const TextDecoder: FrogJSTextDecoderClass;

    /** Checks for kinds of built-in objects, without relying on instanceof. */
    namespace types {
        function isExternal(value: unknown): boolean;
        function isDate(value: unknown): value is Date;
        function isArgumentsObject(value: unknown): value is IArguments;
        function isBigIntObject(value: unknown): value is BigInt;
        function isBooleanObject(value: unknown): value is Boolean;
        function isNumberObject(value: unknown): value is Number;
        function isStringObject(value: unknown): value is String;
        function isSymbolObject(value: unknown): value is Symbol;
        function isBoxedPrimitive(value: unknown): boolean;
        function isNativeError(value: unknown): value is Error;
        function isRegExp(value: unknown): value is RegExp;
        function isAsyncFunction(value: unknown): boolean;
        function isGeneratorFunction(value: unknown): boolean;
        function isGeneratorObject(value: unknown): value is Generator;
        function isPromise(value: unknown): value is Promise<unknown>;
        function isMap(value: unknown): value is Map<unknown, unknown>;
        function isSet(value: unknown): value is Set<unknown>;
        function isMapIterator(value: unknown): boolean;
        function isSetIterator(value: unknown): boolean;
        function isWeakMap(value: unknown): value is WeakMap<object, unknown>;
        function isWeakSet(value: unknown): value is WeakSet<object>;
        function isArrayBuffer(value: unknown): value is ArrayBuffer;
        function isSharedArrayBuffer(value: unknown): value is SharedArrayBuffer;
        function isAnyArrayBuffer(value: unknown): value is ArrayBuffer | SharedArrayBuffer;
        function isDataView(value: unknown): value is DataView;
        function isArrayBufferView(value: unknown): value is ArrayBufferView;
        function isTypedArray(value: unknown): boolean;
        function isUint8Array(value: unknown): value is Uint8Array;
        function isUint8ClampedArray(value: unknown): value is Uint8ClampedArray;
        function isUint16Array(value: unknown): value is Uint16Array;
        function isUint32Array(value: unknown): value is Uint32Array;
        function isInt8Array(value: unknown): value is Int8Array;
        function isInt16Array(value: unknown): value is Int16Array;
        function isInt32Array(value: unknown): value is Int32Array;
        function isFloat16Array(value: unknown): boolean;
        function isFloat32Array(value: unknown): value is Float32Array;
        function isFloat64Array(value: unknown): value is Float64Array;
        function isBigInt64Array(value: unknown): value is BigInt64Array;
        function isBigUint64Array(value: unknown): value is BigUint64Array;
        function isProxy(value: unknown): boolean;
        function isModuleNamespaceObject(value: unknown): boolean;
        /** Always false: FrogJS has no KeyObject. */
        function isKeyObject(value: unknown): boolean;
        /** Always false: FrogJS has no CryptoKey. */
        function isCryptoKey(value: unknown): boolean;
    }
}
