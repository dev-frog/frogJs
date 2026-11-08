// Mistakes the FrogJS declarations must reject. The line after each
// expect-error comment has to be a type error, or tsc fails with "Unused
// '@ts-expect-error' directive". Not run.
import fs = require('fs');
import { spawn } from 'child_process';
import { Worker } from 'worker_threads';
import crypto = require('crypto');
import net = require('net');

export function mistakes(): void {
    // @ts-expect-error  Stats.isDirectory is a method, not a boolean
    const isDirectory: boolean = fs.statSync('.').isDirectory;
    // @ts-expect-error  fs errors have no .nope
    fs.readFile('x', (err) => err?.nope);
    // @ts-expect-error  without an encoding the result is a Buffer
    const text: string = fs.readFileSync('x');
    // @ts-expect-error  with an encoding the result is a string
    const bytes: Buffer = fs.readFileSync('x', 'utf8');
    // @ts-expect-error  fs.promises has no readFileSync
    fs.promises.readFileSync('x');
    // @ts-expect-error  Buffer elements are numbers
    const element: string = Buffer.from('a')[0];
    // @ts-expect-error  timers need a function
    setTimeout('code', 1);
    // @ts-expect-error  a Timeout isn't a number
    const id: number = setTimeout(() => {}, 1);
    // @ts-expect-error  net.connect needs a port
    net.connect();
    // @ts-expect-error  digest() without an encoding is a Buffer
    const hex: string = crypto.createHash('md5').update('a').digest();
    // @ts-expect-error  sockets have no send()
    net.connect(80).send('x');
    // @ts-expect-error  kill() takes a signal name or number
    spawn('ls').kill('TERM_PLEASE');
    // @ts-expect-error  terminate() returns a Promise, not a number
    const code: number = new Worker('0', { eval: true }).terminate();
    // @ts-expect-error  no process.binding
    process.binding('fs');
    // @ts-expect-error  no DOM: fetch doesn't exist
    fetch('http://example.com');
    void hex;
    void code;
    void isDirectory;
    void text;
    void bytes;
    void element;
    void id;
}
