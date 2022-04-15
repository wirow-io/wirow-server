<script lang="ts">
  import { _ } from 'svelte-intl';
  import type { Readable } from 'svelte/store';
  import { emptyReadableArray } from '../../../utils';
  import { MeetingMember, MeetingRoom, meetingRoomStore } from '../meeting';
  import { fade } from 'svelte/transition';
  import MM from './MeetingMember.svelte';

  let room: MeetingRoom | undefined;
  let members: Readable<MeetingMember[]> = emptyReadableArray;

  function onRoomUpdated(..._: any[]) {
    room = $meetingRoomStore;
    if (room) {
      members = room.members;
    } else {
      members = emptyReadableArray;
    }
  }

  $: {
    onRoomUpdated($meetingRoomStore);
  }
</script>

<template>
  {#if room}
    <div class="meeting-members" in:fade>
      <div class="side-area-caption">
        <h3>{$_('MeetingMembers.caption')}</h3>
      </div>
      <div class="members">
        {#each $members.filter((m) => !m.itsme) as member (member.uuid)}
          <MM {member} />
        {/each}
      </div>
    </div>
  {/if}
</template>
