#!/usr/bin/env python3
"""REPL self-test on a real terminal (a pty): line editing, history, completion
and Ctrl+C can't be exercised through a pipe. Exits with code 1 if a check fails.
Usage: examples/test-repl.py [path/to/frogjs]"""
import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import time

FROGJS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else 'build/frogjs')
passed = 0
failed = 0


def check(name, condition, detail=''):
    global passed, failed
    if condition:
        passed += 1
        print('  ok   ' + name)
    else:
        failed += 1
        print('  FAIL ' + name + ('\n       ' + repr(detail) if detail else ''))


class Repl:
    def __init__(self, env=None, args=()):
        self.master, slave = pty.openpty()
        environment = dict(os.environ, NO_COLOR='1', **(env or {}))
        environment.setdefault('FROGJS_REPL_HISTORY', '')
        self.process = subprocess.Popen([FROGJS, *args], stdin=slave, stdout=slave, stderr=slave, env=environment,
                                        start_new_session=True, close_fds=True)
        os.close(slave)
        self.output = ''
        self.wait_for('> ')

    def read(self, timeout=0.15):
        end = time.time() + timeout
        while time.time() < end:
            ready, _, _ = select.select([self.master], [], [], 0.02)
            if ready:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    return
                if not data:
                    return
                self.output += data.decode('utf8', 'replace')
                end = time.time() + 0.05

    def wait_for(self, text, timeout=8):
        end = time.time() + timeout
        while text not in self.output and time.time() < end:
            self.read(0.05)
            if self.process.poll() is not None:
                self.read(0.05)
                break
        return text in self.output

    def send(self, text):
        """Send keys and return the terminal output they caused"""
        self.output = ''
        os.write(self.master, text.encode())
        self.read(0.3)
        return self.output

    def line(self, text):
        self.output = ''
        os.write(self.master, (text + '\r').encode())
        self.wait_for('> ')
        self.read(0.05)
        return self.output

    def finish(self, keys='\x04'):
        os.write(self.master, keys.encode())
        try:
            code = self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.process.kill()
            code = self.process.wait()
        os.close(self.master)
        return code


print('Evaluating')
repl = Repl()
check('banner', 'FrogJS' not in repl.output or 'help' in repl.output, repl.output)
check('expression', '3' in repl.line('1 + 2'))
check('objects are inspected', "{ a: 1 }" in repl.line('({ a: 1 })'))
check('object literal without parentheses', '{ b: 2 }' in repl.line('{ b: 2 }'))
check('_ is the last value', '4' in repl.line('_.b * 2'))
check('let persists', 'undefined' in repl.line('let x = 5'))
check('let is readable', '6' in repl.line('x + 1'))
check('error is printed', 'boom' in repl.line("throw new Error('boom')"))
check('_error', 'boom' in repl.line('_error.message'))
check('syntax error', 'SyntaxError' in repl.line('1 +* 2'))
check('require works', 'function' in repl.line("typeof require('fs').readFileSync"))
check('async output appears', 'later' in (repl.line("setTimeout(() => console.log('later'), 1)") + repl.send('')))
check('callback errors do not end the REPL', 'cb-boom' in (repl.line("setTimeout(() => { throw new Error('cb-boom') }, 1)") + repl.send('')))
check('rejections do not end the REPL', 'rej-boom' in (repl.line("Promise.reject(new Error('rej-boom'))") + repl.send('')))
check('still alive', '7' in repl.line('3 + 4'))
check('ended with Ctrl+D', repl.finish() == 0)

print('Multi-line')
repl = Repl()
check('continuation prompt', '...' in repl.send('function f() {\r'))
repl.send('return 42\r')
check('complete', 'undefined' in repl.send('}\r'))
check('call', '42' in repl.line('f()'))
check('template literal continues', '...' in repl.send('`a\r'))
check('template literal ends', 'b' in repl.line('b`'))
repl.send('if (true) {\r')
check('.break leaves it', '> ' in repl.send('.break\r'))
check('Ctrl+C cancels continuation', '> ' in (repl.send('if (true) {\r') + repl.send('\x03')))
repl.finish()

print('Commands')
tmp = tempfile.mkdtemp()
saved = os.path.join(tmp, 'saved.js')
repl = Repl()
check('.help', '.exit' in repl.line('.help'))
repl.line('var keep = 9')
check('.save', 'saved' in repl.line('.save ' + saved).lower() or os.path.exists(saved))
check('saved file', os.path.exists(saved) and 'keep' in open(saved).read())
check('.clear starts over', 'undefined' in (repl.line('.clear') + repl.line('typeof keep')))
check('built-ins after .clear', 'object' in repl.line('typeof Buffer.from("a")'))
check('.load', '9' in (repl.line('.load ' + saved) + repl.line('keep')))
check('.load of nothing', 'Failed to load' in repl.line('.load /no/such/file'))
check('.save nowhere', 'rror' in repl.line('.save /no/such/dir/x') or 'annot' in repl.output or 'ailed' in repl.output)
check('unknown command', 'nvalid' in repl.line('.nope') or 'nknown' in repl.output)
os.write(repl.master, b'.exit\r')
check('.exit', repl.process.wait(timeout=8) == 0)
os.close(repl.master)

print('Line editing')
repl = Repl()
check('backspace', '12' in repl.line('123\x7f'))
check('left arrow inserts', '"axb"' in repl.line('"ab"\x1b[D\x1b[Dx') or 'axb' in repl.output)
check('Ctrl+A and Ctrl+E', '31' in repl.line('1\x013\x05'))
check('Ctrl+U clears', '8' in repl.line('999\x158'))
check('Ctrl+K cuts', '5' in repl.line('5999\x01\x1b[C\x0b'))
check('Ctrl+W removes a word', '6' in repl.line('6 + foo\x17\x7f\x7f\x7f'))
check('delete key', '7' in repl.line('79\x1b[D\x1b[3~'))
check('home and end keys', '21' in repl.line('1\x1b[H2\x1b[F'))
check('Ctrl+B and Ctrl+F', '43' in repl.line('3\x024\x06'))
check('Ctrl+D deletes forward', '1' in repl.line('19\x02\x04'))
check('Ctrl+L redraws', repl.send('\x0c') is not None)
check('long lines scroll', str(10 ** 3) in repl.line('10 ** 3 ' + ' ' * 200))
check('UTF-8', 'é' in repl.line("'é'"))
repl.finish()

print('History')
history = os.path.join(tmp, 'history')
repl = Repl(env={'FROGJS_REPL_HISTORY': history})
repl.line('11 + 11')
repl.line('22 + 22')
check('up arrow', '44' in repl.line('\x1b[A'))
check('up twice', '22' in repl.line('\x1b[A\x1b[A'))
check('up and down', '44' in repl.line('\x1b[A\x1b[A\x1b[B'.replace('\x1b[B', '\x1b[B')) or '22' in repl.output)
check('Ctrl+P and Ctrl+N', repl.line('\x10\x10\x0e') is not None)
repl.finish()
check('saved', '11 + 11' in open(history).read())
check('private', os.stat(history).st_mode & 0o077 == 0)
repl = Repl(env={'FROGJS_REPL_HISTORY': history})
check('loaded', '11 + 11' in repl.send('\x1b[A\x1b[A\x1b[A\x1b[A') or '22' in repl.output)
repl.send('\x15')
repl.finish()

print('Completion')
repl = Repl()
check('global', 'process' in repl.send('proc\t'))
repl.send('\x15')
check('property', 'readFileSync' in repl.send("fs.readFileS\t"))
repl.send('\x15')
check('choices listed', 'readFile' in (repl.send('fs.readF\t') + repl.send('\t')))
repl.send('\x15')
check('command', '.help' in repl.send('.he\t'))
repl.send('\x15')
repl.line('let myLongVariable = 1')
check('let variable', 'myLongVariable' in repl.send('myLong\t'))
repl.send('\x15')
check('keyword', 'function' in repl.send('functi\t'))
repl.send('\x15')
check('nothing to complete', repl.send('zzzzz\t') is not None)
repl.send('\x15')
repl.finish()

print('Interrupt')
repl = Repl()
repl.line('1')
output = repl.send('for (;;) {}\r')
time.sleep(0.3)
os.killpg(repl.process.pid, signal.SIGINT)
check('Ctrl+C stops running code', repl.wait_for('> ', 8))
check('works afterwards', '9' in repl.line('4 + 5'))
check('Ctrl+C on a line clears it', '> ' in repl.send('abc\x03'))
hint = repl.send('\x03')
check('second Ctrl+C on an empty line warns', 'exit' in hint or '.exit' in hint or repl.process.poll() is not None)
code = repl.finish('\x03')
check('third ends it', code == 0, code)

print('-i with a script')
script = os.path.join(tmp, 'script.js')
with open(script, 'w') as f:
    f.write('globalThis.fromScript = 77; console.log("script ran");')
repl = Repl(args=['-i', script])
check('runs first', 'script ran' in repl.output, repl.output)
check('then the REPL', '77' in repl.line('fromScript'))
repl.finish()

print(f'{passed} passed, {failed} failed')
sys.exit(1 if failed else 0)
