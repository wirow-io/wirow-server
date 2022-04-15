import { onMount } from 'svelte/internal';
import type { SEvent } from '../../interfaces';
import { Logger } from '../../logger';
import { t } from '../../translate';
import { catchLogResolve, html2json } from '../../utils';
import type { WSMessage } from '../../ws';
import { sendAwait, subscribe as wsSubscribe } from '../../ws';
import type { ChatComponentBind, ChatMessage } from '../chat/chat';
import type Chat from '../chat/Chat.svelte';
import type { MeetingContext, MeetingRoom } from './meeting';
import { meetingRoomStore } from './meeting';

const log = new Logger('chat');

type ChatRawMessage = [
  /**
   * Message timestamp ms since epoch
   */
  time: number,

  /**
   * Sender UUID
   */
  memberId: string | null,

  /**
   * Sender nickname in chat
   */
  memberName: string,

  /**
   * Recipient UUID
   */
  recipientId: string | null,

  /**
   * Message html
   */
  message: string
];

interface ChatMessagesResponse {
  messages: ChatRawMessage[];
}

interface ChatMessageResponse {
  message: ChatRawMessage;
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
export function meetingChatAttach(ctx: MeetingContext): Record<string, any> {
  let room: MeetingRoom | undefined;
  let bind: ChatComponentBind;
  const { hasUnreadMessages } = ctx;

  function memberName(r: ChatRawMessage): string {
    const uuid = r[1];
    if (room == null || uuid == null) {
      return r[2]; // memberName
    }
    return room.memberByUUID(uuid)?.nameString ?? r[2];
  }

  function mapChatRawMessage(r: ChatRawMessage): ChatMessage {
    return {
      id: {},
      time: r[0],
      htime: t('time.short', { time: new Date(r[0]) }),
      memberId: r[1],
      memberName: memberName(r),
      recipientId: r[3],
      message: r[4],
      get isMy(): boolean {
        return this.memberId != null && this.memberId === room?.ownMemberUuid;
      },
    };
  }

  async function onSend(ev: SEvent) {
    const { html } = ev.detail;
    if (html == null) {
      return;
    }
    const { message } = await sendAwait<ChatMessageResponse>({
      cmd: 'room_message',
      message: html2json(html),
    });
    bind.addMessages([message].map(mapChatRawMessage), true);
    bind.inputClear();
  }

  function onWSMessage(msg: WSMessage | undefined) {
    if (!msg || msg.cmd !== 'message' || msg.hook != null) {
      return;
    }
    log.debug.enabled && log.debug('onWSMessage() | message: ', msg);
    const { message } = msg as ChatMessageResponse;
    bind.addMessages([message].map(mapChatRawMessage));
  }

  async function fetchMessages() {
    const { messages } = await sendAwait<ChatMessagesResponse>({
      cmd: 'room_messages',
    });
    log.debug.enabled && log.debug('fetchMessages() | messages: ', messages);
    bind.messagesClear();
    bind.addMessages(messages.map(mapChatRawMessage));
  }

  return {
    bind: (me: Chat, _bind: ChatComponentBind) => {
      bind = _bind;
      me.$on('send', onSend);

      onMount(() => {
        hasUnreadMessages.set(false);
        const roomUnsubscribe = meetingRoomStore.subscribe((_room) => {
          room = _room;
        });
        const wsUnsubscribe = wsSubscribe(onWSMessage);
        fetchMessages().catch(catchLogResolve(log, 'Failed to load chat messages'));
        return () => {
          wsUnsubscribe();
          roomUnsubscribe();
        };
      });
    },
  };
}
