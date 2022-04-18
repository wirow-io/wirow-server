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

export class Config {
  static env = 'ENVIRONMENT';
  static port = 'APP_PORT';
  static host = 'APP_HOST';
  static buildTime = 'BUILD_TIME';
  static email = 'info@softmotions.com';
  static debugModules = 'DEBUG_MODULES';
  static minChromeVersion = 'MIN_CHROME_VERSION';
  static minFirefoxVersion = 'MIN_FIREFOX_VERSION';
  static minSafariVersion = 'MIN_SAFARI_VERSION';
  static minBlinkVersion = 'MIN_BLINK_VERSION';
  static minGeckoVersion = 'MIN_GECKO_VERSION';
  static minWebKitVersion = 'MIN_WEBKIT_VERSION';
  static enableWhiteboard = !!parseInt('ENABLE_WHITEBOARD');
  static enableRecording = !!parseInt('ENABLE_RECORDING');
  static limitRegisteredUsers = parseInt('LIMIT_REGISTERED_USERS') || null;

  static get production(): boolean {
    return Config.env === 'production';
  }

  static get timeout(): number | undefined {
    return Config.env === 'production' ? 30 : 0; // 30 sec for prod
  }
}
