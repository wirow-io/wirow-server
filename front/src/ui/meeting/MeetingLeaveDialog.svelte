<script lang="ts">
  import { _ } from 'svelte-intl';
  import Box from '../../kit/Box.svelte';
  import Button from '../../kit/Button.svelte';
  import Input from '../../kit/Input.svelte';
  import type { MeetingRoom } from './meeting';

  export let room: MeetingRoom;
  export let onSuccess: ((message: string) => Promise<unknown>) | undefined = undefined;
  export let onCancel: (() => void) | undefined = undefined;

  const { members } = room;

  let value = '';
  let disabled = false;

  async function onAction() {
    if (onSuccess) {
      disabled = true;
      try {
        await onSuccess(value);
      } finally {
        disabled = false;
      }
    }
  }

  function onClose() {
    onCancel && onCancel();
  }
</script>

<template>
  <Box closeable on:close={onClose} componentClass="modal-small-width">
    <h3>{$_('MeetingLeaveDialog.title')}</h3>
    {#if room.isAllowedBroadcastMessages && ($members.length > 1 || value.length > 0)}
      <Input
        bind:value
        on:action={onAction}
        {disabled}
        title={$_('MeetingLeaveDialog.goodbye_title')}
        maxlength={128} />
    {/if}
    <div class="buttons">
      <Button autofocus componentClass="focus-visible" on:click={onClose} {disabled}>{$_('MeetingLeaveDialog.button_back')}</Button>
      <Button componentClass="error focus-visible" on:click={onAction} {disabled}>{$_('MeetingLeaveDialog.button_leave')}</Button>
    </div>
  </Box>
</template>
