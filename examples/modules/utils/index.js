// Loaded by require('./modules/utils'); its own require() resolves from utils/
const answer = require('./answer');
const logger = require('./logger');

module.exports = { answer, logger, dirname: __dirname };
