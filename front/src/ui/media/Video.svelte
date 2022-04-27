<script lang="ts">
  import { onMount } from 'svelte';

  export let stream: MediaProvider | null = null;
  export let audioLevel = 0;
  export let componentClass = '';
  export let mirror = false;
  export let activeSpeaker = false;

  let videoRef: HTMLElement | undefined;
  let audioMeterRef: HTMLElement | undefined;
  let animationFrameId: number | undefined;
  let stateClass = '';

  function initVideo(..._: any[]) {
    const video = videoRef as HTMLMediaElement;
    if (video) {
      video.srcObject = stream;
    }
  }

  function updateAudioLevel(..._: any[]) {
    const amref = audioMeterRef;
    if (amref == undefined) {
      return;
    }
    const duration = 800;
    let npos = audioLevel;
    if (npos > 100) {
      npos = 100;
    }
    if (npos < 0) {
      npos = 0;
    }
    let cpos = 0;
    let width = amref.style.width;
    if (width == '') {
      cpos = 0;
    } else {
      const idx = width.indexOf('%');
      if (idx !== -1) {
        cpos = parseInt(width.substring(0, idx));
      }
    }
    const ipos = cpos;
    const delta = npos - ipos;
    const start = performance.now();

    if (animationFrameId) {
      cancelAnimationFrame(animationFrameId);
    }
    animationFrameId = requestAnimationFrame(function animate(time) {
      let fr = (time - start) / duration;
      if (fr > 1) fr = 1;
      amref.style.width = `${ipos + delta * easeInOutBack(fr)}%`;
      if (fr < 1) {
        animationFrameId = requestAnimationFrame(animate);
      }
    });

    function easeInOutBack(x: number): number {
      const c1 = 1.70158;
      const c2 = c1 * 1.525;
      return x < 0.5
        ? (Math.pow(2 * x, 2) * ((c2 + 1) * 2 * x - c2)) / 2
        : (Math.pow(2 * x - 2, 2) * ((c2 + 1) * (x * 2 - 2) + c2) + 2) / 2;
    }
  }

  function updateActiveSpeaker(val: boolean) {
    if (val === true) {
      stateClass = 'active-speaker';
      videoRef?.scrollIntoView({ block: 'end', behavior: 'smooth' });
    } else {
      stateClass = '';
    }
  }

  onMount(() => {
    updateAudioLevel();
    return () => {};
  });

  function onClick() {
    videoRef?.scrollIntoView({ block: 'end', behavior: 'smooth' });
  }

  $: initVideo(stream, videoRef);
  $: updateAudioLevel(audioLevel);
  $: updateActiveSpeaker(activeSpeaker);
</script>

<!-- svelte-ignore a11y-media-has-caption -->
<template>
  <div class="video-aspect-wrapper {stateClass} {componentClass}" on:click={onClick}>
    <div class="audio-meter" style="width:0" bind:this={audioMeterRef} />
    <div class="video-block"><video bind:this={videoRef} class:mirror playsinline autoplay /></div>
    <slot />
  </div>
</template>