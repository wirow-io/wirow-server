<script lang="ts">
  import type { MeetingMember } from '../meeting';
  import crownIcon from '../../../kit/icons/crown';
  import Icon from '../../../kit/Icon.svelte';

  export let member: MeetingMember;
  const { name, isRoomOwner } = member;
</script>

<template>
  <div class="meeting-member">
    <div class="name">
      {#if isRoomOwner}
        <Icon size="" icon={crownIcon} />
      {/if}
      {$name}
    </div>
  </div>
</template>
