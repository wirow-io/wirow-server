export type RoomActivityEvent = 'created' | 'closed' | 'renamed' | 'joined' | 'left' | 'recstart' | 'recstop' | 'whiteboard';

export interface RoomActivityEntry {
  ts: number;
  event: RoomActivityEvent;
  member?: string;
  old_name?: string;
  new_name?: string;
  link?: string; // For now is used to get opened whiteboard link in events list. Can be used to get any other link
}

export interface RoomChatEntry {
  ts: number;
  own: boolean;
  member: string;
  message: string;
}

export interface RoomRecordingInfo {
  status?: 'pending' | 'failed' | 'success';
  url?: string;
}

/// User sessions to the this room
export type UserSession = [cid: string, ts: number];

export interface RoomInfo {
  name: string;
  activity: RoomActivityEntry[];
  chat: RoomChatEntry[];
  recording: RoomRecordingInfo;
  history: UserSession[];
}
