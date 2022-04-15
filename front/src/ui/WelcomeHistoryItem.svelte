<script lang="ts">
  import { _ } from 'svelte-intl';
  import type { RoomHistoryEntry } from '../interfaces';
  import Button from '../kit/Button.svelte';
  import { Room } from '../room';
  import infoIcon from '../kit/icons/info';
  import copyIcon from '../kit/icons/copy';
  import trashIcon from '../kit/icons/trash';

  export let item: RoomHistoryEntry;
  export let onRoomAction: ((entry: RoomHistoryEntry) => void) | undefined = undefined;
  export let onRoomRemove: ((entry: RoomHistoryEntry) => void) | undefined = undefined;
  export let onRoomDetails: ((entry: RoomHistoryEntry) => void) | undefined = undefined;

  let itemEl: HTMLElement | undefined;

  function onRoomDetailsForward() {
    onRoomDetails && onRoomDetails(item);
  }

  function onRoomRemoveForward() {
    onRoomRemove && onRoomRemove(item);
  }

  function onCopyRoomUrl() {
    Room.copyUrl(item.uuid);
  }

  function onAction() {
    if (onRoomAction) {
      onRoomAction(item);
    }
  }

  function onKeyDown(ev: KeyboardEvent) {
    if (ev.target === itemEl && ev.key === 'Enter') {
      onAction();
    }
  }
</script>

<template>
  <div
    class="welcome-history-item"
    tabindex="0"
    bind:this={itemEl}
    on:keydown={onKeyDown}
    on:click|stopPropagation={onAction}
  >
    <div class="info">
      <strong>{item.name}</strong>
      <div class="time">
        {$_('time.medium', { time: new Date(item.ts) })}
        {$_('date.long', { date: new Date(item.ts) })}
      </div>
    </div>
    <div class="controls">
      <Button title={$_('HistoryItem.details')} componentClass="s" icon={infoIcon} on:click={onRoomDetailsForward} />
      <Button title={$_('HistoryItem.copy_room_url')} componentClass="s" icon={copyIcon} on:click={onCopyRoomUrl} />
      <Button title={$_('HistoryItem.remove')} componentClass="s" icon={trashIcon} on:click={onRoomRemoveForward} />
    </div>
  </div>
</template>
