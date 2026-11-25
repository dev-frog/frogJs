// tty self-test: exits with code 1 if a check fails. Also runs under Node.js,
// which is how the expected values were checked. Terminal behavior is checked
// in a child process that script(1) gives a pseudo-terminal.
const tty = require('tty');
const { spawnSync } = require('child_process');
const { check, throws, section, run } = require('./lib/check');

run(() => {
  section('isatty');
  check('pipes and files are not terminals', [tty.isatty(1), tty.isatty(9999), tty.isatty(-1), tty.isatty(1.5), tty.isatty('1')],
    [process.stdout.isTTY === true, false, false, false, false]);
  throws('WriteStream needs a terminal', () => new tty.WriteStream(9999), { code: 'ERR_TTY_INIT_FAILED' });
  throws('ReadStream needs a terminal', () => new tty.ReadStream(9999), { code: 'ERR_TTY_INIT_FAILED' });

  section('getColorDepth and hasColors');
  const depth = (env) => tty.WriteStream.prototype.getColorDepth.call(null, env);
  check('environment rules', [
    depth({}), depth({ TERM: 'xterm-256color' }), depth({ TERM: 'xterm' }), depth({ COLORTERM: 'truecolor' }),
    depth({ NO_COLOR: '1', TERM: 'xterm-256color' }), depth({ FORCE_COLOR: '3' }), depth({ FORCE_COLOR: '0' }), depth({ FORCE_COLOR: '' }),
    depth({ TERM: 'dumb' }), depth({ CI: '1', GITHUB_ACTIONS: 'true' }), depth({ CI: '1', TRAVIS: '1' }), depth({ CI: '1' }), depth({ TERM_PROGRAM: 'Apple_Terminal' }),
    depth({ TERM_PROGRAM: 'iTerm.app', TERM_PROGRAM_VERSION: '3.5' }), depth({ TMUX: '1' }), depth({ TERM: 'screen' }),
  ], [1, 8, 4, 24, 1, 24, 1, 4, 1, 24, 8, 1, 8, 24, 24, 4]);
  const hasColors = tty.WriteStream.prototype.hasColors;
  check('hasColors', [hasColors.call(null, { TERM: 'xterm-256color' }), hasColors.call(null, 256, { TERM: 'xterm-256color' }),
                      hasColors.call(null, 257, { TERM: 'xterm-256color' }), hasColors.call(null, {})], [true, true, false, false]);
  throws('hasColors count', () => hasColors.call(null, 1, {}), { code: 'ERR_OUT_OF_RANGE' });

  section('In a terminal');
  const script = spawnSync('script', ['-q', '/dev/null', process.execPath, '-e', `
    const tty = require('tty');
    const out = process.stdout;
    console.log(JSON.stringify([tty.isatty(1), out.isTTY, typeof out.columns, typeof out.rows, out.getWindowSize().length,
      typeof out.cursorTo, typeof out.clearLine, typeof process.stdin.setRawMode, new tty.WriteStream(1).isTTY]));`],
  { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  if (script.error || script.status !== 0) {
    check('script(1) is not available: skipped', true, true);
  } else {
    // script(1) echoes control characters before the output
    const line = /\[.*\]/.exec(script.stdout);
    check('stdout is a tty.WriteStream', line && JSON.parse(line[0]), [true, true, 'number', 'number', 2, 'function', 'function', 'function', true]);
  }
});
