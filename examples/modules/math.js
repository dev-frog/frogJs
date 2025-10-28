// math.js - A simple math utility module

function add(a, b) {
    return a + b;
}

function subtract(a, b) {
    return a - b;
}

function multiply(a, b) {
    return a * b;
}

function divide(a, b) {
    if (b === 0) {
        throw new Error('Division by zero');
    }
    return a / b;
}

// Export functions
exports.add = add;
exports.subtract = subtract;
exports.multiply = multiply;
exports.divide = divide;

// Also export a constant
exports.PI = 3.14159;
