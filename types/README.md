# FrogJS type definitions

TypeScript declarations for the FrogJS globals (`console`, `process`, `Buffer`, timers, `require`, `__dirname`, ...) and its built-in modules: `fs`, `net`, `http`, `path`, `os`, `events`, `stream`, `util`, `crypto`, `child_process`, `worker_threads`, `buffer`.

They describe what FrogJS implements, not all of Node.js: an API FrogJS lacks isn't declared, so the compiler catches code that would fail at run time.

## Use them

TypeScript 5.7 or newer is needed (`Uint8Array` became generic there).

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

const config: string = fs.readFileSync('config.json', 'utf8');
spawn('ls', ['-l']).stdout.pipe(process.stdout);
```

Run the result with `frogjs dist/main.js`. FrogJS has no ES modules (`import ... from` compiled to CommonJS works, native `import` doesn't).

## Keeping them correct

- `types/test/` holds files that use the API. `npm run typecheck` compiles them with `--strict`, and also checks that the mistakes in `types/test/errors.ts` are rejected.
- `npm run check-types` (`node tools/check-types.js`) compares the declarations with the running FrogJS. It loads every module in `build/frogjs`, reports each declared export that doesn't exist, and lists exports that aren't declared.

Update the declarations together with the C++/JavaScript and `docs/api-reference.md` whenever an API changes.
