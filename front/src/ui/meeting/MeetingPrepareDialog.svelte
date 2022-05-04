<script lang="ts">
  import { onMount,tick } from 'svelte';
  import { _ } from 'svelte-intl';
  import { checkAutoplay } from '../../checks';
  import { RCT_ROOM_MEETING,RCT_ROOM_WEBINAR } from '../../constants';
  import type { RoomType } from '../../interfaces';
  import Box from '../../kit/Box.svelte';
  import Button from '../../kit/Button.svelte';
  import Icon from '../../kit/Icon.svelte';
  import cogIcon from '../../kit/icons/cog';
  import linkIcon from '../../kit/icons/link';
  import microphoneIcon from '../../kit/icons/microphone';
  import microphoneMuteIcon from '../../kit/icons/microphoneMute';
  import userFriendsIcon from '../../kit/icons/userFriends';
  import videoIcon from '../../kit/icons/video';
  import videoMuteIcon from '../../kit/icons/videoMute';
  import Input from '../../kit/Input.svelte';
  import Loader from '../../kit/Loader.svelte';
  import { closeModal,updateCurrentModalOpts } from '../../kit/Modal.svelte';
  import { sendNotification } from '../../kit/Notifications.svelte';
  import {
  acquireMediaDevices,
  audioInputMuted,
  closeAllMediaTracks,
  getDevicesStore,
  mediaDevicesAcquireState,
  streamTrigger,
  streamVideo,
  videoInputDevices,
  videoInputMuted
  } from '../../media';
  import { recommendedTimeout } from '../../notify';
  import { canCreateRoomOfType } from '../../permissions';
  import { createRoom,Room } from '../../room';
  import { roomBrowserUUID,route } from '../../router';
  import { nickname } from '../../user';
  import { createValidationContext,getValidate } from '../../validate';
  import { sendAwait } from '../../ws';
  import MediaDevicesSelector from '../media/MediaDevicesSelector.svelte';
  import Video from '../media/Video.svelte';
  import VideoUserOverlay from '../media/VideoUserOverlay.svelte';

  //const log = new Logger('meeting');

  export let type: RoomType = 'meeting';

  let displayName = '';
  let openSettings = false;

  let createPending = false; // Pending room creation request
  let autoJoinPending = true; // Pending automatic room join
  let completed = false; // Join room creation op completed
  let autoplay = true; // Can video autoplayed

  // Room attrs
  let name = '';
  let roomType: RoomType = type;
  let alive = false;
  let owner = false;
  let timeout: any = undefined;

  let loadingStatus = $_('MeetingPrepareDialog.initializing');

  const { validate, valid } = createValidationContext();
  const ownValidate = getValidate();
  const allDevices = getDevicesStore();
  const videoDevices = getDevicesStore('videoinput');
  const audioInputDevices = getDevicesStore('audioinput');

  async function onCreate() {
    if (createPending || !$valid || validate().length) {
      return;
    }
    createPending = true;
    completed = false;

    nickname.set(displayName);
    const uuid = $roomBrowserUUID;
    return createRoom({
      uuid,
      name,
      nickname: displayName,
      type: roomType,
    })
      .then((room) => {
        completed = true;
        route(room.uuid, true);
        setTimeout(() => closeModal(), 0);
      })
      .catch((e) => {
        createPending = false;
        sendNotification({
          text: $_(e.translated || 'MeetingPrepareDialog.failed_to_create_a_room'),
          style: 'error',
        });
      });
  }

  function titleValidator(errors: string[], value?: string): any {
    if (value == null || value.trim() === '') {
      const msg = $_('MeetingPrepareDialog.please_provide_a_room_title');
      errors.push(msg);
      return msg;
    }
  }

  function displayNameValidator(errors: string[], value?: string): any {
    if (value == null || value.trim() === '') {
      const msg = $_('MeetingPrepareDialog.please_provide_display_name');
      errors.push(msg);
      return msg;
    }
  }

  async function roomAwaitActive(uuid: string): Promise<boolean> {
    loadingStatus = $_('Public.awaiting_start');
    clearTimeout(timeout);
    return new Promise<boolean>((resolve) => {
      async function check() {
        try {
          const resp = await fetch(`status?ref=${encodeURIComponent(uuid)}`);
          if (resp.status !== 200) {
            resolve(false);
            return;
          }
          const data = await resp.json();
          if (data.active === true) {
            resolve(true);
          }
          timeout = setTimeout(check, 5000);
        } catch (e) {
          console.error(e);
          resolve(false);
        }
      }
      timeout = setTimeout(check, 5000);
    });
  }

  async function roomJoin(uuid: string | undefined): Promise<string | null> {
    autoJoinPending = true;
    completed = false;

    if (uuid == null) {
      return null;
    }
    loadingStatus = $_('MeetingPrepareDialog.initializing');
    updateCurrentModalOpts({ closeOnEscape: false, closeOnOutsideClick: false });
    const info = await sendAwait({
      cmd: 'room_info_get',
      uuid,
    });
    const flags = info.flags || 0;
    if (flags & RCT_ROOM_MEETING) {
      roomType = 'meeting';
    } else if (flags & RCT_ROOM_WEBINAR) {
      roomType = 'webinar';
    }

    name = info.name || '';
    uuid = info.uuid;
    alive = !!info.alive;
    owner = !!info.owner;
    const nn = $nickname;

    if (!alive && !canCreateRoomOfType(roomType)) {
      alive = await roomAwaitActive(info.uuid);
    }

    if (!alive) {
      if (!canCreateRoomOfType(roomType)) {
        return Promise.reject('error.cannot_join_inactive_room');
      }
      sendNotification({
        text: $_('MeetingPrepareDialog.room_is_not_active{name}', { name }),
        timeout: recommendedTimeout,
      });
      return null;
    }

    if (nn == null || nn == '') {
      sendNotification({
        text: $_('MeetingPrepareDialog.room_missing_name'),
        timeout: recommendedTimeout * 2,
      });
      return null;
    }

    if (owner === true || roomType === 'meeting') {
      loadingStatus = $_('MeetingPrepareDialog.acquiring_local_video');
      await acquireMediaDevices().catch((e) => {
        console.error(e);
        sendNotification({
          text: $_('MeetingPrepareDialog.failed_to_acquire_devices'),
          style: 'error',
        });
      });
      loadingStatus = $_('MeetingPrepareDialog.joining_the_room');
    }

    if (roomType === 'webinar') {
      autoplay = await checkAutoplay();
      if (autoplay === false) {
        return null;
      }
    }
    // Try to autojoin into the room
    const room = await createRoom({
      uuid,
      name,
      nickname: nn,
      type: roomType,
    });
    completed = true;
    return room.uuid;
  }

  function title() {
    switch (roomType) {
      case 'webinar':
        return $_('MeetingPrepareDialog.webinar');
      case 'meeting':
        return $_('MeetingPrepareDialog.meeting');
      default:
        return '';
    }
  }

  const videoDevicesUnsubscribe = videoDevices.subscribe(async () => {
    await tick();
    ownValidate();
  });

  onMount(() => {
    return () => {
      clearTimeout(timeout);
      videoDevicesUnsubscribe();
      if (!completed) {
        // Operation is not completed - room is not created
        closeAllMediaTracks();
        route('');
      }
    };
  });

  roomJoin($roomBrowserUUID)
    .then((uuid) => {
      if (uuid) {
        // We are in the room
        closeModal();
        route(uuid, true);
      } else {
        autoJoinPending = false;
        updateCurrentModalOpts({ closeOnEscape: true, closeOnOutsideClick: true });
      }
    })
    .catch((e) => {
      autoJoinPending = false;
      closeModal();
      let text = $_('MeetingPrepareDialog.failed_to_join_the_room');
      if (typeof e === 'string' && e.startsWith('error.')) {
        text = $_(e);
      } else {
        console.error(e);
      }
      sendNotification({
        text,
        style: 'error',
      });
    });

  function onCopyRoomUrl() {
    const uuid = $roomBrowserUUID;
    if (uuid) {
      Room.copyUrl(uuid);
    }
  }

  $: {
    displayName = $nickname || '';
  }
</script>

<template>
  {#if autoJoinPending}
    <Box closeable on:close={() => closeModal()} componentStyle="min-width:min(30rem,95vw);text-align:center;">
      <Loader />
      {loadingStatus}
    </Box>
  {:else}
    <Box closeable on:close={() => closeModal()} componentClass="modal-medium-width">
      <ul class="form">
        <li>
          <h2>
            {#if $roomBrowserUUID}
              <Icon icon={linkIcon} clickable componentClass="inline" on:click={onCopyRoomUrl} />
            {/if}
            {title()}
          </h2>
        </li>
        <li>
          <Input
            bind:value={displayName}
            on:action={onCreate}
            required
            autofocus={displayName === ''}
            title={$_('MeetingPrepareDialog.your_display_name')}
            validator={displayNameValidator}
          />
        </li>
        <li>
          <Input
            autofocus={displayName !== ''}
            bind:value={name}
            readonly={alive === true}
            icon={userFriendsIcon}
            on:action={onCreate}
            required={!alive}
            title={$_('MeetingPrepareDialog.room_title')}
            validator={titleValidator}
          />
        </li>
        {#if alive === false || owner === true || roomType === 'meeting'}
          <li>
            <Video stream={streamVideo} mirror componentClass="no-overflow">
              {#if $videoInputMuted || ($streamTrigger && streamVideo.getVideoTracks().length == 0)}
                <VideoUserOverlay name={displayName} />
              {/if}
              <div class="video-toolbar">
                {#if $videoInputDevices.length > 0}
                  <Button
                    bind:toggled={$videoInputMuted}
                    icon={videoIcon}
                    toggleIcon={videoMuteIcon}
                    toggleIconClass="toggled-onmedia"
                    componentClass="x onmedia"
                    title={$_($videoInputMuted ? 'ActivityBar.tooltip_videoUnmute' : 'ActivityBar.tooltip_videoMute')}
                  />
                {/if}
                {#if $audioInputDevices.length > 0}
                  <Button
                    bind:toggled={$audioInputMuted}
                    icon={microphoneIcon}
                    toggleIcon={microphoneMuteIcon}
                    toggleIconClass="toggled-onmedia"
                    componentClass="x onmedia"
                    title={$_($audioInputMuted ? 'ActivityBar.tooltip_audioUnmute' : 'ActivityBar.tooltip_audioMute')}
                  />
                {/if}
                {#if $allDevices.length > 0}
                  <Button
                    bind:toggled={openSettings}
                    icon={cogIcon}
                    toggleIconClass="toggled-onmedia"
                    componentClass="x onmedia"
                    title={$_(
                      openSettings ? 'MeetingPrepareDialog.close_settings' : 'MeetingPrepareDialog.open_settings'
                    )}
                  />
                {/if}
              </div>
            </Video>
          </li>
          {#if $videoDevices.length < 1}
            {#if $mediaDevicesAcquireState === 'done'}
              <li><i>{$_('MeetingPrepareDialog.no_input_devices')}</i></li>
            {/if}
          {/if}
          {#if $videoDevices.length > 0}
            <li>
              <MediaDevicesSelector acquireTracks hideForSingleOption={!openSettings} kind="videoinput" />
            </li>
          {/if}
          {#if $audioInputDevices.length > 0}
            <li>
              <MediaDevicesSelector acquireTracks hideControls={!openSettings} kind="audioinput" />
            </li>
          {/if}
        {/if}
        <li class="buttons">
          <Button
            loading={createPending}
            componentClass="x full-width"
            on:click={onCreate}
            disabled={!$valid || createPending}
          >
            {$roomBrowserUUID != null && alive
              ? $_('MeetingPrepareDialog.join_room')
              : $_('MeetingPrepareDialog.create_room')}
          </Button>
        </li>
      </ul>
    </Box>
  {/if}
</template>
