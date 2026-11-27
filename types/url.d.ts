declare module 'url' {
    import { ParsedUrlQuery } from 'querystring';

    /** The global URL class. */
    const URL: FrogJSURLClass;
    type URL = FrogJSURL;
    /** The global URLSearchParams class. */
    const URLSearchParams: FrogJSURLSearchParamsClass;
    type URLSearchParams = FrogJSURLSearchParams;

    /** The legacy URL object of url.parse(). Parts that are missing are null. */
    class Url {
        protocol: string | null;
        slashes: boolean | null;
        auth: string | null;
        host: string | null;
        port: string | null;
        hostname: string | null;
        hash: string | null;
        search: string | null;
        query: string | null | ParsedUrlQuery;
        pathname: string | null;
        path: string | null;
        href: string;
        parse(url: string, parseQueryString?: boolean, slashesDenoteHost?: boolean): this;
        parseHost(): void;
        format(): string;
        resolve(relative: string): string;
        resolveObject(relative: string | Url): Url;
    }

    interface UrlObject {
        protocol?: string | null;
        slashes?: boolean | null;
        auth?: string | null;
        host?: string | null;
        hostname?: string | null;
        port?: string | number | null;
        pathname?: string | null;
        search?: string | null;
        query?: string | null | Record<string, unknown>;
        hash?: string | null;
        href?: string | null;
    }

    /** Legacy parser (prefer `new URL()`). `parseQueryString` makes `query` an object. */
    function parse(url: string): Url & { query: string | null };
    function parse(url: string, parseQueryString: true, slashesDenoteHost?: boolean): Url & { query: ParsedUrlQuery };
    function parse(url: string, parseQueryString?: boolean, slashesDenoteHost?: boolean): Url;
    /** Turn a URL object, legacy object or string into a string. */
    function format(url: URL, options?: { auth?: boolean; fragment?: boolean; search?: boolean; unicode?: boolean }): string;
    function format(url: string | UrlObject): string;
    /** Resolve `to` against `from` the way a browser resolves a link. */
    function resolve(from: string, to: string): string;
    function resolveObject(from: string | Url, to: string | Url): Url;

    /** Punycode form of a domain ('' if it isn't valid). */
    function domainToASCII(domain: string): string;
    /** Unicode form of a domain ('' if it isn't valid). */
    function domainToUnicode(domain: string): string;
    /** An absolute `file:` URL for a path, with characters such as `%` and `#` escaped. */
    function pathToFileURL(path: string): URL;
    /** The path of a `file:` URL (throws for other schemes). */
    function fileURLToPath(url: string | URL): string;
    /** The options http.request() takes, from a URL. */
    function urlToHttpOptions(url: URL): {
        protocol: string;
        hostname: string;
        hash: string;
        search: string;
        pathname: string;
        path: string;
        href: string;
        port?: number;
        auth?: string;
    };
}
