// Mistakes the FrogJS declarations must reject. The line after each
// expect-error comment has to be a type error, or tsc fails with "Unused
// '@ts-expect-error' directive". Not run.
import fs = require('fs');
import { spawn } from 'child_process';
import { Worker } from 'worker_threads';
import crypto = require('crypto');
import net = require('net');

export function mistakes(): void {
    // @ts-expect-error  Stats.isDirectory is a boolean in FrogJS, not a method
    fs.statSync('.').isDirectory();
    // @ts-expect-error  fs errors are strings: no .code
    fs.readFile('x', (err) => err?.code);
    // @ts-expect-error  readFileSync takes no encoding (it always returns a string)
    fs.readFileSync('x', 'utf8');
    // @ts-expect-error  fs.promises doesn't exist
    fs.promises.readFile('x');
    // @ts-expect-error  Buffers can't be indexed
    Buffer.from('a')[0];
    // @ts-expect-error  setTimeout needs a delay
    setTimeout(() => {});
    // @ts-expect-error  no unref() on timer IDs
    setTimeout(() => {}, 1).unref();
    // @ts-expect-error  digest() without an encoding is a Buffer
    const hex: string = crypto.createHash('md5').update('a').digest();
    // @ts-expect-error  sockets aren't streams
    net.connect(80).pipe(process.stdout);
    // @ts-expect-error  kill() takes a signal name or number
    spawn('ls').kill('TERM_PLEASE');
    // @ts-expect-error  terminate() returns a Promise, not a number
    const code: number = new Worker('0', { eval: true }).terminate();
    // @ts-expect-error  process isn't an EventEmitter in FrogJS
    process.on('exit', () => {});
    // @ts-expect-error  no DOM: fetch doesn't exist
    fetch('http://example.com');
    void hex;
    void code;
}
