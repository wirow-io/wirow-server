<script lang="ts">
  import { _ } from 'svelte-intl';
  import type { RoomChatEntry } from '.';
  import { t } from '../../translate';
  import type { ChatMessage } from '../chat/chat';
  import ChatMessageComponent from '../chat/ChatMessage.svelte';

  export let chat: RoomChatEntry[] = [];
  export let pageSize = 15; // Messages per page
  let currentPage = 1;
  let pages: number[] = [];

  let messages: ChatMessage[] = [];
  let pageMessages: ChatMessage[] = [];

  function setPage(num: number) {
    currentPage = num;
    const off = pageSize * (num - 1);
    pageMessages = messages.slice(off, off + pageSize);
  }

  function fillPages(): number[] {
    const res: number[] = [];
    const npages = Math.ceil(messages.length / pageSize);
    for (let i = 0; i < npages; ++i) {
      res.push(i + 1);
    }
    return res;
  }

  function init(..._: any[]) {
    messages = chat.map((e) => {
      return {
        id: {},
        time: e.ts,
        htime: t('time.short', { time: new Date(e.ts) }),
        isMy:  e.own,
        memberName: e.member,
        message: e.message,
      } as ChatMessage;
    });
    pages = fillPages();
    setPage(1);
  }

  $: init(chat, pageSize);
</script>

<template>
  {#if pages.length > 1}
    <div class="pager lead">
      {#if currentPage != pages.length}
        <a href={'#'} on:click|preventDefault={() => setPage(currentPage + 1)} class="knob">&#62;&#62;</a>
      {/if}
      {#each pages as p}
        <a href={'#'} on:click|preventDefault={() => setPage(p)} class:selected={p === currentPage}>{p} </a>
      {/each}
    </div>
  {/if}
  <div class="chat">
    <div />
    <div class="chat-body">
      <div class="flex-expand" />
      {#each pageMessages as m (m.id)}
        <ChatMessageComponent message={m} />
      {/each}
    </div>
    <div />
  </div>
  {#if pages.length > 1}
    <div class="pager">
      {#if currentPage != pages.length}
        <a href={'#'} on:click|preventDefault={() => setPage(currentPage + 1)} class="knob">&#62;&#62;</a>
      {/if}
      {#each pages as p}
        <a href={'#'} on:click|preventDefault={() => setPage(p)} class:selected={p === currentPage}>{p} </a>
      {/each}
    </div>
  {/if}
</template>
