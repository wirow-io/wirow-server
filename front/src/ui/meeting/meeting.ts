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

import { derived, writable } from 'svelte/store';
import type { Readable, Writable } from 'svelte/store';
import { audioChatMessage, audioMemberJoined, audioRecordingStarted } from '../../audio';
import type { NU, RoomType } from '../../interfaces';
import { Logger } from '../../logger';
import {
  audioInputMuted,
  closeAllMediaTracks,
  getNavigatorMediaDevices,
  stream as ownStream,
  videoInputMuted,
  sharingEnabled,
} from '../../media';
import { recommendedTimeout, sendNotification } from '../../notify';
import { Room, RoomMember, roomStore } from '../../room';
import type { RoomConnectionState } from '../../room';
import { t } from '../../translate';
import { getUserSnapshot, nickname } from '../../user';
import { catchLogResolve, storeGet } from '../../utils';
import type { ActivityBarSlot } from '../ActivityBarLib';

const log = new Logger('meeting');

export const Key = {};

export class MeetingContext {
  activityBarSlots = writable<ActivityBarSlot[]>([]);

  activityBarActiveSlot = derived<Readable<ActivityBarSlot[]>, ActivityBarSlot | undefined>(
    this.activityBarSlots,
    (d) => {
      return d.find((s) => s.active);
    }
  );

  hasUnreadMessages = writable<boolean>(false);

  hasStartedRecording = writable<boolean>(false);

  get meetingRoomStore(): typeof meetingRoomStore {
    return meetingRoomStore;
  }

  constructor(readonly findActivityBarSlot: (id: string) => ActivityBarSlot | undefined) {}
}

export const meetingRoomStore = (function () {
  let meetingRoom: MeetingRoom | undefined;
  return derived<Readable<Room | undefined>, MeetingRoom | undefined>(roomStore, (room) => {
    if (room == null) {
      meetingRoom = undefined;
    } else if (meetingRoom == null || meetingRoom.uuid !== room.uuid) {
      meetingRoom = new MeetingRoom(room);
    }
    return meetingRoom;
  });
})();

export class MeetingMember {
  readonly name: Writable<string>;

  readonly audioLevel = writable<number>(0);
  readonly audioEnabled: Readable<boolean>;
  readonly videoEnabled: Readable<boolean>;
  fullscreen = false;

  private readonly _audioEnabled = writable<boolean>(false);
  private readonly _videoEnabled = writable<boolean>(false);

  private replacedVideoTrackBySharing?: MediaStreamTrack;
  private replacedVideoInputMuted?: boolean;

  private pending: {
    webcam: boolean;
  } = {
    webcam: false,
  };

  get uuid(): string {
    return this.member.uuid;
  }

  get nameString(): string {
    return this.member.name;
  }

  get stream(): MediaStream {
    return this.member.stream;
  }

  get itsme(): boolean {
    return this.member.itsme;
  }

  get isRoomOwner(): boolean {
    return this.member.isRoomOwner;
  }

  async onToggleSharing(): Promise<void> {
    if (this.pending.webcam) {
      return;
    }
    const enabled = storeGet(sharingEnabled);
    try {
      this.pending.webcam = true;
      if (enabled) {
        await this.onSharingDisable();
      } else {
        await this.onSharingEnable();
      }
    } finally {
      this.pending.webcam = false;
    }
  }

  dispose(): void {
    this.member.stream.removeEventListener('addtrack', this.onTrackAdded);
    this.member.stream.removeEventListener('removetrack', this.onTrackRemoved);
    this.member.stream.removeEventListener('enabletrack', this.onTrackEnabled);
    this.member.stream.removeEventListener('disabletrack', this.onTrackEnabled);
    this.member.stream.removeEventListener('mutetrack', this.onTrackEnabled);
    this.member.stream.removeEventListener('unmutetrack', this.onTrackEnabled);
    this._audioEnabled.set(false);
    this._videoEnabled.set(false);
  }

  toggleFullscreen(): boolean {
    this.fullscreen = !this.fullscreen;
    this.room.updateMembers();
    return this.fullscreen;
  }

  constructor(readonly room: MeetingRoom, readonly member: RoomMember) {
    this.name = writable(member.name);

    this.onTrackAdded = this.onTrackAdded.bind(this);
    member.stream.addEventListener('addtrack', this.onTrackAdded);

    this.onTrackRemoved = this.onTrackRemoved.bind(this);
    member.stream.addEventListener('removetrack', this.onTrackRemoved);

    this.onTrackEnabled = this.onTrackEnabled.bind(this);
    member.stream.addEventListener('enabletrack', this.onTrackEnabled);
    member.stream.addEventListener('disabletrack', this.onTrackEnabled);
    member.stream.addEventListener('mutetrack', this.onTrackEnabled);
    member.stream.addEventListener('unmutetrack', this.onTrackEnabled);

    member.stream.getTracks().forEach((t) => this.syncTrackEnabled(t));

    this.videoEnabled = derived<[Readable<boolean>, Readable<boolean>], boolean>(
      [videoInputMuted, this._videoEnabled],
      ([m, e]) => e && !(this.itsme && m === true)
    );

    this.audioEnabled = derived<[Readable<boolean>, Readable<boolean>], boolean>(
      [audioInputMuted, this._audioEnabled],
      ([m, e]) => e && !(this.itsme && m === true)
    );
  }

  private async onSharingDisable() {
    let track: MediaStreamTrack | NU = ownStream.getVideoTracks()[0];
    if (track) {
      ownStream.removeTrack(track);
      track.stop();
    }
    if (this.replacedVideoInputMuted != null) {
      const muted = storeGet<boolean>(videoInputMuted); // User muted video during screen sharind
      videoInputMuted.set(muted === true || this.replacedVideoInputMuted);
      this.replacedVideoInputMuted = undefined;
    }
    track = this.replacedVideoTrackBySharing;
    if (track) {
      this.replacedVideoTrackBySharing = undefined;
      ownStream.addTrack(track);
    }
  }

  private async onSharingEnable() {
    const stream = <MediaStream | NU>await (getNavigatorMediaDevices() as any)
      .getDisplayMedia({
        audio: false,
        video: {
          displaySurface: 'monitor',
          logicalSurface: true,
          cursor: true,
          width: { max: 1920 },
          height: { max: 1200 },
          frameRate: { max: 30 },
        },
      })
      .catch((e) => {
        if (e) {
          if (e.name === 'NotAllowedError') {
            sendNotification({
              text: t('error.screen_sharing_not_allowed_by_user'),
              style: 'warning',
              timeout: recommendedTimeout,
            });
          } else {
            log.warn('onWebcamEnable() | ', e);
          }
        }
        return null;
      });
    if (stream === null) {
      return;
    }
    const track = stream?.getVideoTracks()[0];
    if (track == null) {
      return;
    }
    (<any>track).$screenSharing = true;
    this.replacedVideoInputMuted = storeGet<boolean>(videoInputMuted);

    this.replacedVideoTrackBySharing = ownStream.getVideoTracks()[0];
    if (this.replacedVideoTrackBySharing) {
      ownStream.removeTrack(this.replacedVideoTrackBySharing);
    }
    videoInputMuted.set(false);
    ownStream.addTrack(track);
  }

  private syncTrackEnabled(track: MediaStreamTrack) {
    if (track.kind === 'audio') {
      this._audioEnabled.set(track.enabled == true && track.muted == false);
    } else if (track.kind === 'video') {
      this._videoEnabled.set(track.enabled == true);
    }
  }

  private onTrackEnabled(ev: MediaStreamTrackEvent) {
    this.syncTrackEnabled(ev.track);
  }

  private onTrackAdded(ev: MediaStreamTrackEvent) {
    this.syncTrackEnabled(ev.track);
  }

  private onTrackRemoved(ev: MediaStreamTrackEvent) {
    if (ev.track.kind === 'audio') {
      this._audioEnabled.set(false);
    } else if (ev.track.kind === 'video') {
      this._videoEnabled.set(false);
    }
  }
}

export class MeetingRoom {
  readonly isClosed: Writable<boolean>;

  readonly members: Writable<MeetingMember[]>;

  readonly membersOnGrid: Readable<MeetingMember[]>;

  readonly roomName: Writable<string>;

  readonly activeSpeaker: Readable<string>;

  readonly memberOnFullScreen: Readable<MeetingMember | undefined>;

  private _members: MeetingMember[] = [];

  private uuid2member = new Map<string, MeetingMember>();

  private audioLevelResetTimeout: any;

  get name(): string {
    return this.room.name;
  }

  get uuid(): string {
    return this.room.uuid;
  }

  get ownMemberUuid(): string | undefined {
    return this.room.memberOwn?.uuid;
  }

  get type(): RoomType {
    return this.room.type;
  }

  get isAllowedBroadcastMessages(): boolean {
    // todo: webinar room owner?
    return this.type === 'meeting';
  }

  get stateSend(): Readable<RoomConnectionState> {
    return this.room.stateSend;
  }

  get stateRecv(): Readable<RoomConnectionState> {
    return this.room.stateRecv;
  }

  get stateMerged(): Readable<RoomConnectionState> {
    return this.room.stateMerged;
  }

  get memberOwn(): MeetingMember | undefined {
    return this._members.find((m) => m.itsme);
  }

  get owner(): boolean {
    return this.room.owner;
  }

  memberByUUID(uuid: string): MeetingMember | undefined {
    return this.uuid2member.get(uuid);
  }

  leave(message?: string): Promise<unknown> {
    return this.room.leave(message);
  }

  updateMembers(): void {
    this.members.set([...this._members]);
  }

  constructor(readonly room: Room) {
    this.isClosed = writable(room.isClosed);
    this._members = room.members.map((m) => new MeetingMember(this, m));
    this._members.forEach((m) => this.uuid2member.set(m.uuid, m));
    this.members = writable(this._members);
    this.roomName = writable(room.name);
    this.activeSpeaker = room.activeSpeaker;

    this.membersOnGrid = derived<Readable<MeetingMember[]>, MeetingMember[]>(this.members, (members) => {
      if (this.room.type === 'webinar') {
        return members.filter((m) => m.isRoomOwner);
      } else {
        return members;
      }
    });

    this.memberOnFullScreen = derived<Readable<MeetingMember[]>, MeetingMember | undefined>(this.members, (members) => {
      return members.find((m) => m.fullscreen);
    });

    this.onRoomClosed = this.onRoomClosed.bind(this);
    room.addListener('roomClosed', this.onRoomClosed);
    this.onRoomName = this.onRoomName.bind(this);
    room.addListener('roomName', this.onRoomName);
    this.onMemberJoined = this.onMemberJoined.bind(this);
    room.addListener('memberJoined', this.onMemberJoined);
    this.onRoomMessage = this.onRoomMessage.bind(this);
    room.addListener('roomMessage', this.onRoomMessage);
    this.onMemberLeft = this.onMemberLeft.bind(this);
    room.addListener('memberLeft', this.onMemberLeft);
    this.onMemberName = this.onMemberName.bind(this);
    room.addListener('memberName', this.onMemberName);
    this.onMemberBroadcast = this.onMemberBroadcast.bind(this);
    room.addListener('memberBroadcast', this.onMemberBroadcast);
    this.onVolumes = this.onVolumes.bind(this);
    room.addListener('volumes', this.onVolumes);
    this.onRecordingStatus = this.onRecordingStatus.bind(this);
    room.addListener('recordingStatus', this.onRecordingStatus);
  }

  private onVolumes(v: Array<[string, number]>) {
    const u = getUserSnapshot();
    const ath = Math.abs(u?.system?.alo_threshold || 55);
    const aloInterval = u?.system?.alo_interval || 1000;

    if (this.audioLevelResetTimeout) {
      clearTimeout(this.audioLevelResetTimeout);
    }
    v.forEach(([uuid, lvl]) => {
      const m = this.memberByUUID(uuid);
      // Normalize level
      m?.audioLevel.set(((lvl + ath) * 100) / ath);
    });
    this.audioLevelResetTimeout = setTimeout(() => {
      for (const m of this._members.values()) {
        m.audioLevel.set(0);
      }
    }, Math.ceil(aloInterval * 1.25));
  }

  private onMemberName(m: RoomMember, oldName: string) {
    const mm = this.uuid2member.get(m.uuid);
    if (mm) {
      mm.name.set(m.name);
      if (mm.itsme) {
        nickname.set(m.name);
      } else if (oldName) {
        sendNotification({
          text: t('meeting.member_changed_name', {
            member: oldName,
            name: m.name,
          }),
          timeout: recommendedTimeout,
        });
      }
    }
  }

  private onMemberJoined(m: RoomMember) {
    const mm = new MeetingMember(this, m);
    this._members = [...this._members, mm];
    this.uuid2member.set(mm.uuid, mm);
    this.members.set(this._members);

    sendNotification({
      text: t('meeting.member_joined', { member: m.name }),
      timeout: recommendedTimeout,
    });

    audioMemberJoined.currentTime = 0;
    audioMemberJoined.play().catch(catchLogResolve(log, 'audioMemberJoined.play()'));
  }

  private onRoomMessage() {
    audioChatMessage.currentTime = 0;
    audioChatMessage.play().catch(catchLogResolve(log, 'audioChatMessage.play()'));
  }

  private onMemberLeft(m: RoomMember) {
    this._members = this._members.filter((v) => v.member !== m);
    this.uuid2member.delete(m.uuid);
    this.members.set(this._members);
    sendNotification({
      text: t('meeting.member_left', { member: m.name }),
      timeout: recommendedTimeout,
      style: 'warning',
    });
  }

  private onMemberBroadcast(data: [string, string, RoomMember?]) {
    const [member, message, m] = data;
    if (this.room.memberOwn === m) {
      // It is my message so no need to notify me
      return;
    }
    sendNotification({
      text: t('meeting.member_broadcast_message', {
        member,
        message,
      }),
    });
  }

  private onRecordingStatus(room: Room, started: boolean) {
    if (started) {
      audioRecordingStarted.currentTime = 0;
      audioRecordingStarted.play().catch(catchLogResolve(log, 'audioRecordingStarted.play()'));
    }
  }

  private onRoomName() {
    this.roomName.set(this.room.name);
  }

  private onRoomClosed() {
    const r = this.room;
    try {
      r.removeListener('roomClosed', this.onRoomClosed);
      r.removeListener('roomName', this.onRoomName);
      r.removeListener('memberJoined', this.onMemberJoined);
      r.removeListener('roomMessage', this.onRoomMessage);
      r.removeListener('memberLeft', this.onMemberLeft);
      r.removeListener('memberName', this.onMemberName);
      r.removeListener('memberBroadcast', this.onMemberBroadcast);
    } finally {
      closeAllMediaTracks();
      this.isClosed.set(r.isClosed);
    }
  }
}
