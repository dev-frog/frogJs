// Modules that are other names for (parts of) other modules, and small
// modules that only give access to globals

declare module 'fs/promises' {
    import { promises } from 'fs';
    export = promises;
}

declare module 'stream/promises' {
    import { promises } from 'stream';
    export = promises;
}

declare module 'path/posix' {
    import path = require('path');
    export = path;
}

declare module 'perf_hooks' {
    /** The global `performance`. */
    const performance: typeof globalThis.performance;
}

declare module 'process' {
    /** The global `process`. */
    const process: FrogJS.Process;
    export = process;
}

declare module 'module' {
    /** Names of the built-in modules (each also loads as `node:name`). */
    const builtinModules: string[];
    /** A require() function that resolves relative to `filename` (a path or `file:` URL). */
    function createRequire(filename: string | URL): typeof require;
    /** Whether `name` is a built-in module, with or without `node:`. */
    function isBuiltin(name: string): boolean;
}
