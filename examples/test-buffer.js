// Test Buffer class
console.log('=== Buffer Tests ===');

// Test Buffer.alloc()
const buf1 = Buffer.alloc(10);
console.log('Buffer.alloc(10) length:', buf1.length);
console.log('Buffer.alloc(10) should have length 10:', buf1.length === 10);

// Test Buffer.from() with string
const buf2 = Buffer.from('Hello');
console.log('Buffer.from("Hello") length:', buf2.length);
console.log('Buffer.from("Hello") toString():', buf2.toString());
console.log('Buffer.from("Hello") content:', buf2.toString() === 'Hello');

// Test Buffer.byteLength()
const byteLength = Buffer.byteLength('Hello');
console.log('Buffer.byteLength("Hello"):', byteLength);
console.log('Buffer.byteLength("Hello") === 5:', byteLength === 5);

// Test buf.write()
const buf3 = Buffer.alloc(10);
const written = buf3.write('World', 0);
console.log('buf.write("World", 0) returned:', written);
console.log('buf3.toString():', buf3.toString());
console.log('Written 5 bytes:', written === 5);
// Note: buf3 contains "World" followed by null bytes, so we slice for comparison
const writtenPart = buf3.slice(0, 5);
console.log('Written content correct:', writtenPart.toString() === 'World');

// Test buf.slice()
const buf4 = Buffer.from('Hello World');
const slice = buf4.slice(0, 5);
console.log('buf.slice(0, 5) toString():', slice.toString());
console.log('Slice is "Hello":', slice.toString() === 'Hello');

// Test Buffer.from() with array
const buf5 = Buffer.from([72, 101, 108, 108, 111]);
console.log('Buffer.from([72, 101, 108, 108, 111]) toString():', buf5.toString());
console.log('Array buffer is "Hello":', buf5.toString() === 'Hello');

console.log('=== All Buffer Tests Complete ===');
