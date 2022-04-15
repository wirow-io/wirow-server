<script lang="ts">
  import { onMount } from 'svelte';
  import { _ } from 'svelte-intl';
  import { debounce } from 'throttle-debounce';
  import Button from '../../kit/Button.svelte';
  import editIcon from '../../kit/icons/edit';
  import trashIcon from '../../kit/icons/trash';
  import userPlusIcon from '../../kit/icons/userPlus';
  import Input from '../../kit/Input.svelte';
  import { openModal, recommendedModalOptions } from '../../kit/Modal.svelte';
  import type { CloseModalFn } from '../../kit/Modal.svelte';
  import Table from '../../kit/table/Table.svelte';
  import Tbody from '../../kit/table/Tbody.svelte';
  import { t } from '../../translate';
  import type { AdminUser } from '../admin';
  import * as menu from '../menu';
  import UserEditDialog from './UserEditDialog.svelte';
  import UserRemoveDialog from './UserRemoveDialog.svelte';
  import { Config } from '../../config';
  import { sendNotification } from '../../kit/Notifications.svelte';
  import { recommendedTimeout } from '../../notify';

  let skip = 0;
  let limit = 1000;
  let query = '';

  let users: AdminUser[] | undefined = undefined;
  let usersTotalNumber: number | undefined = undefined;
  let loadUsersDebounce: (() => void) | undefined = undefined;

  async function onSuccess(close: CloseModalFn) {
    await loadUsers();
    close();
  }

  function onCreateUser() {
    if (usersTotalNumber !== undefined) {
      if (usersTotalNumber < (Config.limitRegisteredUsers || Infinity)) {
        openModal(UserEditDialog, recommendedModalOptions, (close: CloseModalFn) => ({
          onSuccess: () => onSuccess(close),
        }));
      } else {
        sendNotification({
          text: t('Users.error_max_users', {limit: Config.limitRegisteredUsers}),
          style: 'error',
          closeable: true,
          timeout: recommendedTimeout,
        });
      }
    }
  }

  function onEditUser(user: AdminUser) {
    openModal(UserEditDialog, recommendedModalOptions, (close: CloseModalFn) => ({
      user,
      onSuccess: () => onSuccess(close),
    }));
  }

  function onRemoveUser(user: AdminUser) {
    openModal(UserRemoveDialog, recommendedModalOptions, (close: CloseModalFn) => ({
      user,
      onSuccess: () => onSuccess(close),
    }));

    users = users && [...users];
  }

  function onSearchType(..._: any[]) {
    if (!loadUsersDebounce) {
      loadUsers();
      loadUsersDebounce = debounce(200, false, loadUsers);
    } else {
      loadUsersDebounce();
    }
  }

  async function loadUsers() {
    const resp = await fetch(`admin/user/list?skip=${skip}&limit=${limit}&query=${encodeURIComponent(query)}`);
    const data = await resp.json();
    users = [...data.users];
    usersTotalNumber = data.totalNumber;
    return users;
  }

  onMount(() => {
    menu.menuPush({
      id: 'user_create',
      icon: userPlusIcon,
      tooltip: () => t('Users.user_create'),
      onAction: () => onCreateUser(),
    });
    return () => {
      menu.menuPop();
    };
  });

  $: onSearchType(query);
</script>

<template>
  <div class="admin-panel-holder">
    <div class="admin-panel">
      {#if users !== undefined}
        <Input autofocus placeholder={$_('Users.search_placeholder')} bind:value={query} size="normal" rounded={false} />
        {#if users.length > 0}
          <div class="list">
            <Table componentClass="users-table">
              <Tbody>
                {#each users as u (u.name)}
                  <tr>
                    <td>{u.name}</td>
                    <td>{u.perms || ''}</td>
                    <td class="right">
                      <Button componentClass="transparent" icon={editIcon} on:click={() => onEditUser(u)} />
                      <Button componentClass="transparent" icon={trashIcon} on:click={() => onRemoveUser(u)} />
                    </td>
                  </tr>
                {/each}
              </Tbody>
            </Table>
          </div>
        {/if}
      {/if}
    </div>
  </div>
</template>
