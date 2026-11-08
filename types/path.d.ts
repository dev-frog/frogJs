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
    /** The relative path from `from` to `to`. */
    function relative(from: string, to: string): string;

    interface ParsedPath {
        root: string;
        dir: string;
        base: string;
        ext: string;
        name: string;
    }
    function parse(path: string): ParsedPath;
    /** The reverse of parse(). */
    function format(pathObject: Partial<ParsedPath>): string;
    /** Returns `path` (it only matters on Windows). */
    function toNamespacedPath(path: string): string;
    /** This module itself: FrogJS only has POSIX paths. */
    const posix: typeof import('path');
}
