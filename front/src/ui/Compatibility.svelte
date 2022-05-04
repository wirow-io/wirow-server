<script lang="ts" context="module">
  import Bowser from 'bowser';
  import * as semver from 'semver';
  import { _ } from 'svelte-intl';
  import { Config } from '../config';
  import Box from '../kit/Box.svelte';

  let status: boolean | undefined = undefined;
  let fail: Bowser.Parser.ParsedResult | false = false;

  const ffVersion = Config.minFirefoxVersion;
  const chromeVersion = Config.minChromeVersion;
  const safariVerion = Config.minSafariVersion;
  const webKitVersion = Config.minWebKitVersion;
  const geckoVersion = Config.minGeckoVersion;

  const namecmp = (a: string | undefined, b: string) => a && a.toLowerCase() === b.toLowerCase();
  const vercmp = (ver: string | undefined, check: string) => ver && semver.satisfies(ver, check);

  export function compatibilityMatcher(): boolean {
    if (status !== undefined) {
      return status;
    }

    // https://github.com/lancedikson/bowser/blob/f09411489ced05811c91cc6670a8e4ca9cbe39a7/src/constants.js
    const OS_MAP = Bowser.OS_MAP;
    const ENGINE_MAP = Bowser.ENGINE_MAP;

    const result = Bowser.getParser(window.navigator.userAgent).parse().getResult();
    const { os, engine, browser } = result;

    // Bowser can sometimes give version like "14.0" but semver needs "14.0.0"
    engine.version = semver.valid(semver.coerce(browser.version)) || undefined;
    browser.version = semver.valid(semver.coerce(browser.version)) || undefined;

    // TODO: check os versions
    status = !(
      namecmp(os.name, OS_MAP.iOS) ||
      namecmp(os.name, OS_MAP.Android) ||
      namecmp(os.name, OS_MAP.ChromeOS) ||
      namecmp(engine.name, ENGINE_MAP.Blink) ||
      (namecmp(engine.name, ENGINE_MAP.WebKit) && vercmp(browser.version, `>=${webKitVersion}`)) ||
      (namecmp(engine.name, ENGINE_MAP.Gecko) && vercmp(browser.version, `>=${geckoVersion}`))
    );

    fail = status && result;
    if (fail) {
      console.warn('Browser: ', result);
    }
    return status;
  }
</script>

<script lang="ts">
  function capitalize(val: string | undefined) {
    if (val == null || val.length === 0) {
      return '';
    }
    return `${val.charAt(0).toUpperCase()}${val.substring(1)}`;
  }
</script>

<template>
  <div class="compatibility">
    <div class="holder">
      <Box componentClass="secondary">
        <h2>{$_('Compatibility.caption')}</h2>
        {#if fail}
          <p>
            <strong>
              {capitalize(fail.browser.name || '')}
              v{fail.browser.version || ''}
              {fail.os.name || ''}
              {fail.os.versionName || ''}
              {fail.os.version || ''}
            </strong>
          </p>
        {/if}
        <p>
          {@html $_('Compatibility.supported_caption')}
        </p>
        <ul>
          <li>Mozilla Firefox {ffVersion}+</li>
          <li>Google Chrome {chromeVersion}+</li>
          <li>Apple Safari {safariVerion}+</li>
        </ul>
      </Box>
    </div>
  </div>
</template>
