const rewire = require('rewire')
const defaults = rewire('react-scripts/scripts/build.js') // If you ejected, use this instead: const defaults = rewire('./build.js')
let config = defaults.__get__('config')

config.optimization.splitChunks = {
	cacheGroups: {
		default: false
	}
}

config.optimization.runtimeChunk = false

config.output.filename = '[name].js'
config.output.chunkFilename = '[name].js'
config.plugins[5].options.filename = '[name].css'
config.plugins[5].options.moduleFilename = () => 'main.css'