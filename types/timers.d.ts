declare module 'timers' {
    /** The global timer functions. */
    const setTimeout: typeof globalThis.setTimeout;
    const clearTimeout: typeof globalThis.clearTimeout;
    const setInterval: typeof globalThis.setInterval;
    const clearInterval: typeof globalThis.clearInterval;
    const setImmediate: typeof globalThis.setImmediate;
    const clearImmediate: typeof globalThis.clearImmediate;
    /** `require('timers/promises')` */
    const promises: typeof import('timers/promises');
}

declare module 'timers/promises' {
    interface TimerOptions {
        /** false: don't keep the process alive while waiting (default true). */
        ref?: boolean;
        /** Aborting rejects the promise with an AbortError. */
        signal?: AbortSignal;
    }

    /** Resolve with `value` after `delay` ms. */
    function setTimeout<T = void>(delay?: number, value?: T, options?: TimerOptions): Promise<T>;
    /** Resolve with `value` after the I/O of the current loop iteration. */
    function setImmediate<T = void>(value?: T, options?: TimerOptions): Promise<T>;
    /** Yield `value` every `delay` ms: `for await (const v of setInterval(100, v))`. */
    function setInterval<T = void>(delay?: number, value?: T, options?: TimerOptions): AsyncIterableIterator<T>;

    const scheduler: {
        /** Same as setTimeout(delay, undefined, options). */
        wait(delay: number, options?: { signal?: AbortSignal }): Promise<void>;
        /** Same as setImmediate(). */
        yield(): Promise<void>;
    };
}
