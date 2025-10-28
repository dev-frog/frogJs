// nested-test.js - Test nested module loading

console.log('Testing nested module loading\n');

const logger = require('./utils/logger');

logger.log('This is a log message');
logger.info('This is an info message');
logger.error('This is an error message');

const math = require('./math');
logger.info('Loading math module');
logger.log('10 + 5 = ' + math.add(10, 5));

console.log('\nNested module test passed!');
