<script lang="ts" context="module">
  function compareValueEq(o1: any, o2: any): boolean {
    return o1?.deviceId === o2?.deviceId;
  }
</script>

<script lang="ts">
  import { _ } from 'svelte-intl';
  import type { SEvent } from '../../interfaces';
  import type { Option } from '../../kit/Select.svelte';
  import Select from '../../kit/Select.svelte';
  import {
    acquireMediaDevicesUI,
    getDevicesStore,
    preferredAudioInputDeviceId,
    preferredVideoInputDeviceId,
    stream,
    streamTrigger,
  } from '../../media';
  import type { MediaTrackKind } from '../../mediastream';

  export let kind: MediaDeviceKind = 'videoinput';
  export let value: MediaDeviceInfo | undefined = undefined;
  export let acquireTracks: boolean = false;
  export let hideForSingleOption: boolean = false;
  export let hideControls: boolean = false;

  const store = getDevicesStore(kind);
  let options: Option[] = [];
  let title: string | undefined;
  let showSelect: boolean = true;

  function createDeviceLabel(idx: number, s: MediaDeviceInfo) {
    if (s.label) {
      return s.label;
    } else {
      switch (s.kind) {
        case 'videoinput':
          return $_('MediaDeviceSelector.video_input_idx', { idx });
        case 'audioinput':
          return $_('MediaDeviceSelector.audio_input_idx', { idx });
        case 'audiooutput':
          return $_('MediaDeviceSelector.audio_output_idx', { idx });
        default:
          return $_('MediaDeviceSelector.unknown_device');
      }
    }
  }

  function createSelectTitle() {
    switch (kind) {
      case 'videoinput':
        return $_('MediaDeviceSelector.video_input');
      case 'audioinput':
        return $_('MediaDeviceSelector.audio_input');
      case 'audiooutput':
        return $_('MediaDeviceSelector.audio_output');
      default:
        return $_('MediaDeviceSelector.device');
    }
  }

  function getPreferredDeviceId(): string | undefined {
    switch (kind) {
      case 'videoinput':
        return $preferredVideoInputDeviceId;
      case 'audioinput':
        return $preferredAudioInputDeviceId;
      default:
        return undefined;
    }
  }

  async function changeValue() {
    if (acquireTracks) {
      if (kind === 'videoinput') {
        await acquireMediaDevicesUI(value?.deviceId);
      } else if (kind === 'audioinput') {
        await acquireMediaDevicesUI(undefined, value?.deviceId);
      }
    }
  }

  function getMediaTrackKind(): MediaTrackKind {
    if (kind === 'videoinput') {
      return 'video';
    } else {
      return 'audio';
    }
  }

  function onChange(evt: SEvent) {
    value = evt.detail?.value as MediaDeviceInfo;
    changeValue();
  }

  function updateStream(st?: any) {
    const trackKind = getMediaTrackKind();
    let track = stream.getTracks().find((t) => t.kind === trackKind);
    let trackSharing = stream.getTracks().find((t) => (<any>t).$screenSharing === true);
    let deviceId = track?.getSettings()?.deviceId || getPreferredDeviceId();
    let newValue = $store.find((d) => d.deviceId === deviceId) || $store[0];
    if (value !== newValue) {
      value = newValue;
      if (!st && !trackSharing) {
        changeValue();
      }
    }
  }

  function updateDevices(..._: any[]) {
    let i = 0;
    options = $store.map((s) => ({ label: createDeviceLabel(++i, s), value: s }));
    title = createSelectTitle();
    value == null && updateStream();
  }

  function updateControls(..._: any[]) {
    showSelect = !hideControls && (!hideForSingleOption || $store.length > 1);
  }

  $: updateDevices($store);
  $: updateStream($streamTrigger);
  $: updateControls(hideControls, hideForSingleOption, $store);
</script>

<template>
  {#if showSelect}
    <Select {value} {options} {title} valueComparator={compareValueEq} on:change={onChange} />
  {/if}
</template>
