<script lang="ts">
  import { _ } from 'svelte-intl';
  import Icon from '../../kit/Icon.svelte';
  import linkIcon from '../../kit/icons/link';
  import { Room, roomStore } from '../../room';
  import type { RoomConnectionState } from '../../room';
  import type { Readable } from 'svelte/store';

  let state: Readable<RoomConnectionState> | undefined;
  let time: Readable<number> | undefined;

  function onRoomUrl() {
    const room = $roomStore;
    if (room) {
      Room.copyUrl(room.uuid);
    }
  }

  function roomTime(t?: number): string {
    if (t !== undefined) {
      return `${Math.floor(t / 60)}:${(t % 60).toString().padStart(2, '0')}`;
    } else {
      return '';
    }
  }

  function updateRoom(room?: Room) {
    state = room?.stateMerged;
    time = room?.time;
  }

  $: updateRoom($roomStore);
</script>

<template>
  <div class="footer">
    <div><a href="https://wirow.io" target="_blank">https://wirow.io</a></div>
    <div class="flex-expand" />
    {#if state}
      <div>{$_('meeting.state.' + $state)}</div>
    {/if}
    <div class="flex-expand" />
    {#if time}
      <div>{roomTime($time)}</div>
    {/if}
    <Icon tooltip={$_('tooltip.copy_room_url')} on:click={onRoomUrl} icon={linkIcon} clickable size="1.2rem" />
  </div>
</template>
