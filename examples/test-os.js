// Test os module
console.log('=== OS Tests ===');

// Test os.platform() and os.type()
console.log('os.platform():', os.platform());
console.log('os.platform() matches process.platform:', os.platform() === process.platform);
console.log('os.type():', os.type());

// Test os.arch() and os.release()
console.log('os.arch():', os.arch());
console.log('os.release():', os.release());

// Test os.hostname()
console.log('os.hostname():', os.hostname());
console.log('os.hostname() is non-empty:', os.hostname().length > 0);

// Test os.cpus()
const cpus = os.cpus();
console.log('os.cpus() count:', cpus.length);
console.log('os.cpus()[0].model:', cpus[0].model);
console.log('os.cpus()[0].speed (MHz):', cpus[0].speed);
console.log('os.cpus()[0] has times.user:', typeof cpus[0].times.user === 'number');

// Test os.totalmem() and os.freemem()
const totalMB = Math.round(os.totalmem() / 1024 / 1024);
const freeMB = Math.round(os.freemem() / 1024 / 1024);
console.log('os.totalmem() (MB):', totalMB);
console.log('os.freemem() (MB):', freeMB);
console.log('os.freemem() <= os.totalmem():', os.freemem() <= os.totalmem());

// Test os.homedir() and os.tmpdir()
console.log('os.homedir():', os.homedir());
console.log('os.homedir() is absolute:', path.isAbsolute(os.homedir()));
console.log('os.tmpdir():', os.tmpdir());
console.log('os.tmpdir() is absolute:', path.isAbsolute(os.tmpdir()));

// Test os.EOL
console.log('os.EOL is "\\n":', os.EOL === '\n');

// Test require('os')
console.log('require("os") returns the os module:', require('os') === os);

console.log('=== All OS Tests Complete ===');
