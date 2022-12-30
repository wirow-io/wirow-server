/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

// Sentry need to be imported and started very first to handle as much errors as it can
import Sentry from './sentry/sentry';
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
