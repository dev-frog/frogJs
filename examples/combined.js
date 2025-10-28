console.log("=== FrogJS Feature Demo ===\n");

// 1. Console
console.log("1. Console: Hello from FrogJS!");
console.error("   Error messages work too!");

// 2. Timers
console.log("\n2. Timers:");
setTimeout(() => {
    console.log("   - setTimeout works!");
}, 100);

let intervalCount = 0;
const interval = setInterval(() => {
    intervalCount++;
    console.log(`   - setInterval tick ${intervalCount}`);
    if (intervalCount >= 2) {
        clearInterval(interval);
        console.log("   - clearInterval works!");
    }
}, 200);

// 3. File System (Sync)
console.log("\n3. File System (Sync):");
fs.writeFileSync("demo.txt", "FrogJS Demo File");
const syncContent = fs.readFileSync("demo.txt");
console.log("   - writeFileSync & readFileSync work!");
console.log("   - Content:", syncContent);

// 4. File System (Async)
console.log("\n4. File System (Async):");
fs.writeFile("demo-async.txt", "Async Demo", (err) => {
    if (!err) {
        console.log("   - writeFile works!");
        fs.readFile("demo-async.txt", (err, data) => {
            if (!err) {
                console.log("   - readFile works!");
                console.log("   - Content:", data);
            }
        });
    }
});

console.log("\n=== All features demonstrated! ===");
