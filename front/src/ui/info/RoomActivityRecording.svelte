<script lang="ts">
  import type { RoomRecordingInfo } from '.';
  import Loader from '../../kit/Loader.svelte';
  import { _ } from 'svelte-intl';

  export let recording: RoomRecordingInfo;
</script>

<!-- svelte-ignore a11y-media-has-caption -->
<template>
  {#if recording.status === 'pending'}
    <div class="msg">
      <Loader caption={$_('RoomActivityRecording.pending')} />
    </div>
  {:else if recording.status === 'failed'}
    <div class="msg">{$_('RoomActivityRecording.failed')}</div>
  {:else if recording.status === 'success' && recording.url != null}
    <div class="video-aspect-wrapper">
      <div class="video-block">
        <video playsinline controls src={recording.url} />
      </div>
    </div>
  {/if}
</template>

<style lang="scss">
  .msg {
    margin: 4em;
    text-align: center;
  }
</style>
