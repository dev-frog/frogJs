// Test Enhanced File System operations
// Note: fs is a global object in FrogJS, not a module
console.log('=== Enhanced FS Tests ===');

// Test 1: fs.mkdirSync and fs.existsSync
console.log('\n--- mkdir and existsSync tests ---');
fs.mkdirSync('testdir', 0755);
console.log('Created testdir');
console.log('testdir exists:', fs.existsSync('testdir'));

// Test 2: fs.statSync
console.log('\n--- statSync test ---');
const stats = fs.statSync('testdir');
console.log('testdir stats:');
console.log('  isDirectory:', stats.isDirectory);
console.log('  isFile:', stats.isFile);
console.log('  size:', stats.size);

// Test 3: fs.writeFileSync and fs.statSync for files
console.log('\n--- file stat test ---');
fs.writeFileSync('testdir/test.txt', 'Hello, Enhanced FS!');
const fileStats = fs.statSync('testdir/test.txt');
console.log('testdir/test.txt stats:');
console.log('  isDirectory:', fileStats.isDirectory);
console.log('  isFile:', fileStats.isFile);
console.log('  size:', fileStats.size);

// Test 4: fs.readdirSync
console.log('\n--- readdirSync test ---');
const files = fs.readdirSync('testdir');
console.log('Files in testdir:', files);

// Test 5: fs.unlinkSync
console.log('\n--- unlinkSync test ---');
fs.unlinkSync('testdir/test.txt');
console.log('Deleted testdir/test.txt');
console.log('testdir/test.txt exists:', fs.existsSync('testdir/test.txt'));

// Test 6: fs.rmdirSync
console.log('\n--- rmdirSync test ---');
files2 = fs.readdirSync('testdir');
console.log('Files before rmdir:', files2);
fs.rmdirSync('testdir');
console.log('Removed testdir');
console.log('testdir exists:', fs.existsSync('testdir'));

// Test 7: Async operations
console.log('\n--- Async operations test ---');

// Async mkdir
fs.mkdir('async-test-dir', 0755, (err) => {
    if (err) {
        console.log('mkdir error:', err);
    } else {
        console.log('mkdir async: created async-test-dir');

        // Async stat
        fs.stat('async-test-dir', (err, stats) => {
            if (err) {
                console.log('stat error:', err);
            } else {
                console.log('stat async: isDirectory =', stats.isDirectory);

                // Async readdir
                fs.readdir('async-test-dir', (err, files) => {
                    if (err) {
                        console.log('readdir error:', err);
                    } else {
                        console.log('readdir async: files =', files);

                        // Async rmdir
                        fs.rmdir('async-test-dir', (err) => {
                            if (err) {
                                console.log('rmdir error:', err);
                            } else {
                                console.log('rmdir async: removed async-test-dir');
                                console.log('\n=== All Enhanced FS Tests Complete ===');
                            }
                        });
                    }
                });
            }
        });
    }
});
