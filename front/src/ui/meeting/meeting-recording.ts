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
