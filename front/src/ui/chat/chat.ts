/**
 * Exported Chat bind interface
 */
export interface ChatComponentBind {
  addMessages: (messages: ChatMessage[], scrollDown?: boolean) => void;
  messagesClear: () => void;
  inputClear: () => void;
}

/**
 * Chat message for svelte component
 */
export interface ChatMessage {
  // eslint-disable-next-line @typescript-eslint/ban-types
  id: object | string;
  time: number;
  htime: string;
  isMy: boolean;
  memberId: string | null;
  memberName: string;
  recipientId: string | null;
  message: string;
}

