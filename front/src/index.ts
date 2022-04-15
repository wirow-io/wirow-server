// Sentry need to be imported and started very first to handle as much errors as it can
import Sentry from './sentry';
Sentry?.onLoad(() => console.log('Crash reporting is loaded'));

import { Config } from './config';
import debug from 'debug';
import 'focus-visible';
import Root from './AppRoot.svelte';

if (Config.debugModules.length > 0) {
  debug.enable(Config.debugModules);
}

const root = new Root({
  target: document.body,
});

export default root;
