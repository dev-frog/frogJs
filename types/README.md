# FrogJS type definitions

TypeScript declarations for the FrogJS globals (`console`, `process`, `Buffer`, timers, `require`, `__dirname`, ...) and its built-in modules: `fs`, `net`, `http`, `path`, `os`, `events`, `stream`, `util`, `crypto`, `child_process`, `worker_threads`.

They describe what FrogJS implements, not Node.js. Where the two differ the types follow FrogJS, so the compiler catches code that would fail at run time. For example:
- `fs` callbacks and the `*Sync` functions report errors as strings, and file contents are always strings.
- `fs.Stats.isFile` and `isDirectory` are booleans, not methods.
- Buffers can't be indexed (`buf[0]`).
- net sockets are not streams.
- `setTimeout` needs a delay.

## Use them

FrogJS runs JavaScript, so TypeScript code has to be compiled first: use CommonJS output. You can also type-check plain JavaScript with `checkJs`.

```jsonc
// tsconfig.json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "CommonJS",
    "lib": ["ES2022"],          // no "DOM": FrogJS has its own console and crypto
    "types": [],                // keep @types/node out
    "strict": true,
    "outDir": "dist"
  },
  "include": ["src", "path/to/frogjs/types/index.d.ts"]
}
```

```typescript
import fs = require('fs');
import { spawn } from 'child_process';

const config: string = fs.readFileSync('config.json');
spawn('ls', ['-l']).stdout.pipe(process.stdout);
```

Run the result with `frogjs dist/main.js`. FrogJS exposes no ES modules (`import ... from` compiled to CommonJS works, native `import` doesn't). `module.exports`/`exports` exist in files loaded with `require()`, but not in the main script, so compile the main file to a script that only `require()`s the others.

## Keeping them correct

- `types/test/` holds files that use the API. `npm run typecheck` compiles them with `--strict`, and also checks that the mistakes in `types/test/errors.ts` are rejected.
- `npm run check-types` (`node tools/check-types.js`) compares the declarations with the running FrogJS. It loads every module in `build/frogjs`, reports each declared export that doesn't exist, and lists exports that aren't declared.

Update the declarations together with the C++/JavaScript and `docs/api-reference.md` whenever an API changes.
