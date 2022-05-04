<script lang="ts">
  import { _ } from 'svelte-intl';
  import Button from '../../kit/Button.svelte';
  import Icon from '../../kit/Icon.svelte';
  import crownIcon from '../../kit/icons/crown';
  import desktopIcon from '../../kit/icons/desktop';
  import fullScreenIcon from '../../kit/icons/fullScreen';
  import microphoneIcon from '../../kit/icons/microphone';
  import microphoneMuteIcon from '../../kit/icons/microphoneMute';
  import videoIcon from '../../kit/icons/video';
  import videoMuteIcon from '../../kit/icons/videoMute';
  import { audioInputDevices, audioInputMuted, sharingEnabled, videoInputDevices, videoInputMuted } from '../../media';
  import Video from '../media/Video.svelte';
  import VideoUserOverlay from '../media/VideoUserOverlay.svelte';
  import { meetingRoomStore } from './meeting';

  export let memberUuid: string;

  const room = $meetingRoomStore!;
  const member = room.memberByUUID(memberUuid)!;

  let { stream, name, audioEnabled, audioLevel, videoEnabled, isRoomOwner } = member;
  let { activeSpeaker: activeSpeakerStore } = room;
  let activeSpeaker = false;

  $: {
    activeSpeaker = !member.itsme && !member.fullscreen && $videoEnabled && $activeSpeakerStore === memberUuid;
  }
</script>

<template>
  <Video {stream} {activeSpeaker} audioLevel={$audioLevel * 0.5} mirror={member.itsme && !$sharingEnabled}>
    {#if !$videoEnabled}
      <VideoUserOverlay name={$name} />
    {/if}
    <div class="video-toolbar">
      {#if member.itsme}
        {#if $videoInputDevices.length > 0}
          <Button
            bind:toggled={$videoInputMuted}
            icon={videoIcon}
            toggleIcon={videoMuteIcon}
            toggleIconClass="toggled-onmedia"
            componentClass="onmedia"
            title={$_($videoInputMuted ? 'ActivityBar.tooltip_videoUnmute' : 'ActivityBar.tooltip_videoMute')}
          />
        {/if}
        {#if $audioInputDevices.length > 0}
          <Button
            bind:toggled={$audioInputMuted}
            icon={microphoneIcon}
            toggleIcon={microphoneMuteIcon}
            toggleIconClass="toggled-onmedia"
            componentClass="onmedia"
            title={$_($audioInputMuted ? 'ActivityBar.tooltip_audioUnmute' : 'ActivityBar.tooltip_audioMute')}
          />
        {/if}
        <div class="flex-expand" />
        <Button
          autotoggle={false}
          componentClass="onmedia"
          icon={desktopIcon}
          on:click={() => member.onToggleSharing()}
          toggleIcon={desktopIcon}
          toggleIconClass="toggled-onmedia"
          toggled={$sharingEnabled}
          title={$_($sharingEnabled ? 'ActivityBar.tooltip_shareScreenStop' : 'ActivityBar.tooltip_shareScreen')}
        />
        <Button
          on:click={() => member.toggleFullscreen()}
          toggled={member.fullscreen}
          autotoggle={false}
          componentClass="onmedia"
          icon={fullScreenIcon}
          toggleIcon={fullScreenIcon}
          toggleIconClass="toggled-onmedia"
          title={$_(
            member.fullscreen ? 'MeetingGridUnit.tooltip_exit_fullscreen' : 'MeetingGridUnit.tooltip_enter_fullscreen'
          )}
        />
      {:else}
        <Button
          hidden={$videoEnabled}
          toggled={!$videoEnabled}
          clickable={false}
          icon={videoIcon}
          toggleIcon={videoMuteIcon}
          toggleIconClass="toggled-onmedia"
          componentClass="transparent"
        />
        <Button
          hidden={$audioEnabled}
          toggled={!$audioEnabled}
          clickable={false}
          icon={microphoneIcon}
          toggleIcon={microphoneMuteIcon}
          toggleIconClass="toggled-onmedia"
          componentClass="transparent"
        />
        <div class="flex-expand" />
        <Button
          on:click={() => member.toggleFullscreen()}
          toggled={member.fullscreen}
          autotoggle={false}
          componentClass="onmedia"
          icon={fullScreenIcon}
          toggleIcon={fullScreenIcon}
          toggleIconClass="toggled-onmedia"
        />
      {/if}
    </div>
    <div class="video-footer">
      <div class="caption" class:itsme={member.itsme}>
        {#if isRoomOwner}
          <Icon size="" icon={crownIcon} />
        {/if}
        {$name}
      </div>
    </div>
  </Video>
</template>
