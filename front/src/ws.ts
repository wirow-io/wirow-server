/* eslint-disable @typescript-eslint/no-empty-function */

import { Config } from './config';
import { Logger } from './logger';
import { setFatal } from './notify';
import { t } from './translate';

export interface WSMessage extends Record<string, any> {}

const log = new Logger('WS');

interface Subscriber {
  subscriber: (data: any, error?: string) => void;
  exptime?: number;
  dispose: (error?: string) => void;
}

const _initialReconnectTimeout = 1000;

const _maxReconnectTimeout = 10000;

const _emptyFn = () => {};

let _lastPingTs = 0;

let _hookSeq = 0;

let _checkerTimeoutId = -1;

let _ws: WebSocket | undefined;

let _next_connect_timeout = _initialReconnectTimeout;

let _operable = false;

let _stopped = false;

const _awaiters: Array<(value: WebSocket) => void> = [];

const _subscribers: Array<Subscriber> = [];

let _fatalFn: (() => void) | undefined;

function getNextReconnect(): number {
  _next_connect_timeout = Math.min(_next_connect_timeout * 1.1, _maxReconnectTimeout);
  return _next_connect_timeout;
}

function acquireTicket(endpoint: string): Promise<string> {
  log.debug.enabled && log.debug('Acquire WS ticket at: ', endpoint);
  return new Promise((resolve, reject) => {
    const req = new XMLHttpRequest();
    const url = endpoint + (/\?/.test(endpoint) ? '&' : '?') + new Date().getTime();
    req.open('GET', url);
    req.onload = () => {
      // Preserve original hash
      const hash = window.location.hash;
      if (req.responseURL !== url) {
        window.location.href = `${req.responseURL}${hash}`;
      }
      if (req.status == 200 && req.responseText != '') {
        log.debug.enabled && log.debug('Ticket acquired successfully');
        resolve(req.responseText);
      } else {
        reject(`Invalid WS ticket response. Code: ${req.status}`);
      }
    };
    req.onabort = () => reject('Ticket request aborted');
    req.ontimeout = () => reject('Ticket request timeout');
    req.onerror = () => reject('Ticket request error');
    req.send();
  });
}

export function dispose(): void {
  _stopped = true;
  _operable = false;
  _awaiters.length = 0;
  _subscribers.length = 0;
  _ws?.close();
}

function authWS(ws: WebSocket, ticket: string): Promise<WSMessage> {
  return new Promise((resolve, reject) => {
    ws.onmessage = (evt) => {
      ws.onmessage = null;
      const data = evt.data;
      if (typeof data !== 'string') {
        reject('WS invalid response: ' + evt.data);
        return;
      }
      try {
        const payload = JSON.parse(data);
        log.debug.enabled && log.debug('Ticket authenticated ', payload);
        resolve(payload);
      } catch (e) {
        reject(e);
      }
    };

    try {
      ws.send(ticket);
    } catch (e) {
      log.error('Send error', e);
      reject(`${e}`);
    }
  });
}

// eslint-disable-next-line @typescript-eslint/explicit-module-boundary-types
export async function sendAwait<T extends WSMessage = WSMessage>(
  command: WSMessage,
  timeoutSec?: number,
  hook?: string
): Promise<T> {
  if (timeoutSec == null) {
    timeoutSec = Config.timeout;
  }
  if (hook == null) {
    if (_hookSeq + 1 > 99999999) {
      _hookSeq = 0;
    }
    ++_hookSeq;
    hook = _hookSeq.toString();
  }
  command = command || {};
  command.hook = hook;
  await send(command);
  return new Promise((resolve, reject) => {
    const d = subscribe<T>((data, error) => {
      if (error) {
        reject(error);
        return;
      }
      if (data.hook === hook) {
        d(); // Dispose me first
        if (typeof data.error === 'string') {
          const e = new String(t(data.error));
          if (e != data.error) {
            (<any>e).translated = data.error;
          }
          reject(e);
        } else {
          resolve(data);
        }
      }
    }, timeoutSec);
  });
}

async function startWS(endpoint: string, ticketEndpoint: string): Promise<WebSocket> {
  const ticket = await acquireTicket(ticketEndpoint);
  log.debug.enabled && log.debug('Endpoint ', endpoint);

  return new Promise((resolve, reject) => {
    const ws = new WebSocket(endpoint);
    const wsSend = ws.send.bind(ws);
    const wsConsumer = (data: string) => {
      const json = JSON.parse(data);
      log.debug.enabled && log.debug('onMessage ', json);
      ([] as typeof _subscribers).concat(_subscribers).forEach((s) => s.subscriber(json));
    };

    ws.onopen = function () {
      setTimeout(function () {
        authWS(ws, ticket)
          .then(() => {
            _next_connect_timeout = _initialReconnectTimeout;
            ws.onmessage = onMessage;
            resolve(ws);
            resolve = _emptyFn;
          })
          .catch((err) => {
            ws.close();
            reject(err);
            reject = _emptyFn;
          });
      }, 0);
    };

    function onMessage(evt: MessageEvent) {
      wsConsumer(evt.data || '{}');
    }

    ws.send = function (data: string) {
      wsSend(data);
    };

    ws.onclose = function () {
      if (!_stopped) {
        log.warn('Connection close');
        if (reject !== _emptyFn) {
          setTimeout(start.bind(start, true), getNextReconnect());
        }
      }
    };

    ws.onerror = function (evt) {
      log.warn('Connection error', evt);
    };
  });
}

function check() {
  const ws = _ws;
  const ctime = new Date().getTime();
  const expired = _subscribers.filter((s) => s.exptime && s.exptime <= ctime);
  expired.forEach((e) => e.dispose('Timeout'));
  if (_lastPingTs === 0) {
    _lastPingTs = ctime;
  }
  if (ctime - _lastPingTs >= 30 * 1000 /* 30 sec */ && ws?.readyState === WebSocket.OPEN) {
    send({
      cmd: 'ping',
    });
    _lastPingTs = ctime;
  }
  _checkerTimeoutId = window.setTimeout(check, 1000);
}

function initChecker() {
  if (_checkerTimeoutId < 0) {
    _checkerTimeoutId = window.setTimeout(check, 1000);
  }
}

export function start(reconnect = false): any {
  initChecker();
  const ws = _ws;
  if (ws?.readyState === WebSocket.OPEN) {
    ws.close();
    return;
  }
  _ws = undefined;
  _stopped = false;

  const p = window.location.protocol;
  const wp = p === 'https:' ? 'wss://' : 'ws://';
  const location = window.document.location;
  const host = window.location.host.replace(/:.*/, '');
  const prefix = `${host}${location.port ? ':' + location.port : ''}`;

  startWS(`${wp}${prefix}/ws/channel`, `${p}//${prefix}/ws/ticket`)
    .then((ws) => {
      _ws = ws;
      _awaiters.forEach((a) => a(ws));
      _awaiters.length = 0;
      if (_fatalFn) {
        _fatalFn();
        _fatalFn = undefined;
      }
      if (reconnect) {
        window.setTimeout(() => {
          window.location.reload();
        }, 0);
      }
    })
    .catch((err) => {
      log.error(err);
      if (!_fatalFn) {
        _fatalFn = setFatal(t('ws.lost_connection'), t('ws.wait_please'));
      }
      if (!_stopped) {
        setTimeout(start.bind(start, true), getNextReconnect());
      }
    });
  _operable = true;
}

export function acquireWebsocket(): Promise<WebSocket> {
  const ws = _ws;
  if (ws != null) {
    return Promise.resolve(ws);
  }
  while (_awaiters.length > 1000) {
    _awaiters.shift();
  }
  const ret = new Promise<WebSocket>((resolve) => _awaiters.push(resolve));
  if (!_operable) {
    start();
  }
  return ret;
}

// eslint-disable-next-line @typescript-eslint/explicit-module-boundary-types
export async function send(data: WSMessage | string): Promise<void> {
  if (typeof data !== 'string') {
    data = JSON.stringify(data);
  }
  const ws = await acquireWebsocket();
  log.debug.enabled && log.debug('Send ', data);
  ws.send(data);
}

export function subscribe<T extends WSMessage | undefined>(
  subscriber: (data: T, error?: string) => void,
  timeoutSec?: number
): () => void {
  const slot: Subscriber = {
    subscriber,
    dispose: (error?: string) => {
      if (error) {
        slot.subscriber(undefined, error);
      }
      const idx = _subscribers.findIndex((s) => s === slot);
      if (idx !== -1) {
        _subscribers.splice(idx, 1);
      }
    },
  };
  if (timeoutSec && timeoutSec > 0) {
    slot.exptime = new Date().getTime() + timeoutSec * 1000;
  }
  while (_subscribers.length > 100000) {
    // 100K limit of active subscribers
    _subscribers.shift()?.dispose('Stalled');
  }
  _subscribers.push(slot);
  return slot.dispose;
}
