<script lang="ts">
  import { _ } from 'svelte-intl';
  import Button from '../kit/Button.svelte';
  import desktopIcon from '../kit/icons/desktop';
  import microphoneIcon from '../kit/icons/microphone';
  import microphoneMuteIcon from '../kit/icons/microphoneMute';
  import videoIcon from '../kit/icons/video';
  import videoMuteIcon from '../kit/icons/videoMute';
  import { audioInputDevices,audioInputMuted,sharingEnabled,videoInputDevices,videoInputMuted } from '../media';
  import type { ActivityBarSlot } from './ActivityBarLib';
  import ActivityButton from './ActivityButton.svelte';
  import { meetingRoomStore } from './meeting/meeting';

  export let slots: ActivityBarSlot[] = [];

  const member = $meetingRoomStore?.memberOwn;
</script>

<template>
  <div class="activity-bar">
    <ul>
      {#each slots.filter((s) => s.side === 'top') as s (s.id)}
        <li>
          <ActivityButton {s} />
        </li>
      {/each}
    </ul>
    <ul>
      {#if $videoInputDevices.length > 0}
        <li>
          <Button
            icon={videoIcon}
            bind:toggled={$videoInputMuted}
            toggleIcon={videoMuteIcon}
            toggleIconClass="toggled-onmedia"
            title={$_($videoInputMuted ? 'ActivityBar.tooltip_videoUnmute' : 'ActivityBar.tooltip_videoMute')}
          />
        </li>
      {/if}
      {#if $audioInputDevices.length > 0}
        <li>
          <Button
            bind:toggled={$audioInputMuted}
            icon={microphoneIcon}
            toggleIcon={microphoneMuteIcon}
            toggleIconClass="toggled-onmedia"
            title={$_($audioInputMuted ? 'ActivityBar.tooltip_audioUnmute' : 'ActivityBar.tooltip_audioMute')}
          />
        </li>
      {/if}
      {#if member}
        <li>
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
        </li>
      {/if}
      {#each slots.filter((s) => s.side === 'bottom') as s (s.id)}
        <li>
          <ActivityButton {s} />
        </li>
      {/each}
    </ul>
  </div>
</template>
