// Events & Streams demo: a custom EventEmitter, a Transform that numbers lines,
// and an HTTP server that streams uploads through it with pipeline().
//
//   ./build/frogjs examples/events-streams.js      (PORT=8080 to change the port)
//   curl --data-binary @README.md http://localhost:3000/
const EventEmitter = require('events');
const { Readable, Writable, Transform, pipeline } = require('stream');
const http = require('http');

const PORT = Number(process.env.PORT) || 3000;

// --- EventEmitter --------------------------------------------------------
class Job extends EventEmitter {
    run(steps) {
        let done = 0;
        const timer = setInterval(() => {
            done++;
            this.emit('progress', done, steps);
            if (done === steps) {
                clearInterval(timer);
                this.emit('done');
            }
        }, 10);
        return this;
    }
}

// --- Transform -----------------------------------------------------------
// Numbers each line; keeps a partial last line until the next chunk arrives
function lineNumberer() {
    let line = 0;
    let partial = '';
    return new Transform({
        transform(chunk, encoding, callback) {
            const lines = (partial + chunk).split('\n');
            partial = lines.pop();
            callback(null, lines.map((text) => `${String(++line).padStart(4)} | ${text}\n`).join(''));
        },
        flush(callback) {
            callback(null, partial ? `${String(++line).padStart(4)} | ${partial}\n` : null);
        },
    });
}

const job = new Job();
job.on('progress', (done, total) => console.log(`job: step ${done}/${total}`));
job.once('done', () => {
    console.log('job: done\n');

    // Stream a few lines (split mid-line on purpose) through the transform
    const poem = Readable.from(['Streams flow\nchunk by ', 'chunk,\nback', 'pressure keeps\nthem calm\n']);
    let output = '';
    const collector = new Writable({
        write(chunk, encoding, callback) {
            output += chunk;
            callback();
        },
    });
    pipeline(poem, lineNumberer(), collector, (err) => {
        if (err) return console.error('pipeline failed:', err.message);
        console.log(output);
        startServer();
    });
});
job.run(3);

// --- HTTP + streams --------------------------------------------------------
function startServer() {
    const server = http.createServer((req, res) => {
        if (req.method !== 'POST') {
            res.end('POST a text file to get it back with line numbers\n');
            return;
        }
        res.setHeader('Content-Type', 'text/plain; charset=utf-8');
        // The request body streams through the transform into the response
        pipeline(req, lineNumberer(), res, (err) => {
            if (err) console.error('request failed:', err.message);
        });
    });

    server.listen(PORT, () => {
        console.log(`Listening on http://localhost:${PORT}`);
        console.log(`Try: curl --data-binary @README.md http://localhost:${PORT}/`);
    });
}
