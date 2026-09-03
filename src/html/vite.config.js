import { defineConfig, loadEnv } from 'vite'
import path from 'path'
import { minifyTemplateLiterals } from 'rollup-plugin-minify-template-literals';
import { babelDecoratorsPlugin } from './build-plugins/babel-decorators-plugin.js'
import { cssTreeShakePlugin } from './build-plugins/css-tree-shake-plugin.js'
import { htmlFeatureBlocksPlugin } from './build-plugins/feature-blocks-plugin.js'
import { inlineStaticHtmlAssetsPlugin } from './build-plugins/inline-static-html-assets-plugin.js'
import { viteEsp32HeaderPlugin } from './build-plugins/esp32-header-plugin.js'
import { hamlLitPlugin } from './build-plugins/haml-lit-plugin.js'
import { coffeePlugin } from './build-plugins/coffee-plugin.js'

// Simple dev mock server plugin
import { devMockPlugin } from './dev-plugins/dev-mock-plugin.js'

// Proxy plugin for devlopment against real hardware
import { devProxyPlugin } from './dev-plugins/dev-proxy-plugin.js'

// Export standard Vite config with the plugin enabled for builds
export default defineConfig(({ command, mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  return {
    plugins: [
      coffeePlugin(),
      hamlLitPlugin(),
      htmlFeatureBlocksPlugin(env),
      minifyTemplateLiterals({
        include: ['src/**/*.js'],
        exclude: ['node_modules/**'],
        failOnError: true,
        options: {
          minifyOptions: {
            collapseWhitespace: true,
            removeComments: true,
            removeAttributeQuotes: true,
            collapseBooleanAttributes: true,
            removeRedundantAttributes: true,
            useShortDoctype: true,
            caseSensitive: true,
            minifyCSS: true
          }
        }
      }),
      cssTreeShakePlugin(env),
      inlineStaticHtmlAssetsPlugin(),
      viteEsp32HeaderPlugin({ headerOut: env.ELRS_WEB_HEADER_OUT }),
      babelDecoratorsPlugin(),
      ...(command === 'serve'
        ? [
            env.VITE_ELRS_PROXY_TARGET
              ? devProxyPlugin({ target: env.VITE_ELRS_PROXY_TARGET })
              : devMockPlugin()
          ]
        : []),
    ],
    optimizeDeps: {
      rolldownOptions: {
        plugins: [htmlFeatureBlocksPlugin(env)],
      },
    },
    resolve: {
      extensions: ['.coffee', '.js', '.ts', '.jsx', '.tsx', '.json', '.css', '.lithaml'],
    },
    esbuild: {
      legalComments: 'none'
    },
    build: {
      rolldownOptions: {
        resolve: {
          extensions: ['.coffee', '.js', '.ts', '.jsx', '.tsx', '.json', '.css', '.lithaml'],
        },
        input: {
          app: path.resolve(__dirname, 'index.html'),
        },
        output: {
          entryFileNames: '[name]-[hash].js',
          chunkFileNames: '[name]-[hash].js',
          manualChunks(id) {
            // ESP8285 has too little network heap to reliably serve multiple
            // large module streams during the first uncached page load.
            // PteronautOS uses static imports, so keep it in one JS asset.
            if (env.VITE_FEATURE_PTERONAUTOS === 'true') return undefined
            const p = id.split('\\').join('/')
            if (
              (p.includes('/src/utils/') && !p.endsWith('/hardware-schema.js')) ||
              p.endsWith('/src/components/filedrag.js')
            ) {
              return 'utils'
            }
            return undefined
          },
        },
      },
      cssCodeSplit: true,
      sourcemap: false,
    }
  }
})
