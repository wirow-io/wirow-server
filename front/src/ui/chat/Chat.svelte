<!-- svelte-ignore unused-export-let -->
<script lang="ts">
  import { get_current_component, tick } from 'svelte/internal';
  import type { SBindFn } from '../../interfaces';
  import type { ChatComponentBind, ChatMessage } from './chat';
  import ChatInput from './ChatInput.svelte';
  import ChatMessageComponent from './ChatMessage.svelte';
  import { fade } from 'svelte/transition';

  export let messages: ChatMessage[] = [];

  export let addMessages = async (_messages: ChatMessage[], scrollDown?: boolean) => {
    let shouldScrollDown: boolean | undefined;
    if (scrollDown !== undefined) {
      shouldScrollDown = scrollDown;
    }
    if (shouldScrollDown === undefined && chatBodyEl.scrollTop + chatBodyEl.clientHeight >= chatBodyEl.scrollHeight) {
      shouldScrollDown = true;
    }
    if (_messages.length) {
      messages = messages.concat(_messages);
    }
    if (shouldScrollDown === true) {
      await tick();
      chatBodyEl.scrollTop = chatBodyEl.scrollHeight;
    }
  };

  export let messagesClear = () => {
    messages = [];
  };

  export let inputClear: () => void;

  export let bind: SBindFn<ChatComponentBind> | undefined = undefined;

  let chatBodyEl: HTMLElement;

  bind?.(get_current_component(), {
    get addMessages(): typeof addMessages {
      return addMessages;
    },
    get inputClear(): typeof inputClear {
      return inputClear;
    },
    get messagesClear(): typeof messagesClear {
      return messagesClear;
    },
  });
</script>

<template>
  <div class="chat" in:fade>
    <div class="header" />
    <div class="chat-body" bind:this={chatBodyEl}>
      <div class="flex-expand" />
      {#each messages as m (m.id)}
        <ChatMessageComponent message={m} />
      {/each}
    </div>
    <ChatInput autofocus on:send bind:clear={inputClear} />
  </div>
</template>
