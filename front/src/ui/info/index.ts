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
