declare module 'querystring' {
    /** A parsed query: a name that appears more than once has an array of values. */
    interface ParsedUrlQuery {
        [name: string]: string | string[] | undefined;
    }

    type StringifiableValue = string | number | bigint | boolean | readonly string[] | readonly number[] |
        readonly bigint[] | readonly boolean[] | null | undefined;

    interface ParseOptions {
        /** Most pairs parsed (default 1000; 0 for no limit). */
        maxKeys?: number;
        /** Used instead of `unescape`. */
        decodeURIComponent?: (str: string) => string;
    }

    interface StringifyOptions {
        /** Used instead of `escape`. */
        encodeURIComponent?: (str: string) => string;
    }

    /** Parse `a=1&b=2&b=3` (separators default to `&` and `=`). */
    function parse(str: string, sep?: string | null, eq?: string | null, options?: ParseOptions): ParsedUrlQuery;
    /** Serialize an object; arrays give one pair per value. Other values become ''. */
    function stringify(obj?: Record<string, StringifiableValue>, sep?: string | null, eq?: string | null, options?: StringifyOptions): string;
    /** Same as parse(). */
    const decode: typeof parse;
    /** Same as stringify(). */
    const encode: typeof stringify;
    /** Percent-encode the way stringify() does. */
    function escape(str: string): string;
    /** Percent-decode; malformed escapes are left as they are. */
    function unescape(str: string): string;
    function unescapeBuffer(str: string, decodeSpaces?: boolean): Buffer;
}
