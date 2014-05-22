'use strict';

var Q        = require('q');
var path     = require('path');
var execFile = require('child_process').execFile;

/**
 * Process index sync with the given mode (full|incremental).
 *
 * @param type
 * @param options
 * @returns {*}
 */
function esSync(type, options) {
    var def = Q.defer();

    options = options || {};

    if (((process.platform !== 'linux') && (process.platform !== 'darwin')) || (process.arch !== 'x64')) {
        var errorMessage = 'Unsupported platorm/arch ' + process.platform + '/' + process.arch;
        def.reject(new Error(errorMessage));
    } else {
        var confFilePath = path.join(__dirname, './config.xml');
        if (options.confFile) {
            confFilePath = options.confFile;
        }

        var args = ['--conf', confFilePath, '--type', type];
        if (options.args) {
            for (var arg in options.args) {
                if (options.args.hasOwnProperty(arg)) {
                    args.push('--' + arg, options.args[arg]);
                }
            }
        }

        var exeName = 'coorp_es_sync_' + process.platform + '_' + process.arch;
        var exePath = path.join(__dirname, './' + exeName);

        execFile(exePath, args, { maxBuffer: 1024*1024 }, function (err, stdout) {
            if (err) {
                def.reject(stdout);
            } else {
                def.resolve(stdout);
            }
        });
    }

    return def.promise;
}

/**
 * Process full index sync.
 *
 * @param options
 * @returns {*}
 */
function syncFull(options) {
    return esSync('full', options);
}

/**
 * Process incremental index sync.
 *
 * @param options
 * @returns {*}
 */
function syncIncremental(options) {
    return esSync('incremental', options);
}

exports.sync_full        = syncFull;
exports.sync_incremental = syncIncremental;
