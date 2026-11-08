// Every export the TypeScript declarations promise must be documented: compares
// them with docs/api-reference.md and exits with code 1 if a name is missing.
// Together with tools/check-types.js (declarations against the running runtime)
// this ties the documentation to what exists. Runs under Node.js.
const fs = require('fs');
const path = require('path');
const { declarations } = require('./check-types.js');

const root = path.join(__dirname, '..');
const reference = fs.readFileSync(path.join(root, 'docs', 'api-reference.md'), 'utf8');

const mentioned = (name) => new RegExp('(^|[^A-Za-z0-9_$])' + name.replace(/[$]/g, '\\$&') + '([^A-Za-z0-9_$]|$)').test(reference);

const missing = [];
let checked = 0;

function checkClass(label, info) {
  for (const method of (info && info.methods) || []) {
    checked++;
    if (!mentioned(method)) missing.push(`${label}.${method}`);
  }
}

const { modules, globals } = declarations();
for (const [name, entry] of Object.entries(modules)) {
  if (entry.self) checkClass(name, entry.self);
  for (const [key, info] of Object.entries(entry.exports)) {
    checked++;
    if (!mentioned(key)) missing.push(`${name}.${key}`);
    if (info.kind === 'class') checkClass(`${name}.${key}`, info);
  }
}
for (const [name, info] of Object.entries(globals)) {
  checked++;
  if (!mentioned(name)) missing.push(name);
  if (info.kind === 'class') checkClass(name, info);
}

if (missing.length > 0) {
  console.error(`Not in docs/api-reference.md (${missing.length} of ${checked}):`);
  for (const name of missing) console.error(`  ${name}`);
  process.exit(1);
}
console.log(`docs/api-reference.md mentions all ${checked} declared names`);
