<script lang="ts">
  import env from '../env';
  import Modal from '../kit/Modal.svelte';
  import Notifications from '../kit/Notifications.svelte';
  import type { RouterSpec } from '../router';
  import Router from '../router/Router.svelte';
  import Dashboard from './dashboard/Dashboard.svelte';
  import Menu from './Menu.svelte';
  import Users from './users/Users.svelte';

  const routes: RouterSpec[] = [
    [Users, (segment) => segment === 'users'],
    [Dashboard, () => true],
  ];

  env.init();
</script>

<template>
  <Modal />
  <Notifications />
  <div class="admin-wrapper">
    <Menu />
    <div style="overflow:hidden;">
      <Router {routes} />
    </div>
    <div class="admin-footer" />
  </div>
</template>

<style lang="scss">
  :global {
    @import '../../css/admin';
  }
</style>
