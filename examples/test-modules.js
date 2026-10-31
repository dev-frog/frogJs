// Module system self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked.
const path = require('path');

let passed = 0;
let failed = 0;

function check(name, actual, expected) {
    if (actual === expected) {
        passed++;
        console.log(`  ✓ ${name}`);
    } else {
        failed++;
        console.error(`  ✗ ${name}\n      expected: ${JSON.stringify(expected)}\n      got:      ${JSON.stringify(actual)}`);
    }
}

const math = require('./modules/math');
check('require a relative file', math.add(2, 3), 5);
check('require with the .js extension', require('./modules/math.js'), math);
check('modules are cached', require('./modules/math'), math);

const utils = require('./modules/utils');
check('require a directory (index.js)', typeof utils.logger.log, 'function');
check('nested require resolves from the module directory', utils.answer, 42);
check('module __dirname', utils.dirname, path.join(__dirname, 'modules', 'utils'));
check('exports can be any value', require('./modules/utils/answer'), 42);
check('absolute path', require(path.join(__dirname, 'modules', 'math')), math);

check('__filename is absolute', __filename, path.join(__dirname, 'test-modules.js'));
check('main script has module.exports', typeof module.exports, 'object');
check('built-in modules', require('path'), path);

let error;
try {
    require('./modules/missing');
} catch (err) {
    error = err;
}
check('missing module', error && error.message.split('\n')[0], "Cannot find module './modules/missing'");

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
