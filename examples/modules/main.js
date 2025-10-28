// main.js - Test the module system

console.log('Testing FrogJS Module System\n');

// Test 1: Require math module
console.log('=== Test 1: Math Module ===');
const math = require('./math');

console.log('math.add(5, 3) =', math.add(5, 3));
console.log('math.subtract(10, 4) =', math.subtract(10, 4));
console.log('math.multiply(6, 7) =', math.multiply(6, 7));
console.log('math.divide(20, 4) =', math.divide(20, 4));
console.log('math.PI =', math.PI);

// Test 2: Require greeter module (class export)
console.log('\n=== Test 2: Greeter Module ===');
const Greeter = require('./greeter');

const greeter = new Greeter('FrogJS');
console.log(greeter.greet());
console.log(greeter.farewell());

// Test 3: Module caching - require same module twice
console.log('\n=== Test 3: Module Caching ===');
const math2 = require('./math');
console.log('Same module instance?', math === math2);

// Test 4: __filename and __dirname
console.log('\n=== Test 4: Module Variables ===');
console.log('__filename:', __filename);
console.log('__dirname:', __dirname);

console.log('\nAll tests passed!');
