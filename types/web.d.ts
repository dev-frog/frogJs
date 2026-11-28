// Web platform globals: URL, URLSearchParams, events, abort signals,
// DOMException, atob and btoa. They behave as in Node.js.

/** A parsed URL (the WHATWG URL Standard). Setting a part updates the rest. */
declare class URL {
    /** Throws a TypeError with code ERR_INVALID_URL if `input` doesn't parse. */
    constructor(input: string | URL, base?: string | URL);
    /** Whether `new URL(input, base)` would succeed. */
    static canParse(input: string | URL, base?: string | URL): boolean;
    /** `new URL(input, base)`, or null instead of throwing. */
    static parse(input: string | URL, base?: string | URL): URL | null;
    /** Throws: FrogJS has no Blob. */
    static createObjectURL(object: unknown): string;
    static revokeObjectURL(url: string): void;
    href: string;
    readonly origin: string;
    protocol: string;
    username: string;
    password: string;
    host: string;
    hostname: string;
    port: string;
    pathname: string;
    search: string;
    readonly searchParams: URLSearchParams;
    hash: string;
    toString(): string;
    toJSON(): string;
}

/** The query of a URL as a list of name/value pairs. */
declare class URLSearchParams implements Iterable<[string, string]> {
    constructor(init?: string | URLSearchParams | Record<string, string> | Iterable<readonly [string, string]>);
    /** Number of pairs. */
    readonly size: number;
    append(name: string, value: string): void;
    /** Remove every pair with this name (and this value, if given). */
    delete(name: string, value?: string): void;
    /** The first value for `name`, or null. */
    get(name: string): string | null;
    getAll(name: string): string[];
    has(name: string, value?: string): boolean;
    /** Replace every pair with this name by one pair. */
    set(name: string, value: string): void;
    /** Sort by name, keeping the order of pairs with the same name. */
    sort(): void;
    forEach(callback: (value: string, name: string, params: URLSearchParams) => void, thisArg?: unknown): void;
    entries(): IterableIterator<[string, string]>;
    keys(): IterableIterator<string>;
    values(): IterableIterator<string>;
    [Symbol.iterator](): IterableIterator<[string, string]>;
    /** `application/x-www-form-urlencoded`, without a leading `?`. */
    toString(): string;
}

interface EventInit {
    bubbles?: boolean;
    cancelable?: boolean;
    composed?: boolean;
}

declare class Event {
    constructor(type: string, options?: EventInit);
    static readonly NONE: 0;
    static readonly CAPTURING_PHASE: 1;
    static readonly AT_TARGET: 2;
    static readonly BUBBLING_PHASE: 3;
    readonly NONE: 0;
    readonly CAPTURING_PHASE: 1;
    readonly AT_TARGET: 2;
    readonly BUBBLING_PHASE: 3;
    readonly type: string;
    readonly bubbles: boolean;
    readonly cancelable: boolean;
    readonly composed: boolean;
    /** True after preventDefault() on a cancelable event. */
    readonly defaultPrevented: boolean;
    /** Milliseconds since the process started, when the event was made. */
    readonly timeStamp: number;
    /** False for events made by scripts. */
    readonly isTrusted: boolean;
    readonly target: EventTarget | null;
    readonly currentTarget: EventTarget | null;
    readonly srcElement: EventTarget | null;
    readonly eventPhase: number;
    returnValue: boolean;
    cancelBubble: boolean;
    composedPath(): EventTarget[];
    preventDefault(): void;
    stopPropagation(): void;
    /** Don't call the listeners after this one. */
    stopImmediatePropagation(): void;
}

declare class CustomEvent<T = any> extends Event {
    constructor(type: string, options?: EventInit & { detail?: T });
    readonly detail: T;
}

type EventListenerOrEventListenerObject = ((event: Event) => void) | { handleEvent(event: Event): void };

interface AddEventListenerOptions {
    capture?: boolean;
    /** Remove the listener after its first call. */
    once?: boolean;
    passive?: boolean;
    /** Remove the listener when this signal aborts. */
    signal?: AbortSignal;
}

declare class EventTarget {
    constructor();
    addEventListener(type: string, listener: EventListenerOrEventListenerObject | null, options?: AddEventListenerOptions | boolean): void;
    removeEventListener(type: string, listener: EventListenerOrEventListenerObject | null, options?: { capture?: boolean } | boolean): void;
    /** Call the listeners; false if one called preventDefault() on a cancelable event. */
    dispatchEvent(event: Event): boolean;
}

declare class AbortSignal extends EventTarget {
    /** A signal that is already aborted (default reason: an AbortError DOMException). */
    static abort(reason?: any): AbortSignal;
    /** A signal that aborts with a TimeoutError DOMException after `ms`. It doesn't keep the process alive. */
    static timeout(ms: number): AbortSignal;
    /** A signal that aborts when the first of `signals` does, with its reason. */
    static any(signals: Iterable<AbortSignal>): AbortSignal;
    readonly aborted: boolean;
    readonly reason: any;
    /** Throw `reason` if aborted. */
    throwIfAborted(): void;
    onabort: ((this: AbortSignal, event: Event) => any) | null;
}

declare class AbortController {
    constructor();
    readonly signal: AbortSignal;
    /** Abort the signal (default reason: an AbortError DOMException). */
    abort(reason?: any): void;
}

declare class DOMException extends Error {
    /** `options` is the name, or `{ name, cause }`. */
    constructor(message?: string, options?: string | { name?: string; cause?: unknown });
    readonly name: string;
    readonly message: string;
    /** The legacy code for the name (0 for newer names such as AbortError). */
    readonly code: number;
    static readonly INDEX_SIZE_ERR: 1;
    static readonly DOMSTRING_SIZE_ERR: 2;
    static readonly HIERARCHY_REQUEST_ERR: 3;
    static readonly WRONG_DOCUMENT_ERR: 4;
    static readonly INVALID_CHARACTER_ERR: 5;
    static readonly NO_DATA_ALLOWED_ERR: 6;
    static readonly NO_MODIFICATION_ALLOWED_ERR: 7;
    static readonly NOT_FOUND_ERR: 8;
    static readonly NOT_SUPPORTED_ERR: 9;
    static readonly INUSE_ATTRIBUTE_ERR: 10;
    static readonly INVALID_STATE_ERR: 11;
    static readonly SYNTAX_ERR: 12;
    static readonly INVALID_MODIFICATION_ERR: 13;
    static readonly NAMESPACE_ERR: 14;
    static readonly INVALID_ACCESS_ERR: 15;
    static readonly VALIDATION_ERR: 16;
    static readonly TYPE_MISMATCH_ERR: 17;
    static readonly SECURITY_ERR: 18;
    static readonly NETWORK_ERR: 19;
    static readonly ABORT_ERR: 20;
    static readonly URL_MISMATCH_ERR: 21;
    static readonly QUOTA_EXCEEDED_ERR: 22;
    static readonly TIMEOUT_ERR: 23;
    static readonly INVALID_NODE_TYPE_ERR: 24;
    static readonly DATA_CLONE_ERR: 25;
}

// For modules that export the same classes ('url')
type FrogJSURLClass = typeof URL;
type FrogJSURL = URL;
type FrogJSURLSearchParamsClass = typeof URLSearchParams;
type FrogJSURLSearchParams = URLSearchParams;

/** Decode base64 into a string of Latin-1 characters (throws an InvalidCharacterError DOMException). */
declare function atob(data: string): string;
/** Encode a string of Latin-1 characters as base64. */
declare function btoa(data: string): string;
