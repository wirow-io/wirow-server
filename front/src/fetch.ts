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
