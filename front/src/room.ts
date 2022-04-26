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

import { Device } from '@softmotions/mediasoup-client';
import type {
  ConnectionState,
  Consumer,
  ConsumerOptions,
  DtlsParameters,
  Producer,
  RtpCapabilities,
  RtpCodecCapability,
  RtpParameters,
  Transport,
  TransportOptions,
} from '@softmotions/mediasoup-client/lib/types';
import type { Readable } from 'svelte/store';
import { derived, writable } from 'svelte/store';
import {
  MRES_RECV_TRANSPORT,
  MRES_SEND_TRANSPORT,
  RCT_TYPE_CONSUMER,
  RCT_TYPE_PRODUCER,
  RTP_KIND_AUDIO,
  RTP_KIND_VIDEO,
} from './constants';
import { ExtendedEventEmitter } from './events';
import type { RoomType } from './interfaces';
import { Logger } from './logger';
import { stream as ownStream, streamVideo as ownVideoStream } from './media';
import type { MediaTrackKind } from './mediastream';
import { EnhancedMediaStream } from './mediastream';
import { recommendedTimeout, sendNotification } from './notify';
import { routeUrl } from './router';
import { t } from './translate';
import { getUserSnapshot, nickname } from './user';
import { catchLogResolve, copyToClipboard, storeGet } from './utils';
import type { WSMessage } from './ws';
import { send, sendAwait, subscribe } from './ws';

export type TransportDirection = 'recv' | 'send';

export type RoomConnectionState = ConnectionState | '';

/**
 * Meeting room creation request.
 */
export interface CreateRoomRequest {
  uuid?: string; /// Attach to existing room with given uuid
  type: RoomType; /// Type of room
  name: string; ///  Room name
  nickname: string; ///  Username
}

interface CreateRoomResponse {
  room: string; /// Room UUID
  cid: string; /// Room cid
  name: string; /// Room name
  flags: number;
  ts: number; /// Room creation timestamp
  rtpCapabilities: RtpCapabilities;
  member: string; /// Member UUID
  owner: boolean; /// If calling user is a room owner
  recording: boolean; /// True if recording session is active
  members: [member_uuid: string, member_name: string, is_owner: boolean][];
  whiteboard: string | undefined; /// Pre-generated whiteboard link (if exists)
}

interface InitTransportResponse {
  recvTransport: TransportOptions;
  sendTransport?: TransportOptions;
}

interface ConsumerCommand extends ConsumerOptions {
  cmd: 'consumer';
  memberId: string;
  producerPaused: boolean;
}

interface BroadcastMessageCommand {
  cmd: 'broadcast_message';
  message: string;
  member: string; // Member UUID
  member_name: string; // Member name at the time of sending broadcast message
}

interface MemberInfoCommand {
  cmd: 'member_info';
  member: string; // Member UUID
  name?: string; // Member name
}

interface RoomInfoCommand {
  cmd: 'room_info';
  name?: string; // Room name
}

interface ResourceEvent {
  type: number;
  id: string;
}

interface ResourceClosedEvent extends ResourceEvent {
  event: 'RESOURCE_CLOSED';
}

interface RoomMemberLeftEvent {
  event: 'ROOM_MEMBER_LEFT';
  room: string; // Room UUID
  member: string; // Member UUID
}

interface RoomMemberJoinEvent {
  event: 'ROOM_MEMBER_JOIN';
  room: string; // Room UUID
  member: string; // Member UUID
  name: string;
  owner: boolean;
}

interface ActiveSpeakerEvent {
  event: 'ACTIVE_SPEAKER';
  member: string;
}

interface VolumesEvent {
  event: 'VOLUMES';
  data: Array<[string, number]>;
}

export interface SilenceEvent {
  event: 'SILENCE';
}

const log = new Logger('Room');

/**
 * - roomClosed         (Room)          Room closed
 * - roomName           (Room)          Room name changed
 * - roomMessage        (Room, message) Recieved room chat message
 * - memberName         (RoomMember, oldName)  Room member changed its name
 * - memberJoined       (RoomMember)    Member is joined to the room
 * - memberLeft         (RoomMember)    Member is left the room
 * - memberBroadcast    [member_name, message, RoomMember?]
 *                      Member send a broadcast message
 * - recordingStatus    (Room, bool)    Meeting recording status changed
 * - volumes            (Room, Array<Array<[string, number]>>)
 * - silence            (Room)
 */
export type RoomEvents =
  | 'memberBroadcast'
  | 'memberJoined'
  | 'memberLeft'
  | 'memberName'
  | 'roomClosed'
  | 'roomMessage'
  | 'roomName'
  | 'recordingStatus'
  | 'volumes'
  | 'silence'
  | 'activeSpeaker';

const TRACKED_EVENTS = new Set([
  'VOLUMES',
  'SILENCE',
  'ROOM_MEMBER_JOIN',
  'ROOM_MEMBER_LEFT',
  'PRODUCER_SCORE',
  'CONSUMER_SCORE',
  'CONSUMER_LAYERS_CHANGED',
  'RESOURCE_CLOSED',
  'CONSUMER_PAUSED',
  'CONSUMER_RESUMED',
  'ROOM_CLOSED',
  'ROOM_RECORDING_ON',
  'ROOM_RECORDING_OFF',
  'ROOM_WHITEBOARD_INIT',
]);

const TRACKED_COMMANDS = new Set(['consumer', 'broadcast_message', 'message', 'member_info', 'room_info']);

export const roomStore = writable<Room | undefined>(undefined);

function rtpStreamKindAsFlags(kind: MediaTrackKind | string): number {
  switch (kind) {
    case 'video':
      return RTP_KIND_VIDEO;
    case 'audio':
      return RTP_KIND_AUDIO;
    default:
      throw new Error(`Unknown track kind: ${kind}`);
  }
}

export class RoomMember {
  readonly room: Room;
  readonly uuid: string;
  readonly stream: MediaStream;
  readonly isRoomOwner: boolean;
  audio?: Consumer;
  video?: Consumer;

  private _name: string;

  constructor(
    room: Room,
    uuid: string,
    name: string,
    isRoomOwner: boolean,
    stream: MediaStream = new EnhancedMediaStream()
  ) {
    this.room = room;
    this._name = name;
    this.uuid = uuid;
    this.stream = stream;
    this.isRoomOwner = isRoomOwner;
  }

  get itsme(): boolean {
    return this.room.memberOwn === this;
  }

  get name(): string {
    return this._name;
  }

  set name(n: string) {
    if (n !== this._name) {
      const old = this._name;
      this._name = n;
      if (this.itsme) {
        send({
          cmd: 'member_info_set',
          name: n,
        });
      }
      this.room.safeEmit('memberName', this, old);
    }
  }

  consumer(id: string): Consumer | undefined {
    if (this.audio?.id === id) {
      return this.audio;
    } else if (this.video?.id === id) {
      return this.video;
    }
    return undefined;
  }
}

/**
 * Events of type RoomEvents
 */
export class Room extends ExtendedEventEmitter<RoomEvents> {
  /// Copy room access URL into clipboard.
  static async copyUrl(uuid: string): Promise<void> {
    const url = routeUrl(uuid);
    const success = await copyToClipboard(url);
    sendNotification({
      html: t('notification.room_url_copied', { url }),
      timeout: success ? recommendedTimeout * 2 : 0,
      closeable: true,
    });
  }

  device = new Device();
  isClosed = true;
  _name!: string;
  uuid!: string;
  owner!: boolean;
  recording!: boolean;
  flags!: number;
  whiteboard?: string;

  readonly type: RoomType;
  readonly producers: Producer[] = [];
  readonly stateSend = writable<RoomConnectionState>('');
  readonly stateRecv = writable<RoomConnectionState>('');
  readonly stateMerged = derived<[Readable<RoomConnectionState>, Readable<RoomConnectionState>], RoomConnectionState>(
    [this.stateSend, this.stateRecv],
    ([ss, rs]): RoomConnectionState => {
      let ret: RoomConnectionState = '';
      if (ss === rs) {
        ret = ss;
      } else if (rs === '') {
        ret = ss;
      } else if (ss === '') {
        ret = rs;
      } else if (ss === 'failed' || rs === 'failed') {
        ret = 'failed';
      } else if (ss === 'connecting' || rs === 'connecting') {
        ret = 'connecting';
      }
      if (ret == 'new') {
        ret = 'connecting';
      }
      return ret;
    }
  );

  readonly uuid2member = new Map<string, RoomMember>();
  members: RoomMember[] = [];
  memberOwn?: RoomMember;

  rtpCapabilities!: RtpCapabilities;
  transportRecv!: Transport;
  transportSend?: Transport;

  private wsUnsubscribe?: () => void;

  get name(): string {
    return this._name;
  }

  set name(n: string) {
    if (n !== this._name) {
      this._name = n;
      if (this.owner) {
        send({
          cmd: 'room_info_set',
          name: n,
        });
      }
      // Do not update it in prediction of server message
      // Something on server can go wrong and we dont need to show incorrect name
    }
  }

  async init(request: CreateRoomRequest): Promise<void> {
    this.wsUnsubscribe = subscribe(this.onWSMessage);
    const resp = await sendAwait<CreateRoomResponse>({
      cmd: 'room_create',
      room: {
        uuid: request.uuid,
        name: request.name,
        type: request.type,
      },
      member: {
        name: request.nickname,
      },
    });
    log.debug.enabled && log.debug(`Room.init() | CreateRoomResponse: ${JSON.stringify(resp, null, 2)}`);
    this.isClosed = false;

    const { name, room, member, members, rtpCapabilities, owner, flags, recording, whiteboard } = resp;
    this._name = name;
    this.uuid = room;
    this.owner = owner;
    this.flags = flags;
    this.recording = recording;
    this.rtpCapabilities = {
      ...rtpCapabilities,
      // Fix wrong rotation when streaming to mobile firefox from other mobile browser (like safari)
      // https://mediasoup.org/documentation/v3/tricks/#rtp-capabilities-filtering
      headerExtensions: rtpCapabilities.headerExtensions?.filter((ext) => ext.uri !== 'urn:3gpp:video-orientation'),
    };
    this.whiteboard = whiteboard;

    const user = getUserSnapshot();
    if (user) {
      user.addRoomHistoryEntry({
        uuid: room,
        cid: resp.cid,
        ts: resp.ts,
        name,
      });
    }

    const myStream = request.type === 'meeting' || owner ? ownVideoStream : undefined;
    await this.device.load({ routerRtpCapabilities: this.rtpCapabilities });

    this.memberOwn = new RoomMember(this, member, storeGet(nickname), owner, myStream); // Video is only for me!
    this.members = this.syncOrderingOfMembers([
      ...members.map((m) => new RoomMember(this, m[0], m[1], m[2])),
      this.memberOwn,
    ]);

    this.uuid2member.clear();
    this.members.forEach((m) => this.uuid2member.set(m.uuid, m));

    await this.transportsInit();

    if (this.transportSend) {
      ownStream.addEventListener('addtrack', this.onOwnTrackAdd);
      ownStream.addEventListener('removetrack', this.onOwnTrackRemove);
      ownStream.addEventListener('disabletrack', this.onOwnTrackEnabled);
      ownStream.addEventListener('enabletrack', this.onOwnTrackEnabled);
      await this.produceTracks(ownStream);
    }

    send({
      cmd: 'acquire_room_streams',
    });

    if (recording) {
      this.safeEmit('recordingStatus', this, true);
      sendNotification({
        text: t('meeting.recording_on'),
        style: 'warning',
        timeout: recommendedTimeout,
      });
    }
  }

  close(): void {
    if (this.isClosed) {
      return;
    }
    this.isClosed = true;
    log.debug.enabled && log.debug('Room.close()');
    try {
      try {
        this.wsUnsubscribe?.();
      } catch (ignored) {
        // Ignored
      }

      this.transportClose(this.transportRecv);
      this.transportClose(this.transportSend);

      ownStream.removeEventListener('addtrack', this.onOwnTrackAdd);
      ownStream.removeEventListener('removetrack', this.onOwnTrackRemove);
      ownStream.removeEventListener('disabletrack', this.onOwnTrackEnabled);
      ownStream.removeEventListener('enabletrack', this.onOwnTrackEnabled);
    } finally {
      this.safeEmit('roomClosed', this);
      if (storeGet(roomStore) === this) {
        roomStore.set(undefined);
      }
    }
  }

  async toggleRecording(): Promise<unknown> {
    return sendAwait({
      cmd: 'recording',
    });
  }

  openWhiteboard(): void {
    window.open(this.whiteboard);
    send({
      cmd: 'whiteboard_open',
    });
  }

  async leave(message?: string): Promise<unknown> {
    if (this.isClosed) {
      return;
    }
    return sendAwait({
      cmd: 'room_leave',
      message,
    })
      .catch(catchLogResolve(log, 'cmd:room_leave'))
      .finally(() => this.close());
  }

  constructor(type: RoomType) {
    super();
    this.type = type;
    this.onOwnTrackAdd = this.onOwnTrackAdd.bind(this);
    this.onOwnTrackRemove = this.onOwnTrackRemove.bind(this);
    this.onOwnTrackEnabled = this.onOwnTrackEnabled.bind(this);
    this.produceTrack = this.produceTrack.bind(this);
    this.onWSMessage = this.onWSMessage.bind(this);
  }

  private onOwnTrackAdd(ev: MediaStreamTrackEvent) {
    this.produceTrack(ev.track).catch(catchLogResolve(log));
  }

  private onOwnTrackRemove(ev: MediaStreamTrackEvent) {
    this.produceCloseTrack(ev.track);
  }

  private onOwnTrackEnabled(ev: MediaStreamTrackEvent) {
    log.debug.enabled && log.info('onOwnTrackEnabled() | called');
    const track = ev.track;
    const producer = this.producers.find((p) => p.track === track);
    if (producer) {
      if (track.enabled === true /*  && producer.paused === true */) {
        log.debug.enabled && log.info('onOwnTrackEnabled() | producerResume()');
        this.producerResume(producer);
      } else if (track.enabled === false /*  && producer.paused === false */) {
        log.debug.enabled && log.info('onOwnTrackEnabled() | producerPause()');
        this.producerPause(producer);
      }
    } else {
      this.produceTrack(track);
    }
  }

  private produceCloseAllOfKind(kind: string) {
    this.producers.filter((p) => p.track?.kind === kind).forEach((p) => p.close());
  }

  private produceCloseTrack(track: MediaStreamTrack) {
    this.producers.filter((p) => p.track?.id === track.id).forEach((p) => p.close());
  }

  private initProducer(producer: Producer): Producer {
    producer.addListener('trackended', onProducerClosed.bind(onProducerClosed, 'trackended', this, producer));
    producer.addListener('transportclose', onProducerClosed.bind(onProducerClosed, 'transportclose', this, producer));

    function onProducerClosed(this: typeof onProducerClosed, eventType: string, room: Room, producer: Producer) {
      log.debug.enabled && log.debug(`Room.onProducerClosed() | ${eventType} event`);
      producer.removeListener(eventType, this);
      const idx = room.producers.indexOf(producer);
      if (idx > -1) {
        room.producers.splice(idx, 1);
        if (eventType !== 'transportclose') {
          sendAwait({
            cmd: 'producer_close',
            id: producer.id,
          }).catch(catchLogResolve(log, 'cmd:producer_close'));
        }
      }
      log.debug.enabled &&
        log.debug(`Room.onProducerClosed() | Sending producer_close command, producer: ${producer.id}`);
    }

    return producer;
  }

  private async produceTrackAudio(track: MediaStreamTrack): Promise<Producer> {
    const trn = this.transportSend!;
    const producer = await trn.produce({
      track,
      stopTracks: false,
      disableTrackOnPause: true,
      codecOptions: {
        opusStereo: false,
        opusFec: true,
        opusDtx: false,
      },
      appData: { paused: !track.enabled },
    });
    if (producer.appData) {
      delete producer.appData.paused;
    }
    this.producers.push(this.initProducer(producer));
    return producer;
  }

  private async produceTrackVideo(track: MediaStreamTrack): Promise<Producer> {
    const trn = this.transportSend!;
    const screenSharing = (<any>track).$screenSharing ?? false;
    let codec: RtpCodecCapability | undefined;
    let encodings: any;

    if (screenSharing) {
      // FIXME: Mediasoup reported FF has no VP9 RTP caps
      //codec = this.device.rtpCapabilities.codecs?.find((codec) => codec.mimeType.toLowerCase() === 'video/vp9');
      //if (codec) {
      //   encodings = [{ scalabilityMode: 'S3T3', dtx: true }];
      // } else {
      codec = this.device.rtpCapabilities.codecs?.find((codec) => codec.mimeType.toLowerCase() === 'video/vp8');
      if (codec) {
        encodings = [{ dtx: true }];
      }
      //}
    } else {
      codec = this.device.rtpCapabilities.codecs?.find((codec) => codec.mimeType.toLowerCase() === 'video/vp8');
      if (codec) {
        encodings = [{ dtx: true }];
      }
    }
    // FIXME:
    // } else { todo: we have a problems with safari
    //   codec = this.device.rtpCapabilities.codecs?.find((codec) => codec.mimeType.toLowerCase() === 'video/h264');
    // }

    const producer = await trn.produce({
      track,
      stopTracks: screenSharing,
      disableTrackOnPause: true,
      codec,
      encodings,
      codecOptions: {
        videoGoogleStartBitrate: 1000,
      },
      appData: { paused: !track.enabled, screenSharing },
    });
    if (producer.appData) {
      delete producer.appData.paused;
    }
    this.producers.push(this.initProducer(producer));
    return producer;
  }

  private async produceTrack(track: MediaStreamTrack): Promise<Producer> {
    this.produceCloseAllOfKind(track.kind);
    if (track.kind === 'audio') {
      return this.produceTrackAudio(track);
    } else if (track.kind === 'video') {
      return this.produceTrackVideo(track);
    } else {
      return Promise.reject(`Unknown track kind: ${track.kind}`);
    }
  }

  private produceTracks(stream: EnhancedMediaStream) {
    return Promise.all(stream.getTracks().map(this.produceTrack));
  }

  private async producerPause(producer: Producer) {
    log.debug.enabled && log.debug('Room.producerPause() | called');
    const track = producer.track;
    if (track && (!producer.paused || track.enabled === true)) {
      producer.pause();
      await sendAwait({
        cmd: 'producer_pause',
        id: producer.id,
      }).catch(catchLogResolve(log, 'Room.producerPause() | producer_pause'));
    }
  }

  private async producerResume(producer: Producer) {
    log.debug.enabled && log.debug('Room.producerResume() | called');
    const track = producer.track;
    if (track && (producer.paused || track.enabled === false)) {
      producer.resume();
      await sendAwait({
        cmd: 'producer_resume',
        id: producer.id,
      }).catch(catchLogResolve(log, 'Room.producerResume() | producer_resume'));
    }
  }

  private onTransportState(
    transport: Transport,
    direction: TransportDirection,
    state: ConnectionState,
    details?: string
  ) {
    log.debug.enabled && log.debug(`Room.onTransportState() | Transport[${direction}]: ${state} ${details || ''}`);
    if (transport.direction === 'send') {
      this.stateSend.set(state);
    } else if (transport.direction === 'recv') {
      this.stateRecv.set(state);
    }
    if (state === 'failed') {
      const tryInit = () => {
        this.transportsInit(transport.direction).catch((e) => {
          log.warn.enabled && log.warn(e);
          this.close();
        });
      };
      setTimeout(tryInit, 500);
    }
  }

  private onTransportConnect(
    transport: Transport,
    dtlsParameters: DtlsParameters,
    resolve: () => void,
    reject: (err: any) => void
  ) {
    sendAwait({
      cmd: 'transport_connect',
      uuid: transport.id,
      dtlsParameters,
    })
      .then(() => resolve()) // Strip any args to be passed into resolve()
      .catch(reject);
  }

  private onTransportProduce(
    transport: Transport,
    kind: MediaTrackKind,
    rtpParameters: RtpParameters,
    appData: any,
    resolve: (val: { id: string }) => void,
    reject: (err: any) => void
  ) {
    const paused = appData?.paused === true;
    sendAwait({
      cmd: 'transport_produce',
      uuid: transport.id,
      kind: rtpStreamKindAsFlags(kind),
      paused,
      rtpParameters,
    })
      .then((v: any) => resolve(v))
      .catch((e) => reject(e));
  }

  private transportClose(transport: Transport | undefined) {
    if (transport) {
      try {
        transport.close();
      } catch (_) {
        // Ignored
      }
    }
  }

  private transportUse(transport: Transport, direction: TransportDirection): Transport {
    transport.on('connect', ({ dtlsParameters }, resolve, reject) => {
      log.debug.enabled && log.debug('Room.transportUse() | Event: connect');
      this.onTransportConnect(transport, dtlsParameters, resolve, reject);
    });
    transport.on('connectionstatechange', (state) => {
      log.debug.enabled && log.debug(`Room.transportUse() | Event: connectionstatechange: ${state}`);
      this.onTransportState(transport, direction, state);
    });
    if (direction === 'send') {
      transport.on('produce', ({ kind, rtpParameters, appData }, resolve, reject) => {
        log.debug.enabled && log.debug(`Room.transportUse() | Event produce, kind: ${kind}`);
        this.onTransportProduce(transport, kind, rtpParameters, appData, resolve, reject);
      });
    }
    return transport;
  }

  private async transportsInit(direction?: TransportDirection) {
    const device = this.device;
    log.debug.enabled && log.debug(`Room.transportsInit() | ${JSON.stringify(this.device.rtpCapabilities, null, 2)}`);
    const resp = await sendAwait<InitTransportResponse>({
      cmd: 'transports_init',
      rtpCapabilities: this.device.rtpCapabilities,
      direction: direction ? (direction === 'send' ? MRES_SEND_TRANSPORT : MRES_RECV_TRANSPORT) : undefined,
    });
    if (direction === undefined || direction === 'recv') {
      this.transportRecv && this.transportClose(this.transportRecv);
      this.transportRecv = this.transportUse(device.createRecvTransport(resp.recvTransport), 'recv');
    }
    if (resp.sendTransport && (direction === undefined || direction === 'send')) {
      this.transportSend && this.transportClose(this.transportSend);
      this.transportSend = this.transportUse(device.createSendTransport(resp.sendTransport), 'send');
    }
  }

  private syncOrderingOfMembers(members: RoomMember[]) {
    let idx = members.findIndex((m) => m.itsme);
    if (idx > 0) {
      const m = members[idx];
      members.splice(idx, 1);
      members.unshift(m);
    }
    idx = members.findIndex((m) => m.isRoomOwner);
    if (idx > 0) {
      const m = members[idx];
      members.splice(idx, 1);
      members.unshift(m);
    }
    return members;
  }

  private onRoomMemberJoinEvent({ member: uuid, name, owner }: RoomMemberJoinEvent) {
    let member = this.uuid2member.get(uuid);
    if (member !== undefined) {
      member.name = name;
      return;
    }
    member = new RoomMember(this, uuid, name, owner);
    this.members.push(member);
    this.members = this.syncOrderingOfMembers(this.members);
    this.uuid2member.set(member.uuid, member);
    this.safeEmit('memberJoined', member);
  }

  private async onConsumeCommand(command: ConsumerCommand) {
    const member = this.uuid2member.get(command.memberId);
    if (member === undefined) {
      log.warn(`Room.onConsumeCommand() | No room member ${command.memberId} is found`);
      return;
    }
    if (typeof command.kind === 'number') {
      if (command.kind === RTP_KIND_VIDEO) {
        command.kind = 'video';
      } else if (command.kind === RTP_KIND_AUDIO) {
        command.kind = 'audio';
      }
    }
    log.debug.enabled && log.debug(`Room.onConsumeCommand() | command: ${JSON.stringify(command, null, 2)}`);
    const kind = command.kind!;
    const consumer = await this.transportRecv
      .consume({ ...command, ...{ appData: { touched: false, lastKnonwScore: undefined } } })
      .catch(catchLogResolve(log, 'transportRecv.consume()'));
    if (consumer == null) {
      return; // Consumer failed to create error is logged
    }

    consumer.addListener('trackended', onConsumerClosed.bind(onConsumerClosed, 'trackended', member, consumer));
    consumer.addListener('transportclose', onConsumerClosed.bind(onConsumerClosed, 'transportclose', member, consumer));

    member[kind]?.close(); // Close old consumer if exists
    member[kind] = consumer;

    if (command.producerPaused === true && consumer.track.enabled === true && consumer.appData.touched === false) {
      consumer.track.enabled = false;
    }
    if (member.stream instanceof EnhancedMediaStream) {
      member.stream.mergeTracks([consumer.track], { keepOtherKinds: true, stop: true });
    }

    // Notify server we ready to receive stream
    send({
      cmd: 'consumer_created',
      id: consumer.id,
    });

    function onConsumerClosed(
      this: typeof onConsumerClosed,
      eventType: string,
      member: RoomMember,
      consumer: Consumer
    ) {
      const kind = consumer.kind;
      log.debug.enabled &&
        log.debug(`Room.onConsumerClosed() | Event: ${eventType} Kind: ${kind} Consumer: ${consumer.id}`);
      member.stream.removeTrack(consumer.track);
      if (member[kind] === consumer) {
        member[kind] = undefined;
      }
      consumer.removeListener(eventType, this);
    }
  }

  private onConsumerClosedEvent({ id }: ResourceClosedEvent) {
    this.members.find((m) => {
      const consumer = m.consumer(id);
      if (consumer !== undefined) {
        consumer.appData.touched = true;
        log.debug.enabled && log.debug(`Room.onConsumerClosedEvent() | ${id}`);
        consumer.close();
        return true;
      }
      return false;
    });
  }

  private onProducerClosedEvent({ id }: ResourceClosedEvent) {
    const producer = this.producers.find((p) => p.id === id);
    producer?.close();
  }

  private onResourceClosedEvent(evt: ResourceClosedEvent) {
    switch (evt.type) {
      case RCT_TYPE_CONSUMER:
        this.onConsumerClosedEvent(evt);
        break;
      case RCT_TYPE_PRODUCER:
        this.onProducerClosedEvent(evt);
        break;
    }
  }

  private onConsumerPausedResumedEvent({ id }: ResourceEvent, paused: boolean) {
    this.members.find((m) => {
      const consumer = m.consumer(id);
      if (consumer !== undefined) {
        log.debug.enabled && log.debug(`Room.onConsumerPausedResumed() | consumer: ${id} paused: ${paused}`);
        consumer.appData.touched = true;
        if (paused) {
          consumer.pause();
        } else {
          consumer.resume();
        }
        return true;
      }
      return false;
    });
  }

  private onRoomMemberLeftEvent({ member: uuid }: RoomMemberLeftEvent) {
    const idx = this.members.findIndex((m) => m.uuid === uuid);
    if (idx > -1) {
      const member = this.members[idx];
      this.members.splice(idx, 1);
      this.uuid2member.delete(member.uuid);
      try {
        member.audio?.close();
        member.video?.close();
      } catch (ignored) {
        // Ignored
      }
      if (member !== this.memberOwn) {
        this.safeEmit('memberLeft', member);
      } else {
        // We have unexpectedly received own memberLeft event
        // Kicked out of the room
        this.close();
      }
    }
  }

  private onBroadcastMessageCommand(bm: BroadcastMessageCommand) {
    const m = this.uuid2member.get(bm.member);
    this.safeEmit('memberBroadcast', [bm.member_name, bm.message, m]);
  }

  private onMemberInfoCommand(mi: MemberInfoCommand) {
    const m = this.uuid2member.get(mi.member);
    if (m && mi.name) {
      m.name = mi.name;
    }
  }

  private onRoomInfoCommand(rn: RoomInfoCommand) {
    if (rn.name) {
      this._name = rn.name;
      this.safeEmit('roomName', this);
      sendNotification({
        html: t('meeting.room_renamed', { name: rn.name }),
        timeout: recommendedTimeout,
        closeable: true,
      });
    }
  }

  private onVolumes(ev: VolumesEvent) {
    this.safeEmit('volumes', ev.data);
  }

  private onSilence() {
    this.safeEmit('silence', this);
  }

  private onActiveSpeaker(ev: ActiveSpeakerEvent) {
    this.safeEmit('activeSpeaker', ev.member);
  }

  private onWSCommand(cmd: string, msg: WSMessage) {
    switch (cmd) {
      case 'consumer':
        this.onConsumeCommand(<ConsumerCommand>msg);
        break;
      case 'broadcast_message':
        this.onBroadcastMessageCommand(<BroadcastMessageCommand>msg);
        break;
      case 'message':
        this.safeEmit('roomMessage', this, msg);
        break;
      case 'member_info':
        this.onMemberInfoCommand(<MemberInfoCommand>msg);
        break;
      case 'room_info':
        this.onRoomInfoCommand(<RoomInfoCommand>msg);
        break;
    }
  }

  private onWSEvent(event: string, msg: WSMessage) {
    switch (event) {
      case 'VOLUMES':
        this.onVolumes(<VolumesEvent>msg);
        break;
      case 'SILENCE':
        this.onSilence();
        break;
      case 'ACTIVE_SPEAKER':
        this.onActiveSpeaker(<ActiveSpeakerEvent>msg);
        break;
      case 'ROOM_MEMBER_JOIN':
        if (msg.room === this.uuid) {
          this.onRoomMemberJoinEvent(<RoomMemberJoinEvent>msg);
        }
        break;
      case 'ROOM_MEMBER_LEFT':
        if (msg.room === this.uuid) {
          this.onRoomMemberLeftEvent(<RoomMemberLeftEvent>msg);
        }
        break;
      case 'RESOURCE_CLOSED':
        this.onResourceClosedEvent(<ResourceClosedEvent>msg);
        break;
      case 'CONSUMER_PAUSED':
      case 'CONSUMER_RESUMED':
        this.onConsumerPausedResumedEvent(<ResourceEvent>msg, event === 'CONSUMER_PAUSED');
        break;
      case 'ROOM_CLOSED':
        sendNotification({
          text: t('meeting.room_closed'),
          style: 'error',
        });
        this.close();
        break;
      case 'ROOM_RECORDING_ON':
      case 'ROOM_RECORDING_OFF': {
        this.safeEmit('recordingStatus', this, event === 'ROOM_RECORDING_ON');
        sendNotification({
          text: t(event === 'ROOM_RECORDING_ON' ? 'meeting.recording_on' : 'meeting.recording_off'),
          style: 'warning',
          timeout: recommendedTimeout,
        });
        break;
      }
      case 'ROOM_WHITEBOARD_INIT': {
        sendNotification({
          text: t('meeting.whiteboard_opened', { name: msg.name }),
          actionTitle: t('meeting.open'),
          onAction: () => this.openWhiteboard(),
        });
        break;
      }
    }
  }

  private onWSMessage(msg: WSMessage | undefined) {
    if (!msg || msg.hook != null || (!TRACKED_EVENTS.has(msg.event) && !TRACKED_COMMANDS.has(msg.cmd))) {
      return;
    }
    log.debug.enabled && log.debug(`Room.onWSMessage() |\n${JSON.stringify(msg, null, 2)}`);
    if (msg.cmd !== undefined) {
      this.onWSCommand(msg.cmd, msg);
    } else if (msg.event !== undefined) {
      this.onWSEvent(msg.event, msg);
    }
  }
}

export async function createRoom(request: CreateRoomRequest): Promise<Room> {
  log.debug.enabled && log.debug('createRoom() | Creating a room ', request);
  let room = storeGet<Room | undefined>(roomStore);
  room?.close();
  try {
    room = new Room(request.type);
    await room.init(request);
    roomStore.set(room);
  } catch (e) {
    log.error('Failed to create a room: ', request, e);
    room?.close();
    return Promise.reject(e);
  }
  return room;
}
