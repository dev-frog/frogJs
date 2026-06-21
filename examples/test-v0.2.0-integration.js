// Comprehensive integration test for FrogJS v0.2.0
console.log('=== FrogJS v0.2.0 Integration Test ===');

// ============================================
// Test 1: Process + Buffer Integration
// ============================================
console.log('\n--- Test 1: Process + Buffer ---');
const exePath = process.argv[0];
const exeBuf = Buffer.from(exePath);
console.log('Executable path:', exePath);
console.log('Executable buffer length:', exeBuf.length);
console.log('Buffer from path:', exeBuf.toString());

// ============================================
// Test 2: Buffer + FS Integration
// ============================================
console.log('\n--- Test 2: Buffer + FS ---');
const testData = Buffer.from('Hello, v0.2.0!');
fs.writeFileSync('test-v0.2.0.txt', testData.toString());
const readData = fs.readFileSync('test-v0.2.0.txt');
console.log('Written and read:', readData);
console.log('Data matches:', readData === 'Hello, v0.2.0!');

// ============================================
// Test 3: Enhanced FS + Process Integration
// ============================================
console.log('\n--- Test 3: Enhanced FS + Process ---');
const testDir = 'test-v0.2.0-dir';
fs.mkdirSync(testDir, 0755);
console.log('Created directory:', testDir);
console.log('Directory exists:', fs.existsSync(testDir));

const stats = fs.statSync(testDir);
console.log('Directory isDirectory:', stats.isDirectory);
console.log('Directory size:', stats.size);

const files = fs.readdirSync('.');
console.log('Current directory contains files:', files.length > 0);

// ============================================
// Test 4: Async Operations Integration
// ============================================
console.log('\n--- Test 4: Async Operations ---');

let asyncTestsComplete = 0;
const totalAsyncTests = 3;

// Async stat
fs.stat('test-v0.2.0.txt', (err, stats) => {
    if (err) {
        console.log('stat error:', err);
    } else {
        console.log('Async stat - size:', stats.size);
        console.log('Async stat - isFile:', stats.isFile);
    }
    asyncTestsComplete++;
    checkAsyncComplete();
});

// Async readdir
fs.readdir('.', (err, files) => {
    if (err) {
        console.log('readdir error:', err);
    } else {
        console.log('Async readdir - found', files.length, 'items');
    }
    asyncTestsComplete++;
    checkAsyncComplete();
});

// Async cleanup
setTimeout(() => {
    fs.unlink('test-v0.2.0.txt', (err) => {
        if (err) {
            console.log('unlink error:', err);
        } else {
            console.log('Cleaned up test file');
        }
        asyncTestsComplete++;

        fs.rmdir(testDir, (err) => {
            if (err) {
                console.log('rmdir error:', err);
            } else {
                console.log('Cleaned up test directory');
            }
            asyncTestsComplete++;
            checkAsyncComplete();
        });
    });
}, 100);

function checkAsyncComplete() {
    if (asyncTestsComplete >= totalAsyncTests + 1) { // +1 for rmdir
        console.log('\n=== All Integration Tests Complete ===');
    }
}

// ============================================
// Test 5: Process Environment Integration
// ============================================
console.log('\n--- Test 5: Process Environment ---');
console.log('Process platform:', process.platform);
console.log('Process version:', process.version);
console.log('Process pid:', process.pid);
console.log('Process cwd:', process.cwd());
console.log('Has HOME in env:', process.env.HOME !== undefined);

// ============================================
// Test 6: Buffer Operations Integration
// ============================================
console.log('\n--- Test 6: Buffer Operations ---');
const buf1 = Buffer.alloc(20);
const buf2 = Buffer.from('FrogJS v0.2.0');
buf1.write(buf2.toString(), 0);
const slice = buf1.slice(0, 10);
console.log('Buffer write and slice:', slice.toString());
console.log('Buffer byteLength test:', Buffer.byteLength('Test') === 4);

// ============================================
// Test 7: Existing Examples Still Work
// ============================================
console.log('\n--- Test 7: System Health Check ---');
console.log('setTimeout available:', typeof setTimeout === 'function');
console.log('setInterval available:', typeof setInterval === 'function');
console.log('console.log available:', typeof console.log === 'function');
console.log('fs available:', typeof fs.readFile === 'function');
console.log('Buffer available:', typeof Buffer.alloc === 'function');
console.log('process available:', typeof process.exit === 'function');
