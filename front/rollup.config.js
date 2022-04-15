/* eslint-disable @typescript-eslint/explicit-module-boundary-types */
import { babel } from '@rollup/plugin-babel';
import commonjs from '@rollup/plugin-commonjs';
import html from '@rollup/plugin-html';
import resolve from '@rollup/plugin-node-resolve';
import typescript from '@rollup/plugin-typescript';
import autoprefixer from 'autoprefixer';
import cssnano from 'cssnano';
import copy from 'rollup-plugin-copy';
import css from 'rollup-plugin-css-only';
import filesize from 'rollup-plugin-filesize';
import gzip from 'rollup-plugin-gzip';
import replace from 'rollup-plugin-replace';
import svelte from 'rollup-plugin-svelte';
import { terser } from 'rollup-plugin-terser';
import autoPreprocess from 'svelte-preprocess';
import alias from '@rollup/plugin-alias';

const isWatch = process.env.ROLLUP_WATCH === 'true';
const isProduction = process.env.NODE_ENV === 'production';
const debugModules = isProduction ? [] : ['wirow:*', '-wirow:WS:DEBUG', 'mediasoup-client:*'];
const enableSentry = !!parseInt(process.env.ENABLE_SENTRY) || false;
const enableWhiteboard = !!parseInt(process.env.ENABLE_WHITEBOARD) || !isProduction;
const enableRecording = !!parseInt(process.env.ENABLE_RECORDING) || !isProduction;
const limitRegisteredUsers = parseInt(process.env.LIMIT_REGISTERED_USERS) || null;

let dist = process.env.OUTPUT_DIR || './dist';

const minChromeVersion = '74';
const minFirefoxVersion = '70';
const minSafariVersion = '14';

// Chrome engine version (seems to be the same as chrome major version)
// https://en.wikipedia.org/wiki/Google_Chrome_version_history
const minBlinkVersion = minChromeVersion;
// Gecko version numbering is the same as the Firefox build version number (from wikipedia)
const minGeckoVersion = minFirefoxVersion;
// Safari engine version, seems to be the same as safari version
const minWebKitVersion = minSafariVersion;

const babelOptions = {
  babelHelpers: 'bundled',
  extensions: ['.js', '.ts'],
  presets: [
    [
      '@babel/preset-env',
      {
        targets: {
          chrome: minChromeVersion,
          firefox: minFirefoxVersion,
          safari: minSafariVersion,
        },
      },
    ],
  ],
};

const replaceConfigOptions = {
  include: 'src/config.ts',
  ENVIRONMENT: isProduction ? 'production' : 'development',
  APP_PORT: process.env.APP_PORT || '0',
  APP_HOST: process.env.APP_HOST || '',
  BUILD_TIME: process.env.BUILD_TIME || '',
  DEBUG_MODULES: debugModules.join(','),
  MIN_CHROME_VERSION: minChromeVersion,
  MIN_FIREFOX_VERSION: minFirefoxVersion,
  MIN_SAFARI_VERSION: minSafariVersion,
  MIN_BLINK_VERSION: minBlinkVersion,
  MIN_GECKO_VERSION: minGeckoVersion,
  MIN_WEBKIT_VERSION: minWebKitVersion,
  ENABLE_WHITEBOARD: enableWhiteboard && 1 || 0,
  ENABLE_RECORDING: enableRecording && 1 || 0,
  LIMIT_REGISTERED_USERS: limitRegisteredUsers || 0,
};

const aliasSentryOptions = {
  entries: enableSentry ? [{ find: './sentry', replacement: './sentry.internal' }] : [],
};

function onwarn(warning, warn) {
  if (warning.code === 'CIRCULAR_DEPENDENCY' && ['semver'].some((d) => warning.importer.includes(d))) {
    return;
  } else if (warning.code === 'THIS_IS_UNDEFINED' && warning.loc.file.indexOf('/detect-browser') !== -1) {
    return;
  }
  warn(warning);
}

async function configAdmin() {
  const plugins = [
    alias(aliasSentryOptions),
    svelte({
      compilerOptions: {
        dev: !isProduction,
      },
      preprocess: autoPreprocess({
        scss: {
          outputStyle: 'compressed',
          sourceMap: !isProduction,
        },
        typescript: {
          tsconfigFile: true,
        },
        postcss: {
          plugins: [
            autoprefixer(),
            ...(isProduction
              ? [
                  cssnano({
                    preset: 'default',
                  }),
                ]
              : []),
          ],
        },
      }),
      onwarn: (warning, handler) => {
        if (warning.code === 'unused-export-let') {
          return;
        }
        handler(warning);
      },
    }),

    css({ output: 'admin.css' }),
    replace(replaceConfigOptions),
    resolve({
      browser: true,
      dedupe: ['svelte'],
      extensions: ['.js', '.json'],
      preferBuiltins: false,
    }),
    typescript({ sourceMap: !isProduction }),
    commonjs(),
    babel(babelOptions),
  ];
  if (isProduction) {
    plugins.push(
      terser({
        output: {
          comments: false,
        },
      })
    );
    plugins.push(gzip());
  }
  return {
    input: 'src/admin/index.ts',
    output: {
      sourcemap: !isProduction,
      file: `${dist}/admin.js`,
      format: 'iife',
      name: 'bundle',
      plugins: [],
    },
    watch: {
      exclude: 'node_modules/**',
      include: ['src/**', 'css/**'],
      clearScreen: false,
      chokidar: {
        usePolling: true,
      },
    },
    plugins,
    onwarn,
  };
}

async function configPublic() {
  const plugins = [
    alias(aliasSentryOptions),
    svelte({
      compilerOptions: {
        dev: !isProduction,
      },
      emitCss: false,
      preprocess: autoPreprocess({
        scss: {
          outputStyle: 'compressed',
          sourceMap: !isProduction,
        },
        typescript: {
          tsconfigFile: true,
        },
        postcss: {
          plugins: [
            autoprefixer(),
            ...(isProduction
              ? [
                  cssnano({
                    preset: 'default',
                  }),
                ]
              : []),
          ],
        },
      }),
      onwarn: (warning, handler) => {
        if (warning.code === 'unused-export-let') {
          return;
        }
        handler(warning);
      },
    }),
    resolve({
      browser: true,
      dedupe: ['svelte'],
      extensions: ['.js', '.json'],
      preferBuiltins: false,
    }),
    typescript({ sourceMap: !isProduction }),
    commonjs(),
    babel(babelOptions),
  ];

  if (isProduction) {
    plugins.push(
      terser({
        output: {
          comments: false,
        },
      })
    );
    plugins.push(gzip());
  }

  return {
    input: 'src/public/index.ts',
    output: {
      sourcemap: !isProduction,
      file: `${dist}/public.js`,
      format: 'iife',
      name: 'bundle',
      plugins: [],
    },
    watch: {
      exclude: 'node_modules/**',
      include: ['src/**', 'css/**'],
      clearScreen: false,
      chokidar: {
        usePolling: true,
      },
    },
    plugins,
    onwarn,
  };
}

async function configMain() {
  // define all our plugins
  const plugins = [
    alias(aliasSentryOptions),
    svelte({
      compilerOptions: {
        dev: !isProduction,
      },
      preprocess: autoPreprocess({
        scss: {
          outputStyle: 'compressed',
          sourceMap: !isProduction,
        },
        typescript: {
          tsconfigFile: true,
        },
        postcss: {
          plugins: [
            autoprefixer(),
            ...(isProduction
              ? [
                  cssnano({
                    preset: 'default',
                  }),
                ]
              : []),
          ],
        },
      }),
      onwarn: (warning, handler) => {
        if (warning.code === 'unused-export-let') {
          return;
        }
        handler(warning);
      },
    }),

    css({ output: 'bundle.css' }),
    replace(replaceConfigOptions),
    resolve({
      browser: true,
      dedupe: ['svelte'],
      extensions: ['.js', '.json'],
      preferBuiltins: false,
    }),
    typescript({ sourceMap: !isProduction }),
    commonjs(),
    babel(babelOptions),
    // injects your bundles into index page
    html({
      title: 'Wirow',
      fileName: 'index.html',
      template: htmlTemplate,
    }),
    copy({
      targets: [
        { src: ['images/**/*.{png,jpg}'], dest: `${dist}/images` },
        { src: ['fonts/**/*.{woff2,}'], dest: `${dist}/fonts` },
        { src: ['css/**/*.css'], dest: dist },
      ],
      flatten: false,
      copyOnce: isWatch,
    }),
  ];

  if (isProduction) {
    plugins.push(
      filesize({
        showBrotliSize: false,
        showMinifiedSize: false,
        showGzippedSize: true,
        showBeforeSizes: true,
      })
    );

    plugins.push(
      terser({
        output: {
          comments: false,
        },
      })
    );

    plugins.push(
      gzip({
        additionalFiles: [`${dist}/bundle.css`],
        additionalFilesDelay: 500,
      })
    );
  }

  return {
    input: 'src/index.ts',
    output: {
      sourcemap: !isProduction,
      file: `${dist}/bundle.js`,
      name: 'bundle',
      format: 'iife',
      plugins: [],
    },
    watch: {
      exclude: 'node_modules/**',
      include: ['src/**', 'css/**'],
      clearScreen: false,
      chokidar: {
        usePolling: true,
      },
      //chokidar: false,
    },
    plugins,
    onwarn,
  };
}

async function htmlTemplate({ attributes, files, meta, publicPath, title }) {
  const makeHtmlAttributes = (attributes) => {
    if (!attributes) {
      return '';
    }
    const keys = Object.keys(attributes);
    // eslint-disable-next-line no-param-reassign
    return keys.reduce((result, key) => (result += ` ${key}="${attributes[key]}"`), '');
  };
  const scripts = (files.js || [])
    .map(({ fileName }) => {
      const attrs = makeHtmlAttributes(attributes.script);
      return `<script src="${publicPath}${fileName}"${attrs}></script>`;
    })
    .join('\n');
  const metas = meta
    .map((input) => {
      const attrs = makeHtmlAttributes(input);
      return `<meta${attrs}>`;
    })
    .join('\n');
  return `<!doctype html>
<html${makeHtmlAttributes(attributes.html)}>
  <head>
    <title>${title}</title>
    <meta name='viewport' content='width=device-width,initial-scale=1'>
    ${metas}
    <link href='fonts.css' rel='stylesheet' type='text/css'>
    <link href='bundle.css' rel='stylesheet' type='text/css'>
  </head>
  <body>
    ${scripts}
  </body>
</html>`;
}

export default (args) => {
  const configs = [];
  const hasConfigs = args.configMain || args.configPublic || args.configAdmin;
  if (!hasConfigs || args.configMain) {
    configs.push(configMain());
  }
  if (!hasConfigs || args.configPublic) {
    if (hasConfigs && !args.configMain) {
      args.configMain = true;
      configs.push(configMain()); // We depend on main config
    }
    configs.push(configPublic());
  }
  if (!hasConfigs || args.configAdmin) {
    configs.push(configAdmin());
  }
  console.log(`
    --configMain ${hasConfigs && args.configMain === undefined ? '\tno' : '\tyes'}
    --configPublic ${hasConfigs && args.configPublic === undefined ? '\tno' : '\tyes'}
    --configAdmin ${hasConfigs && args.configAdmin === undefined ? '\tno' : '\tyes'}
  `);
  return Promise.all(configs);
};
