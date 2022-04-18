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

import debug from 'debug';

const APP_NAME = 'wirow';

export class Logger {
  private readonly _debug: debug.Debugger;
  private readonly _info: debug.Debugger;
  private readonly _warn: debug.Debugger;
  private readonly _error: debug.Debugger;

  constructor(prefix?: string) {
    if (prefix) {
      this._debug = debug(`${APP_NAME}:${prefix}:DEBUG`);
      this._info = debug(`${APP_NAME}:${prefix}:INFO`);
      this._warn = debug(`${APP_NAME}:${prefix}:WARN`);
      this._error = debug(`${APP_NAME}:${prefix}:ERROR`);
    } else {
      this._debug = debug(`${APP_NAME}:DEBUG`);
      this._info = debug(`${APP_NAME}:INFO`);
      this._warn = debug(`${APP_NAME}:WARN`);
      this._error = debug(`${APP_NAME}:ERROR`);
    }

    /* eslint-disable no-console */
    this._error.enabled = true;
    this._debug.log = console.debug.bind(console);
    this._info.log = console.info.bind(console);
    this._warn.log = console.warn.bind(console);
    this._error.log = console.error.bind(console);
    /* eslint-enable no-console */
  }

  get debug(): debug.Debugger {
    return this._debug;
  }

  get info(): debug.Debugger {
    return this._info;
  }

  get warn(): debug.Debugger {
    return this._warn;
  }

  get error(): debug.Debugger {
    return this._error;
  }
}
