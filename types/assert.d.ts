declare module 'assert' {
    /** Throw an AssertionError unless `value` is truthy (same as assert.ok). */
    function assert(value: unknown, message?: string | Error): asserts value;

    namespace assert {
        class AssertionError extends Error {
            constructor(options: {
                message?: string;
                actual?: unknown;
                expected?: unknown;
                operator?: string;
                stackStartFn?: Function;
            });
            actual: unknown;
            expected: unknown;
            operator: string;
            /** Whether the message was generated (not passed in). */
            generatedMessage: boolean;
            code: 'ERR_ASSERTION';
        }

        interface CallTrackerCall {
            thisArg: object;
            arguments: unknown[];
        }

        interface CallTrackerReportInformation {
            message: string;
            actual: number;
            expected: number;
            operator: string;
            stack: object;
        }

        /** Check that functions are called the expected number of times (deprecated in Node.js). */
        class CallTracker {
            calls(exact?: number): () => void;
            calls<F extends (...args: any[]) => any>(fn: F, exact?: number): F;
            getCalls(fn: Function): CallTrackerCall[];
            report(): CallTrackerReportInformation[];
            reset(fn?: Function): void;
            /** Throw if a tracked function wasn't called the expected number of times. */
            verify(): void;
        }

        /** An error class or constructor, a RegExp the message must match, a validation function, or an object whose properties must match. */
        type AssertPredicate = RegExp | (new (...args: any[]) => object) | ((thrown: unknown) => boolean) | object | Error;

        function fail(message?: string | Error): never;
        function ok(value: unknown, message?: string | Error): asserts value;
        /** Loose equality (`==`, with NaN equal to NaN). */
        function equal(actual: unknown, expected: unknown, message?: string | Error): void;
        function notEqual(actual: unknown, expected: unknown, message?: string | Error): void;
        /** Loose deep equality: prototypes are ignored and primitives compared with `==`. */
        function deepEqual(actual: unknown, expected: unknown, message?: string | Error): void;
        function notDeepEqual(actual: unknown, expected: unknown, message?: string | Error): void;
        /** `Object.is` equality. */
        function strictEqual<T>(actual: unknown, expected: T, message?: string | Error): asserts actual is T;
        function notStrictEqual(actual: unknown, expected: unknown, message?: string | Error): void;
        /** Deep equality with `Object.is` for primitives, and prototypes compared. */
        function deepStrictEqual<T>(actual: unknown, expected: T, message?: string | Error): asserts actual is T;
        function notDeepStrictEqual(actual: unknown, expected: unknown, message?: string | Error): void;
        /** Like deepStrictEqual, but `actual` may have properties `expected` doesn't. */
        function partialDeepStrictEqual(actual: unknown, expected: unknown, message?: string | Error): void;
        /** Expect `block` to throw (and the error to match `error`). */
        function throws(block: () => unknown, message?: string | Error): void;
        function throws(block: () => unknown, error: AssertPredicate, message?: string | Error): void;
        function doesNotThrow(block: () => unknown, message?: string | Error): void;
        function doesNotThrow(block: () => unknown, error: AssertPredicate, message?: string | Error): void;
        /** Expect the promise (or the one `block` returns) to reject. */
        function rejects(block: (() => Promise<unknown>) | Promise<unknown>, message?: string | Error): Promise<void>;
        function rejects(block: (() => Promise<unknown>) | Promise<unknown>, error: AssertPredicate, message?: string | Error): Promise<void>;
        function doesNotReject(block: (() => Promise<unknown>) | Promise<unknown>, message?: string | Error): Promise<void>;
        function doesNotReject(block: (() => Promise<unknown>) | Promise<unknown>, error: AssertPredicate, message?: string | Error): Promise<void>;
        /** Throw `value` if it is neither null nor undefined (for callback errors). */
        function ifError(value: unknown): asserts value is null | undefined;
        function match(value: string, regExp: RegExp, message?: string | Error): void;
        function doesNotMatch(value: string, regExp: RegExp, message?: string | Error): void;

        /** The same functions, with the loose ones (equal, deepEqual, ...) replaced by the strict ones. */
        const strict: typeof assert & {
            equal: typeof strictEqual;
            notEqual: typeof notStrictEqual;
            deepEqual: typeof deepStrictEqual;
            notDeepEqual: typeof notDeepStrictEqual;
            strict: typeof strict;
        };
    }

    export = assert;
}

declare module 'assert/strict' {
    import { strict } from 'assert';
    export = strict;
}
