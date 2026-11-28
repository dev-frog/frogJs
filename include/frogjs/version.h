#pragma once

// Single source of the FrogJS version (process.versions.frogjs, --version, REPL banner)
#define FROGJS_VERSION "1.1.0"

// The Node.js version whose API FrogJS follows and tests against. It is
// process.version and process.versions.node, as in other runtimes that follow
// Node.js: packages check it (yargs-parser refuses to run below v10), and
// "v1.1.0" would read as a Node.js that old.
#define FROGJS_NODE_VERSION "24.3.0"
