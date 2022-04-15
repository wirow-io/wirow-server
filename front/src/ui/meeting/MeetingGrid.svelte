<script lang="ts">
  import type { MeetingMember, MeetingRoom } from './meeting';
  import MeetingGridUnit from './MeetingGridUnit.svelte';
  import { scale } from 'svelte/transition';

  export let room: MeetingRoom;
  const { membersOnGrid, memberOnFullScreen } = room;

  function memberUuid(member: MeetingMember | undefined) {
    return member?.uuid || '';
  }
</script>

<template>
  {#if $memberOnFullScreen}
    <div class="fullscreen" in:scale>
      <MeetingGridUnit memberUuid={memberUuid($memberOnFullScreen)} />
    </div>
  {/if}
  <div class="meeting-grid-scroll-wrapper" class:excluded={$memberOnFullScreen}>
    <div class="meeting-grid-scroll">
      <div class="meeting-grid" class:meetingGridSolo={$membersOnGrid.length == 1}>
        {#each $membersOnGrid as member (member.uuid)}
          {#if member.uuid !== memberUuid($memberOnFullScreen)}
            <MeetingGridUnit memberUuid={member.uuid} />
          {/if}
        {/each}
      </div>
    </div>
  </div>
</template>
