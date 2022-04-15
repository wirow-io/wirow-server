<script lang="ts">
  import Input from '../../kit/Input.svelte';
  import { _ } from 'svelte-intl';
  import { getDevicesStore } from '../../media';
  import MediaDevicesSelector from '../media/MediaDevicesSelector.svelte';
  import { createValidationContext } from '../../validate';
  import { meetingRoomStore } from './meeting';
  import { onMount } from 'svelte';
  import { fade } from 'svelte/transition';

  const videoDevices = getDevicesStore('videoinput');
  const audioInputDevices = getDevicesStore('audioinput');
  const member = $meetingRoomStore?.memberOwn;
  const memberNameStore = member?.name;
  const { validate } = createValidationContext();

  let memberName: string = '';
  let roomName = $meetingRoomStore ? $meetingRoomStore.name : '';

  function displayNameValidator(errors: string[], value?: string): any {
    if (value == null || value.trim() === '') {
      const msg = $_('MeetingPrepareDialog.please_provide_display_name');
      errors.push(msg);
      return msg;
    }
  }

  function onUpdateMemberName() {
    let oldName = memberNameStore ? $memberNameStore : '';
    if (oldName === memberName || validate().length) {
      return;
    }
    oldName = memberName;
    const roomMember = $meetingRoomStore?.memberOwn?.member;
    if (roomMember) {
      roomMember.name = memberName;
    }
  }

  function onUpdateRoomName() {
    let oldName = $meetingRoomStore ? $meetingRoomStore.name : '';
    if (oldName === roomName || validate().length) {
      return;
    }
    oldName = roomName;
    const meeting = $meetingRoomStore;
    if (meeting) {
      meeting.room.name = roomName;
    }
  }

  onMount(() => {
    return () => {
      onUpdateMemberName();
      onUpdateRoomName();
    };
  });

  $: {
    const mn = memberNameStore && $memberNameStore;
    if (mn) {
      memberName = mn;
    }
  }
</script>

<template>
  <div class="meeting-settings" in:fade>
    <ul class="form">
      <li class="side-area-caption">
        <h3>{$_('MeetingSettings.caption')}</h3>
      </li>

      {#if member?.isRoomOwner}
        <li>
          <Input
            bind:value={roomName}
            required
            on:blur={onUpdateRoomName}
            title={$_('MeetingPrepareDialog.room_display_name')}
            validator={displayNameValidator}
          />
        </li>
      {/if}

      <li>
        <Input
          bind:value={memberName}
          required
          on:blur={onUpdateMemberName}
          title={$_('MeetingPrepareDialog.your_display_name')}
          validator={displayNameValidator}
        />
      </li>

      {#if $meetingRoomStore?.type !== 'webinar' || $meetingRoomStore?.owner === true}
        {#if $videoDevices.length > 0}
          <li>
            <MediaDevicesSelector acquireTracks hideForSingleOption={false} kind="videoinput" />
          </li>
        {/if}
        {#if $audioInputDevices.length > 0}
          <li>
            <MediaDevicesSelector acquireTracks hideControls={false} kind="audioinput" />
          </li>
        {/if}
      {/if}
    </ul>
  </div>
</template>
