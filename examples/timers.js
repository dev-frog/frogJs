console.log("Testing timers...");

// Test setTimeout
console.log("Setting timeout for 1000ms");
setTimeout(() => {
    console.log("Timeout fired after 1 second!");
}, 1000);

// Test setInterval
console.log("Setting interval for 500ms");
let count = 0;
const intervalId = setInterval(() => {
    count++;
    console.log(`Interval fired ${count} times`);

    if (count >= 3) {
        console.log("Clearing interval");
        clearInterval(intervalId);
    }
}, 500);

// Test clearTimeout
const timeoutId = setTimeout(() => {
    console.log("This should NOT print");
}, 2000);

setTimeout(() => {
    console.log("Clearing the 2-second timeout");
    clearTimeout(timeoutId);
}, 100);

console.log("Script execution complete, waiting for timers...");
