<!-- svelte-ignore unused-export-let -->
<script lang="ts" context="module">
  import { writable } from 'svelte/store';
  import { sendNotification, recommendedTimeout } from '../../notify';
  import { t } from '../../translate';
  import { getUserSnapshot } from '../../user';

  let uploadsCount = writable(0);
  let uploadsTotal = writable(0);
  let uploadsLoaded = writable(0);

  // If you want to make a module from this function create necessary TS types for request, responce and etc.
  function axios({ method, url, data, onUploadProgress }): Promise<any> {
    return new Promise((resolve, reject) => {
      let xhr = new XMLHttpRequest();
      xhr.onload = () => {
        let headers = {},
          xhrheaders = xhr.getAllResponseHeaders();
        if (xhrheaders) {
          for (let i of xhrheaders.split('\r\n')) {
            let [header, value] = i.split(': ', 2);
            headers[header] = value;
          }
        }

        resolve({
          data: xhr.response,
          status: xhr.status,
          statusText: xhr.statusText,
          headers,
        });
      };
      xhr.onerror = reject;
      xhr.upload.onprogress = onUploadProgress;
      xhr.open(method, url);
      xhr.send(data);
    });
  }

  function sendFile(file: File, dispatch: Function | undefined = undefined): Promise<any> | void {
    const name = file.name;
    const total = file.size;
    let loaded = 0;

    const max_size = getUserSnapshot()?.system.max_upload_size;

    if (max_size === undefined) {
      return sendNotification({
        text: t('ChatInput.file_upload_error', { name }),
        style: 'error',
        closeable: true,
      });
    }

    if (file.size > max_size) {
      return sendNotification({
        text: t('ChatInput.file_size_limit', { name, limit: Math.floor(max_size / 1024 / 1024) }),
        style: 'warning',
        closeable: true,
      });
    }

    uploadsCount.update((val) => val + 1);
    uploadsTotal.update((val) => val + total);

    let form = new FormData();
    // Encode name for now and decode on server
    form.append('file', file, encodeURIComponent(name));

    return axios({
      method: 'POST',
      url: '/files/upload',
      data: form,
      onUploadProgress: (p: ProgressEvent) => {
        uploadsLoaded.update((val) => val + p.loaded - loaded);
        loaded = p.loaded;
      },
    })
      .then((response) => {
        if (response.status != 200) {
          return Promise.reject({ status: response.status, text: response.statusText });
        }
        return response;
      })
      .then((response) => response.data)
      .then((link) => new URL(link, document.URL).toString())
      .then((link) => {
        if (!dispatch) {
          return;
        }

        // Escape attribute quotes and text
        const _link = link.replace('"', '&quot;');
        const _name = name.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;').replace('>', '&gt;');
        const _a = `<a href="${_link}" target="_blank">${_name}</a>`;
        const _dl = `<a href="${_link}" download>${_name}</a>`;
        if (file.type.startsWith('image/')) {
          dispatch('send', {
            html: `<img src="${_link}" alt="${_name}"><br>${_dl}`,
          });
        } else if (file.type.startsWith('audio/')) {
          dispatch('send', {
            html: `<audio controls src="${_link}"></audio><br>${_dl}`,
          });
        } else if (file.type.startsWith('video/')) {
          dispatch('send', {
            html: `<video controls src="${_link}"></video><br>${_dl}`,
          });
        } else if (file.type === 'application/pdf') {
          dispatch('send', {
            html: _a,
          });
        } else {
          dispatch('send', {
            html: _dl,
          });
        }
      })
      .then((_) =>
        sendNotification({
          text: t('ChatInput.file_upload_success', { name }),
          closeable: true,
          timeout: recommendedTimeout,
        })
      )
      .catch((reason) => {
        if (reason?.status == 403) {
          sendNotification({
            text: t('ChatInput.file_upload_forbidden'),
            style: 'warning',
            closeable: true,
          });
        } else {
          const msg = t('ChatInput.file_upload_error', { name });

          console.log(msg, reason);
          sendNotification({
            text: msg,
            style: 'error',
            closeable: true,
          });
        }
      })
      .finally(() => {
        uploadsCount.update((val) => val - 1);
        uploadsTotal.update((val) => val - total);
        uploadsLoaded.update((val) => val - loaded);
      });
  }
</script>

<script lang="ts">
  import { _ } from 'svelte-intl';
  import { createEventDispatcher } from 'svelte';

  export let disabled: boolean = false;

  let fileInput: HTMLInputElement | undefined;
  let dragCounter = 0;
  let progressMsg: string;

  const dispatch = createEventDispatcher();

  async function selectedFile() {
    if (!disabled) {
      let files = Array.from(fileInput?.files || []);
      for (let i of files) {
        sendFile(i, dispatch);
      }
    }
  }

  export function peekFile() {
    if (!disabled) {
      fileInput?.click();
    }
  }

  function dragEvent(ev: DragEvent) {
    ev.preventDefault();

    if (ev.type == 'dragenter') {
      dragCounter++;
    }
    if (ev.type == 'dragleave') {
      dragCounter--;
    }
  }

  function dropEvent(ev: DragEvent) {
    ev.preventDefault();
    dragCounter = 0;

    let files = Array.from(ev.dataTransfer?.files || []);
    for (let i of files) {
      sendFile(i, dispatch);
    }
  }

  $: progressMsg = $_('ChatInput.file_upload_progress', {
    count: $uploadsCount,
    percent: Math.floor((100 * $uploadsLoaded) / $uploadsTotal),
  });
</script>

<svelte:window on:drag={dragEvent} on:dragenter={dragEvent} on:dragover={dragEvent} on:dragleave={dragEvent} />

<template>
  <div class="chat-dropzone-holder">
    {#if dragCounter > 0 && !disabled}
      <div class="chat-dropzone" on:drop={dropEvent}>{$_('ChatInput.file_dropzone')}</div>
    {/if}
    {#if $uploadsCount > 0}
      <div class="upload-progress">{progressMsg}</div>
    {/if}
    <input type="file" multiple accept="*" style="display:none" bind:this={fileInput} on:change={selectedFile} />
  </div>
</template>
