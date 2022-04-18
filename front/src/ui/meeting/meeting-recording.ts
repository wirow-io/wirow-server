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

import { Logger } from '../../logger';
import { catchLogResolve, storeGet } from '../../utils';
import type { MeetingContext } from './meeting';

const log = new Logger('MeetingRecording');

export function toggleRecording(ctx: MeetingContext): void {
  const mr = storeGet(ctx.meetingRoomStore);
  const s = ctx.findActivityBarSlot('recording');
  if (s == null || mr == null) {
    return;
  }
  mr.room.toggleRecording().catch(catchLogResolve(log));
}
