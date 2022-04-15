<script lang="ts">
  import Box from '../../kit/Box.svelte';
  import type { ChatMessage } from './chat';

  export let message: ChatMessage;
</script>

<svelte:options immutable={true} />
<template>
  <div class="chat-message-holder {message.isMy ? 'chat-message-holder-my' : 'chat-message-holder-other'}">
    <Box componentClass="chat-message" hoverable>
      <span class="time">{message.htime}</span>
      {#if !message.isMy}<span class="sender">{message.memberName}</span>{/if}
      {@html message.message}
    </Box>
  </div>
</template>
