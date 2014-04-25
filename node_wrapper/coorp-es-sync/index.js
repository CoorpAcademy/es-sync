function sync_full(completion_handler)
{
	return es_sync('full', completion_handler);
}

function sync_incremental(completion_handler)
{
	return es_sync('incremental', completion_handler);
}

function es_sync(type, completion_handler)
{
	if (((process.platform == 'linux') || (process.platform == 'darwin')) && (process.arch == 'x64'))
	{
		var path = require('path');
		var exeName = 'coorp_es_sync_' + process.platform + '_' + process.arch;
		var exePath = path.join(__dirname, './' + exeName);
		var confPath = path.join(__dirname, './config.xml');
		sync_process = require('child_process').execFile(exePath, ['--conf', confPath, '--type', type], 
				{ maxBuffer: 1024*1024 }, completion_handler);
		return true;
	}else{
		console.log('Unsupported platorm/arch ' + process.platform + '/' + process.arch);
		return false;
	}
}

exports.sync_full = sync_full;
exports.sync_incremental = sync_incremental;