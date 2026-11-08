// Shared helpers of the self-tests. The tests run under FrogJS and Node.js, so
// this file only uses what both have.
const util = require('util');

let passed = 0;
let failed = 0;

function pass(name) {
  passed++;
  console.log('  ok   ' + name);
}

function fail(name, detail) {
  failed++;
  console.log('  FAIL ' + name + (detail ? '\n       ' + detail : ''));
}

function section(name) {
  console.log(name);
}

// Deep equality, with both values shown on failure
function check(name, actual, expected) {
  if (util.isDeepStrictEqual(actual, expected)) pass(name);
  else fail(name, `expected ${util.inspect(expected)}, got ${util.inspect(actual)}`);
}

// fn must throw an error with these properties ({ name, code, message: string
// or RegExp })
function matches(err, expected) {
  for (const key of Object.keys(expected)) {
    const want = expected[key];
    const have = err === null || err === undefined ? undefined : err[key];
    if (want instanceof RegExp ? !want.test(String(have)) : have !== want) {
      return `${key}: expected ${util.inspect(want)}, got ${util.inspect(have)}`;
    }
  }
  return null;
}

function throws(name, fn, expected = {}) {
  let result;
  try {
    result = fn();
  } catch (err) {
    const problem = matches(err, expected);
    if (problem) fail(name, problem + `\n       (${err && err.stack})`);
    else pass(name);
    return;
  }
  fail(name, `expected a throw, got ${util.inspect(result)}`);
}

async function rejects(name, promise, expected = {}) {
  let result;
  try {
    result = await promise;
  } catch (err) {
    const problem = matches(err, expected);
    if (problem) fail(name, problem);
    else pass(name);
    return;
  }
  fail(name, `expected a rejection, got ${util.inspect(result)}`);
}

// An error handed to a Node-style callback
function checkError(name, err, expected) {
  if (!err) return fail(name, `expected an error, got ${util.inspect(err)}`);
  const problem = matches(err, expected);
  if (problem) fail(name, problem);
  else pass(name);
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// A promise with the arguments of the event's next emission
function once(emitter, event) {
  return new Promise((resolve, reject) => {
    const onEvent = (...args) => {
      emitter.removeListener('error', onError);
      resolve(args);
    };
    const onError = (err) => {
      emitter.removeListener(event, onEvent);
      reject(err);
    };
    emitter.once(event, onEvent);
    if (event !== 'error') emitter.once('error', onError);
  });
}

// Run the test body, print the totals and set the exit code. A body waiting on
// something that never happens would end the process quietly when the event
// loop runs out of work: that is a failure too.
function run(body) {
  let finished = false;
  process.on('exit', () => {
    if (!finished) {
      console.log('FAIL: the test ended before it finished');
      process.exitCode = 1;
    }
  });
  Promise.resolve().then(body).catch((err) => fail('unexpected error', err && err.stack)).then(() => {
    finished = true;
    console.log(`${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
  });
}

module.exports = { check, throws, rejects, checkError, section, sleep, once, run, pass, fail };
