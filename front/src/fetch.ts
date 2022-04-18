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

import type { Logger } from './logger';
import { recommendedTimeout, sendNotification } from './notify';
import { t } from './translate';

export function fetchWrapper(
  p: Promise<Response>,
  { errorKey, log, okCode = 200 }: { errorKey: string; log?: Logger; okCode?: number }
): Promise<Response | null> {
  return p
    .then((resp) => {
      const error = resp.headers.get('X-Greenrooms-Error');
      if (error) {
        return Promise.reject(error);
      }
      if (resp.status != okCode) {
        return Promise.reject(errorKey);
      }
      return resp;
    })
    .catch((e) => {
      let text;
      log?.warn(e);
      if (typeof e === 'string') {
        text = t(e);
        if (e === text) {
          text = t(errorKey);
        }
      } else {
        text = t(errorKey);
      }
      sendNotification({
        text,
        style: 'error',
        closeable: true,
        timeout: recommendedTimeout,
      });
      return null;
    });
}
