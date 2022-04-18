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

import { Logger } from './logger';

const log = new Logger('mediastream');

/* eslint-disable prefer-rest-params */
export type MediaTrackKind = 'audio' | 'video';

declare global {
  interface MediaStreamEventMap {
    enabletrack: MediaStreamTrackEvent;
    disabletrack: MediaStreamTrackEvent;
    mutetrack: MediaStreamTrackEvent;
    unmutetrack: MediaStreamTrackEvent;
  }
}

export class EnhancedMediaStream extends MediaStream {
  private dispatchSupported = true;
  private trackListeners: [MediaStreamTrack, string, any][] = [];
  private trackPropertyDescriptors: [MediaStreamTrack, string, PropertyDescriptor][] = [];
  private listeners: {
    [P in keyof MediaStreamEventMap]?: Array<(this: MediaStream, ev: MediaStreamEventMap[P]) => any>;
  } = {};
  private _videoProjection?: MediaStreamKindProjection;
  private _audioProjection?: MediaStreamKindProjection;

  get videoProjection(): MediaStream {
    if (this._videoProjection === undefined) {
      this._videoProjection = new MediaStreamKindProjection('video', this);
    }
    return this._videoProjection;
  }

  get audioProjection(): MediaStream {
    if (this._audioProjection === undefined) {
      this._audioProjection = new MediaStreamKindProjection('audio', this);
    }
    return this._audioProjection;
  }

  addEventListener<K extends keyof MediaStreamEventMap>(
    type: K,
    listener: (this: MediaStream, ev: MediaStreamEventMap[K]) => any
  ): void {
    if (this.dispatchSupported === false) {
      const slots = this.listeners[type];
      if (slots === undefined) {
        this.listeners[type] = [listener];
      } else if (!slots.includes(listener)) {
        slots.push(listener);
      }
    } else {
      // @ts-ignore
      super.addEventListener(...Array.from(arguments));
    }
  }

  removeEventListener<K extends keyof MediaStreamEventMap>(
    type: K,
    listener: (this: MediaStream, ev: MediaStreamEventMap[K]) => any
  ): void {
    if (this.dispatchSupported === false) {
      const slots = this.listeners[type];
      if (slots) {
        const idx = slots?.indexOf(listener);
        if (idx !== -1) {
          slots.splice(idx, 1);
        }
      }
    } else {
      // @ts-ignore
      super.removeEventListener(...Array.from(arguments));
    }
  }

  dispatchEvent(event: MediaStreamTrackEvent): boolean {
    if (this.dispatchSupported === false) {
      const slots = this.listeners[event.type as keyof MediaStreamEventMap];
      slots?.forEach((s) => s.apply(this, [event]));
      return true;
    } else {
      return super.dispatchEvent(event);
    }
  }

  addTrack(track: MediaStreamTrack): void {
    if (track == null || this.getTrackById(track.id) != null) {
      return;
    }

    super.addTrack(track);

    (() => {
      // eslint-disable-next-line @typescript-eslint/no-this-alias
      const me = this;
      const pd = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(track), 'enabled')!;
      Object.defineProperty(track, 'enabled', {
        configurable: true,
        enumerable: true,
        get() {
          return pd.get?.call(track);
        },
        set(value) {
          log.debug && log.debug(`EnhancedMediaStream | Set track.enabled=${value} for ${track.kind} ${track.id}`);
          const old = pd.get?.call(track);
          if (value !== old) {
            pd.set?.call(track, value);
            if (value === true) {
              me.dispatchEvent(
                new MediaStreamTrackEvent('enabletrack', {
                  track,
                })
              );
            } else {
              me.dispatchEvent(
                new MediaStreamTrackEvent('disabletrack', {
                  track,
                })
              );
            }
          }
        },
      });
      this.trackPropertyDescriptors.push([track, 'enabled', pd]);
    })();

    const onended = this.onTrackEnded.bind(this, track);
    this.trackListeners.push([track, 'ended', onended]);
    track.addEventListener('ended', onended);

    const onmute = this.onTrackMuted.bind(this, track);
    this.trackListeners.push([track, 'mute', onmute]);
    track.addEventListener('mute', onmute);
    this.trackListeners.push([track, 'unmute', onmute]);
    track.addEventListener('unmute', onmute);
    this.dispatchEvent(
      new MediaStreamTrackEvent('addtrack', {
        track,
      })
    );
  }

  removeTrack(track: MediaStreamTrack): void {
    if (track == null || this.getTrackById(track.id) == null) {
      return;
    }

    super.removeTrack(track);

    this.trackListeners = this.trackListeners.filter((t) => {
      if (t[0] === track) {
        track.removeEventListener(t[1], t[2]);
        return false;
      }
      return true;
    });

    this.trackPropertyDescriptors = this.trackPropertyDescriptors.filter((t) => {
      if (t[0] === track) {
        Object.defineProperty(t, t[1], t[2]);
        return false;
      } else {
        return true;
      }
    });

    this.dispatchEvent(
      new MediaStreamTrackEvent('removetrack', {
        track,
      })
    );
  }

  removeTracks({ stop = false, kind }: { stop?: boolean; kind?: MediaTrackKind } = {}): number {
    const tracks = this.getTracks().filter((t) => kind === undefined || t.kind === kind);
    tracks.forEach((t) => {
      this.removeTrack(t);
      stop && t.stop();
    });
    return tracks.length;
  }

  mergeTracks(
    tracks: MediaStreamTrack[],
    {
      keep = true,
      clone = false,
      stop = false,
      keepOtherKinds = false,
    }: {
      keep?: boolean;
      clone?: boolean;
      stop?: boolean;
      keepOtherKinds?: boolean;
    } = {}
  ): number {
    let n = 0;
    if (keepOtherKinds) {
      keep = false;
    }
    if (!keep) {
      this.getTracks().forEach((t) => {
        if (tracks.find((tt) => tt.id === t.id || (keepOtherKinds && tt.kind !== t.kind)) == null) {
          this.removeTrack(t);
          stop && t.stop();
          ++n;
        }
      });
    }
    tracks.forEach((t) => {
      if (this.getTrackById(t.id) == null) {
        this.addTrack(clone ? t.clone() : t);
        ++n;
      }
    });
    return n;
  }

  constructor();
  constructor(stream?: MediaStream) {
    super();
    if (stream) {
      this.mergeTracks(stream.getTracks());
    }
    // Dest dispatch supported event
    let dispatched = false;
    const testAddTrackEvent = () => {
      dispatched = true;
    };
    this.addEventListener('addtrack', testAddTrackEvent);
    // @ts-ignore
    this.dispatchEvent(new Event('addtrack'));
    this.removeEventListener('addtrack', testAddTrackEvent);
    this.dispatchSupported = dispatched;
  }

  private onTrackMuted(track: MediaStreamTrack) {
    this.dispatchEvent(
      new MediaStreamTrackEvent(track.muted ? 'mutetrack' : 'unmutetrack', {
        track,
      })
    );
  }

  private onTrackEnded(track: MediaStreamTrack) {
    this.removeTrack(track);
  }
}

class MediaStreamKindProjection extends MediaStream {
  private listeners: [
    keyof MediaStreamEventMap,
    (ev: MediaStreamTrackEvent) => void,
    (ev: MediaStreamTrackEvent) => void
  ][] = [];

  addEventListener<K extends keyof MediaStreamEventMap>(
    type: K,
    listener: (this: MediaStream, ev: MediaStreamEventMap[K]) => any
  ): void {
    const idx = this.listeners.findIndex((l) => l[0] === type && l[1] === listener);
    if (idx === -1) {
      const l = this.onEvent.bind(this, listener);
      this.listeners.push([type, listener, l]);
      this.stream.addEventListener(type, l);
    }
  }

  removeEventListener<K extends keyof MediaStreamEventMap>(
    type: K,
    listener: (this: MediaStream, ev: MediaStreamEventMap[K]) => any
  ): void {
    const idx = this.listeners.findIndex((l) => l[0] === type && l[1] === listener);
    if (idx !== -1) {
      this.stream.removeEventListener(type, this.listeners[idx][2]);
      this.listeners.splice(idx, 1);
    }
  }

  detach(): void {
    this.stream.removeEventListener('addtrack', this.onAddTrack);
    this.stream.removeEventListener('removetrack', this.onRemoveTrack);
  }

  constructor(readonly kind: MediaTrackKind, private readonly stream: MediaStream) {
    super(stream.getTracks().filter((t) => t.kind === kind));
    this.onAddTrack = this.onAddTrack.bind(this);
    this.onRemoveTrack = this.onRemoveTrack.bind(this);
    stream.addEventListener('addtrack', this.onAddTrack);
    stream.addEventListener('removetrack', this.onRemoveTrack);
  }

  private onAddTrack(ev: MediaStreamTrackEvent) {
    if (ev.track.kind == this.kind) {
      this.addTrack(ev.track);
    }
  }

  private onRemoveTrack(ev: MediaStreamTrackEvent) {
    if (ev.track.kind == this.kind) {
      this.removeTrack(ev.track);
    }
  }

  private onEvent(delegate: (ev: MediaStreamTrackEvent) => void, ev: MediaStreamTrackEvent) {
    if (ev.track.kind === this.kind) {
      delegate(ev);
    }
  }
}
