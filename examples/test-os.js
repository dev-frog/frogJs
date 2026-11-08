// os self-test: exits with code 1 if a check fails. Also runs under Node.js.
const os = require('os');
const { check, section, run } = require('./lib/check');

run(() => {
  section('Identity');
  check('platform', os.platform(), process.platform);
  check('arch', os.arch(), process.arch);
  check('type', typeof os.type() === 'string' && os.type().length > 0, true);
  check('release', typeof os.release() === 'string' && os.release().length > 0, true);
  check('hostname', typeof os.hostname() === 'string' && os.hostname().length > 0, true);
  check('EOL', os.EOL, '\n');

  section('Directories');
  check('homedir', os.homedir().startsWith('/'), true);
  check('tmpdir', os.tmpdir().startsWith('/'), true);
  check('tmpdir has no trailing slash', os.tmpdir().endsWith('/'), false);

  section('Resources');
  check('totalmem', os.totalmem() > 0, true);
  check('freemem', os.freemem() > 0 && os.freemem() <= os.totalmem(), true);
  const cpus = os.cpus();
  check('cpus', Array.isArray(cpus) && cpus.length > 0, true);
  check('cpu model', typeof cpus[0].model, 'string');
  check('cpu speed', typeof cpus[0].speed, 'number');
  check('cpu times', Object.keys(cpus[0].times).sort(), ['idle', 'irq', 'nice', 'sys', 'user']);
});
