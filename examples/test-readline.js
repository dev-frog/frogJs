// readline and readline/promises self-test: exits with code 1 if a check
// fails. Also runs under Node.js, which is how the expected values were
// checked. Terminal editing is checked in a child that script(1) gives a
// pseudo-terminal.
const fs = require('fs');
const readline = require('readline');
const readlinePromises = require('readline/promises');
const { PassThrough } = require('stream');
const { spawnSync } = require('child_process');
const { check, rejects, throws, section, run, once } = require('./lib/check');

function collect(stream) {
  let text = '';
  stream.on('data', (chunk) => { text += chunk; });
  return () => text;
}

run(async () => {
  section('Lines');
  const input = new PassThrough();
  const rl = readline.createInterface({ input, crlfDelay: Infinity });
  const lines = [];
  rl.on('line', (line) => lines.push(line));
  input.write('a\r');
  input.write('\nb\r\nc\rd\n\ne');
  input.end('f');
  await once(rl, 'close');
  check('line endings, and the last line without one', lines, ['a', 'b', 'c', 'd', '', 'ef']);
  check('closed at the end of input', rl.closed, true);
  const fileLines = [];
  for await (const line of readline.createInterface({ input: fs.createReadStream(__filename), crlfDelay: Infinity })) fileLines.push(line);
  check('async iteration', fileLines.length, fs.readFileSync(__filename, 'utf8').split('\n').length - 1);
  const multibyte = new PassThrough();
  const multibyteLines = [];
  readline.createInterface({ input: multibyte }).on('line', (line) => multibyteLines.push(line));
  const euro = Buffer.from('€\n');
  multibyte.write(euro.subarray(0, 1));
  multibyte.end(euro.subarray(1));
  await new Promise((resolve) => setImmediate(resolve));
  check('a character split between chunks', multibyteLines, ['€']);

  section('question and prompt');
  const questionInput = new PassThrough();
  const output = new PassThrough();
  const shown = collect(output);
  const asking = readline.createInterface({ input: questionInput, output, prompt: '$ ' });
  const answer = new Promise((resolve) => asking.question('name? ', resolve));
  questionInput.write('bob\n');
  check('question', await answer, 'bob');
  check('the prompt comes back', asking.getPrompt(), '$ ');
  asking.prompt();
  const line = once(asking, 'line');
  questionInput.write('x\n');
  check('line after prompt', await line, ['x']);
  check('what was shown', shown(), 'name? $ ');
  asking.setPrompt('# ');
  check('setPrompt', asking.getPrompt(), '# ');
  asking.close();
  throws('closed', () => asking.prompt(), { code: 'ERR_USE_AFTER_CLOSE' });

  section('readline/promises');
  const promiseInput = new PassThrough();
  const promised = readlinePromises.createInterface({ input: promiseInput, output: new PassThrough() });
  setTimeout(() => promiseInput.write('42\n'), 5);
  check('question', await promised.question('? '), '42');
  const controller = new AbortController();
  const aborted = promised.question('again? ', { signal: controller.signal });
  controller.abort();
  await rejects('question with a signal', aborted, { name: 'AbortError', code: 'ABORT_ERR' });
  promised.close();
  await rejects('question after close', promised.question('x'), { code: 'ERR_USE_AFTER_CLOSE' });
  const commands = new PassThrough();
  const written = collect(commands);
  await new readlinePromises.Readline(commands).cursorTo(0).moveCursor(2, -1).clearLine(1).clearScreenDown().commit();
  check('Readline', written(), '\x1b[1G\x1b[2C\x1b[1A\x1b[0K\x1b[0J');

  section('Keypress events');
  const keys = new PassThrough();
  readline.emitKeypressEvents(keys);
  const pressed = [];
  keys.on('keypress', (str, key) => pressed.push([str, key.name, key.ctrl, key.meta, key.shift]));
  keys.write('a\x1b[A\x03\x1b[1;5C\x1bb\r Z\x7f\x1b[3~');
  await new Promise((resolve) => setTimeout(resolve, 10));
  check('decoded keys', pressed, [
    ['a', 'a', false, false, false], [undefined, 'up', false, false, false], ['\x03', 'c', true, false, false],
    [undefined, 'right', true, false, false], [undefined, 'b', false, true, false], ['\r', 'return', false, false, false],
    [' ', 'space', false, false, false], ['Z', 'z', false, false, true], ['\x7f', 'backspace', false, false, false],
    [undefined, 'delete', false, false, false],
  ]);

  section('Cursor functions');
  const cursor = new PassThrough();
  const moved = collect(cursor);
  readline.cursorTo(cursor, 3);
  readline.cursorTo(cursor, 1, 2);
  readline.moveCursor(cursor, -2, 3);
  readline.clearLine(cursor, 0);
  readline.clearScreenDown(cursor);
  check('escape sequences', moved(), '\x1b[4G\x1b[3;2H\x1b[2D\x1b[3B\x1b[2K\x1b[0J');

  section('A terminal');
  const script = `
    const rl = require('readline').createInterface({ input: process.stdin, output: process.stdout,
      completer: (line) => [['hello', 'help'].filter((c) => c.startsWith(line)), line] });
    rl.question('Q? ', (a) => {
      process.stdout.write('\\nANSWER=[' + a + '] ' + rl.terminal + '\\n');
      rl.on('line', (l) => process.stdout.write('LINE=[' + l + ']\\n'));
      rl.on('close', () => process.stdout.write('CLOSED\\n'));
      rl.prompt();
    });`;
  const keystrokes = ['abc\x7fd\x01X\x05Y\r', 'h', 'e', 'l', '\t', 'p', '\r', '\x04'];
  // script(1) reads the keystrokes from its stdin; a shell feeds them slowly
  const feeder = keystrokes.map((key) => `sleep 0.2; printf '${key.replace(/[\x00-\x1f\x7f]/g, (c) => '\\' + c.charCodeAt(0).toString(8).padStart(3, '0'))}'`).join('; ');
  const result = spawnSync('sh', ['-c', `(sleep 0.5; ${feeder}) | script -q /dev/null "$0" -e "$1"`, process.execPath, script],
    { encoding: 'utf8', timeout: 20000 });
  if (result.error || !/CLOSED/.test(result.stdout || '')) {
    check('script(1) is not available: skipped', true, true);
  } else {
    const events = result.stdout.replace(/\r/g, '').split('\n').map((text) => /(ANSWER|LINE|CLOSED).*/.exec(text)).filter(Boolean).map((match) => match[0]);
    check('editing, completion and Ctrl+D', events, ['ANSWER=[XabdY] true', 'LINE=[help]', 'CLOSED']);
  }
});
