// POSIX paths only.

declare module 'path' {
    /** `'/'` */
    const sep: string;
    /** `':'` (the PATH separator) */
    const delimiter: string;

    /** Join segments with `sep` and normalize the result (`'.'` for nothing). */
    function join(...paths: string[]): string;
    /** Resolve segments right to left into an absolute path (from process.cwd()). */
    function resolve(...paths: string[]): string;
    /** Collapse `.`, `..` and repeated separators. */
    function normalize(path: string): string;
    function dirname(path: string): string;
    /** The last segment, without `ext` if it ends with it. */
    function basename(path: string, ext?: string): string;
    /** `'.js'` for `'a/b.js'`, `''` when there is none. */
    function extname(path: string): string;
    function isAbsolute(path: string): boolean;
}
