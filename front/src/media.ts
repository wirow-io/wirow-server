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

import type { Readable } from 'svelte/store';
import { derived, readable, writable } from 'svelte/store';
import { makeStoreAsPersistent } from './db';
import { Logger } from './logger';
import { EnhancedMediaStream } from './mediastream';
import { onFatalSubscribers, setFatal } from './notify';
import { t } from './translate';
import { storeGet } from './utils';

const log = new Logger('media');

interface AcquireMediaDeviceTask {
  audio?: boolean | string;
  video?: boolean | string;
  terminate?: boolean;
  resolve: (v: any) => void;
  reject: (e: any) => void;
}

const _acquireTasks = new Array<AcquireMediaDeviceTask>();

export const mediaDevicesAcquireState = writable<string>('unknown');

export const stream = new EnhancedMediaStream();

export const streamVideo = stream.videoProjection;

export const streamTrigger = writable({});

stream.addEventListener('removetrack', () => {
  streamTrigger.set({});
});

stream.addEventListener('addtrack', (ev) => {
  const track = ev.track;
  if (ev.track.kind === 'video') {
    track.enabled = storeGet(videoInputMuted) === false;
  } else if (track.kind === 'audio') {
    track.enabled = storeGet(audioInputMuted) === false;
  }
  streamTrigger.set({});
});

function setStream(s: MediaStream, clone = false): MediaStream {
  stream.mergeTracks(s.getTracks(), { clone });
  return stream;
}

function _closeAllMediaTracks(stop = false): void {
  stream.removeTracks({ stop }) && streamTrigger.set({});
}

export function closeAllMediaTracks(): void {
  acquireMediaDevices(undefined, undefined, true);
}

let refreshMediaDevices: () => Promise<MediaDeviceInfo[]>;

export function getNavigatorMediaDevices() {
  const msg = 'Media devices are undefined it seems connection is not secure';
  const mock = {
    ondevicechange: null,
    enumerateDevices: () => Promise.reject<MediaDeviceInfo[]>(msg),
    getUserMedia: () => Promise.reject<MediaStream>(msg),
    getDisplayMedia: () => Promise.reject<MediaStream>(msg),
    getSupportedConstraints: () => ({} as MediaTrackSupportedConstraints),
    ...new EventTarget(),
  } as MediaDevices;

  if (!navigator.mediaDevices) {
    log.warn(msg);
  }
  return navigator.mediaDevices || mock;
}

/// Available media devices
export const mediaDevices = readable<MediaDeviceInfo[]>([], (set) => {
  getNavigatorMediaDevices().ondevicechange = () => run();
  refreshMediaDevices = run;
  function run(): Promise<MediaDeviceInfo[]> {
    return new Promise((resolve, reject) => {
      getNavigatorMediaDevices()
        .enumerateDevices()
        .then((v) => {
          set(v);
          resolve(v);
        })
        .catch((e) => {
          log.error(e);
          set([]);
          reject(e);
        });
    });
  }
  run();
});

/// Camera devices.
export const videoInputDevices = derived<Readable<MediaDeviceInfo[]>, MediaDeviceInfo[]>(mediaDevices, (d) => {
  return d.filter((d) => d.kind === 'videoinput');
});

/// Audio input devices.
export const audioInputDevices = derived<Readable<MediaDeviceInfo[]>, MediaDeviceInfo[]>(mediaDevices, (d) => {
  return d.filter((d) => d.kind === 'audioinput');
});

/// All devices store.
export const allInputDevices = derived<Readable<MediaDeviceInfo[]>, MediaDeviceInfo[]>(mediaDevices, (d) => {
  return d.filter((d) => d.kind === 'audioinput' || d.kind === 'videoinput');
});

export const sharingEnabled = derived<Readable<{}>, boolean>(
  streamTrigger,
  () =>
    stream.getTracks().find((t) => {
      return (<any>t).$screenSharing === true;
    }) != null
);

/// Video input device ID preferred by user.
export const preferredVideoInputDeviceId = writable<string | undefined>(undefined);

export const preferredAudioInputDeviceId = writable<string | undefined>(undefined);

export const videoInputMuted = writable<boolean>(false);

export const audioInputMuted = writable<boolean>(false);

videoInputMuted.subscribe((muted) => {
  const track = stream.getVideoTracks()[0];
  log.debug.enabled && log.debug(`videoInputMuted.subscribe() | track=${track}  muted=${muted}`);
  if (track) {
    track.enabled = muted === false;
  }
});

audioInputMuted.subscribe((muted) => {
  const track = stream.getAudioTracks()[0];
  log.debug.enabled && log.debug(`audioInputMuted.subscribe() | track=${track}  muted=${muted}`);
  if (track) {
    track.enabled = muted === false;
  }
});

export function getDevicesStore(kind?: MediaDeviceKind): Readable<MediaDeviceInfo[]> {
  switch (kind) {
    case 'audioinput':
      return audioInputDevices;
    case 'videoinput':
      return videoInputDevices;
    default:
      return allInputDevices;
  }
}

async function _acquireMediaDevicesImpl(): Promise<void> {
  // Ensure we have loaded audio/video devices
  if (storeGet(mediaDevices).length == 0) {
    await refreshMediaDevices().catch((e) => {
      log.warn(e);
    });
  }

  while (_acquireTasks.length > 0) {
    const task = _acquireTasks[_acquireTasks.length - 1];

    if (task.terminate === true) {
      _acquireTasks.pop();
      _closeAllMediaTracks(true);
      continue;
    }

    const constraints: MediaStreamConstraints = {};
    if (task.audio == null) {
      const devices = storeGet(audioInputDevices);
      const did = storeGet(preferredAudioInputDeviceId);
      if (devices.length > 0) {
        task.audio = devices.find((v) => v.deviceId === did)?.deviceId;
        if (task.audio == null) {
          preferredAudioInputDeviceId.set(undefined);
          task.audio = typeof did === 'string' ? did : true;
        }
      }
    }
    if (task.video == null) {
      const devices = storeGet(videoInputDevices);
      const did = storeGet(preferredVideoInputDeviceId);
      if (devices.length > 0) {
        task.video = devices.find((v) => v.deviceId === did)?.deviceId;
        if (task.video == null) {
          preferredVideoInputDeviceId.set(undefined);
          task.video = typeof did === 'string' ? did : true;
        }
      }
    }
    if (task.audio != null) {
      if (typeof task.audio === 'string') {
        if (stream.getAudioTracks().find((t) => t.getSettings().deviceId === task.audio) == null) {
          constraints.audio = {
            deviceId: task.audio,
          };
        }
      } else if (task.audio === true) {
        if (stream.getAudioTracks().length == 0) {
          constraints.audio = true;
        }
      }
    }
    if (task.video != null) {
      if (typeof task.video === 'string') {
        if (stream.getVideoTracks().find((t) => t.getSettings().deviceId === task.video) == null) {
          constraints.video = {
            deviceId: task.video,
          };
        }
      } else if (task.video === true) {
        if (stream.getVideoTracks().length == 0) {
          constraints.video = true;
        }
      }
    }
    if (constraints.audio) {
      // Only one audio track allowed
      stream.getAudioTracks().forEach((t) => {
        t.stop();
        stream.removeTrack(t);
      });
    }
    if (constraints.video) {
      // Only one video track allowed
      stream.getVideoTracks().forEach((t) => {
        t.stop();
        stream.removeTrack(t);
      });
    }
    if (Object.keys(constraints).length == 0) {
      _acquireTasks.pop();
      task.resolve(stream);
      continue;
    }

    await getNavigatorMediaDevices()
      .getUserMedia(constraints)
      .then((s) => {
        try {
          setStream(s);
          let t = stream.getVideoTracks()[0];
          if (t && t.getSettings().deviceId) {
            preferredVideoInputDeviceId.set(t.getSettings().deviceId);
          }
          t = stream.getAudioTracks()[0];
          if (t && t.getSettings().deviceId) {
            preferredAudioInputDeviceId.set(t.getSettings().deviceId);
          }
          task.resolve(stream);
          const devices = storeGet(mediaDevices);
          if (devices.length && devices[0].label === '') {
            refreshMediaDevices?.();
          }
        } catch (e) {
          task.reject(e);
          return;
        }
      })
      .catch((e) => task.reject(e));
    _acquireTasks.pop();
  }
}

async function _acquireMediaDevices(): Promise<void> {
  mediaDevicesAcquireState.set('progress');
  return _acquireMediaDevicesImpl().finally(() => {
    mediaDevicesAcquireState.set('done');
  });
}

export async function acquireMediaDevices(
  video?: boolean | string,
  audio?: boolean | string,
  terminate?: boolean
): Promise<MediaStream> {
  if (video === '') {
    video = true;
  }
  if (audio === '') {
    audio = true;
  }
  return new Promise((resolve, reject) => {
    _acquireTasks.unshift({
      video,
      audio,
      terminate,
      resolve,
      reject,
    });
    if (_acquireTasks.length === 1) {
      _acquireMediaDevices();
    }
  });
}

export async function acquireMediaDevicesUI(
  video?: boolean | string,
  audio?: boolean | string
): Promise<MediaStream | undefined> {
  return acquireMediaDevices(video, audio).catch((err) => {
    console.error(err);
    setFatal(t('env.failed_access_devices'), t('ws.ask_reload'));
    return undefined;
  });
}

onFatalSubscribers.push(() => {
  closeAllMediaTracks();
});

makeStoreAsPersistent('preferredVideoInputDeviceId', preferredVideoInputDeviceId);
makeStoreAsPersistent('preferredAudioInputDeviceId', preferredAudioInputDeviceId);
makeStoreAsPersistent('videoInputMuted', videoInputMuted);
makeStoreAsPersistent('audioInputMuted', audioInputMuted);
