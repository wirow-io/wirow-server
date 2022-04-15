<script lang="ts">
  import env from './env';
  import Modal from './kit/Modal.svelte';
  import Notifications from './kit/Notifications.svelte';
  import type { RouterSpec } from './router';
  import Router from './router/Router.svelte';
  import Compatibility, { compatibilityMatcher } from './ui/Compatibility.svelte';
  import Meeting, { routerMatcher as meetingRouterMatcher } from './ui/meeting/Meeting.svelte';

  import Welcome from './ui/Welcome.svelte';
  //import Palette from './kit/Palette.svelte';

  const routes: RouterSpec[] = [
    //[Palette, () => true],
    [Compatibility, compatibilityMatcher],
    [Meeting, meetingRouterMatcher],
    [Welcome, () => true],
  ];

  env.init();
</script>

<template>
  <Modal />
  <Notifications />
  <Router {routes} />
</template>

<style lang="scss">
  :global {
    @import '../css/global';
  }
</style>
