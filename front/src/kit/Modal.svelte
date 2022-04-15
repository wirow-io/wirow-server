<script context="module" lang="ts">
  /// Modal box area.
  ///
  /// Props:
  ///   - overlayOpacity Opacity of modal overlay layer. Default: 0.5
  ///
  /// Slots:
  ///   - default Modal content markup

  import { onMount } from 'svelte';
  import { writable } from 'svelte/store';
  import { scale } from 'svelte/transition';
  import type { NU, SEvent } from '../interfaces';

  export interface ModalOptions {
    /// Close modal if overlay clicked
    closeOnOutsideClick?: boolean;
    closeOnEscape?: boolean;
    disposedFn?: DisposedModalFn;
    title?: string;
  }

  export type CloseModalFn = () => void;
  export type DisposedModalFn = () => void;

  interface ModalSpec {
    component: any;
    opts: ModalOptions;
    props: any;
    closeFn: CloseModalFn;
    opacity?: number;
    url: string;
  }

  let stack = new Array<ModalSpec>();
  const stackStore = writable<typeof stack>(stack);

  function disposeModal(spec: ModalSpec | undefined) {
    if (spec && typeof spec.opts.disposedFn === 'function') {
      spec.opts.disposedFn();
    }
  }

  export function focusModal() {
    const el = document.getElementById('modal-area');
    if (el) {
      const child = el.firstElementChild as any;
      if (child && typeof child.focus === 'function') {
        child.focus({
          preventScroll: true,
        });
      } else {
        el.focus({
          preventScroll: true,
        });
      }
    }
  }

  export function hasModals() {
    return stack.length > 0;
  }

  export function closeModal(popState: boolean = true) {
    if (stack.length == 0) {
      return;
    }
    if (popState === true) {
      window.history.back();
      return;
    }
    disposeModal(stack.pop());
    stackStore.set(stack);
  }

  export function openReplace(
    component: any,
    opts: ModalOptions = {},
    props: Record<string, any> | NU = null
  ): Function {
    while (stack.length) {
      disposeModal(stack.pop());
    }
    return openModal(component, opts, props);
  }

  export const recommendedModalOptions = Object.freeze({ closeOnOutsideClick: true, closeOnEscape: true });

  export function openModal(
    component: any,
    opts: ModalOptions = {},
    props: Record<string, any> | ((closeFn: CloseModalFn) => Record<string, any>) | NU = null
  ): Function {
    const url = window.location.href;
    const closeFn = () => closeModal();
    stack.push({
      component,
      opts,
      props: typeof props === 'function' ? props(closeFn) : props,
      closeFn,
      url,
    });
    window.history.pushState(null, opts.title || '', url);
    window.addEventListener(
      'popstate',
      () => {
        closeModal(false);
      },
      {
        once: true,
      }
    );

    stackStore.set(stack);
    return closeFn;
  }

  export function openOverlayBlock(opacity?: number) {
    const url = window.location.href;
    const closeFn = () => closeModal();
    stack.push({
      component: null,
      opts: {
        closeOnEscape: false,
      },
      opacity: opacity != null ? opacity : 0.5,
      props: {},
      closeFn,
      url,
    });
    window.history.pushState(null, '', url);
    window.addEventListener(
      'popstate',
      () => {
        closeModal(false);
      },
      {
        once: true,
      }
    );
    stackStore.set(stack);
    return closeModal;
  }

  export function updateCurrentModalOpts(opts: ModalOptions) {
    const m = stack[stack.length - 1];
    if (m) {
      m.opts = { ...m.opts, ...opts };
      stackStore.set(stack);
    }
  }
</script>

<script lang="ts">
  export let overlayOpacity = 0.7;

  let instanceRef: HTMLElement | undefined = undefined;

  let currentModal: any = null;
  $: currentModal = $stackStore[$stackStore.length - 1];
  $: {
    if ($stackStore.length == 1) {
      document.addEventListener('keydown', onKey);
    } else if ($stackStore.length == 0) {
      document.removeEventListener('keydown', onKey);
    }
  }

  function onKey(evt: KeyboardEvent) {
    if (evt.key === 'Escape' && currentModal?.opts?.closeOnEscape === true) {
      closeModal();
    }
  }

  onMount(() => {
    const child = instanceRef!;
    const body = document.body;
    body.appendChild(child);
    return () => {
      document.removeEventListener('keyup', onKey);
      body.removeChild(child);
    };
  });

  function onClickOverlay() {
    const top = stack[stack.length - 1];
    if (!top) {
      return;
    }
    if (top.opts?.closeOnOutsideClick === true) {
      closeModal();
    }
  }

  function onClickModalArea(ev: SEvent) {
    ev.stopPropagation();
  }
</script>

<template>
  <div id="modal-holder" style="visibility:{currentModal != null ? 'visible' : 'hidden'}" bind:this={instanceRef}>
    <div
      id="modal-overlay"
      style="opacity:{currentModal && currentModal.opacity ? currentModal.opacity : overlayOpacity}"
    />
    <div id="modal-area-holder" on:click={onClickOverlay}>
      {#if currentModal && currentModal.component}
        <div id="modal-area" in:scale={{ duration: 100 }} on:click={onClickModalArea}>
          <svelte:component this={currentModal.component} {...currentModal.props} />
        </div>
      {/if}
    </div>
  </div>
</template>
