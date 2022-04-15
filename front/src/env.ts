import { Device } from '@softmotions/mediasoup-client';
import { sendNotification, setFatal } from './notify';
import { t } from './translate';
import type { UserSpec } from './user';
import { user, User } from './user';
import { acquireWebsocket, subscribe as wsSubscribe } from './ws';

class Env {
  /// Websocket unsubscribe hook.
  wsUnsubscribe: (() => void) | undefined = undefined;

  constructor() {}

  ///
  /// Init
  ///

  private async wsInit() {
    await acquireWebsocket();
    this.wsUnsubscribe = wsSubscribe(this.onWsMessage.bind(this));
  }

  private async userUnit() {
    const resp = await fetch('/users/init');
    const spec = (await resp.json()) as UserSpec;
    user.set(new User(spec));
  }

  async init() {
    console.log(`Init environment `);
    try {
      new Device();
    } catch (e) {
      const msg = 'Unsupported browser';
      setFatal(t(msg));
      return Promise.reject(msg);
    }

    await Promise.all([this.userUnit(), this.wsInit()]).catch((reason) => {
      console.error('App initialization failed ', reason);
      setFatal(t('env.failed_initialize_app'));
    });

    return;
  }

  async dispose() {
    this.wsUnsubscribe?.();
  }

  onWsMessage(data: any): void {
    const event = data.event;
    const cmd = data.cmd;
    if (cmd === 'watcher') {
      this.reloadWindow();
    } else if (event === 'WRC_EVT_ROOM_RECORDING_PP') {
      sendNotification({
        text: t('event.RoomRecordingPPDone', { name: data.name || '' }),
      });
    }
  }

  reloadWindow() {
    window.setTimeout(() => {
      console.log('Reload window');
      document.location.reload();
    }, 0);
  }
}

const env = new Env();
export default env;
