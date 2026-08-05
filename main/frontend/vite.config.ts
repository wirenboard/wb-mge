/// <reference types="vitest" />
import vue from '@vitejs/plugin-vue';
import VueI18nPlugin from '@intlify/unplugin-vue-i18n/vite';
import { defineConfig, loadEnv } from 'vite';
import svgLoader from 'vite-svg-loader';

// https://vitejs.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');

  return {
    base: './',
    plugins: [vue(), svgLoader(), VueI18nPlugin({ strictMessage: false })],
    build: {
      assetsDir: '',
      rollupOptions: {
        output: {
          entryFileNames: `[name].js`,
          chunkFileNames: `[name].js`,
          assetFileNames: `[name].[ext]`
        }
      }
    },
    server: {
      cors: false,
      proxy: {
        '/api': {
          target: env?.PROXY_TARGET || 'http://localhost:60081',
          changeOrigin: true,
          rewrite: (path) => path.replace(/^\/api/, ''),
        },
        '/sniffer': {
          target: env?.PROXY_TARGET || 'http://localhost:60081',
          changeOrigin: true,
          ws: true,
        },
      }
    },
    resolve: {
      alias: {
        '@': '/src',
      },
    },
    test: {
      projects: [
        {
          // Unit tests: pure utility functions, no DOM required.
          // Exclude *.integration.test.ts — those end in .test.ts too.
          plugins: [vue(), svgLoader(), VueI18nPlugin({})],
          resolve: { alias: { '@': '/src' } },
          test: {
            name: 'unit',
            environment: 'node',
            include: ['src/**/*.test.ts'],
            exclude: ['src/**/*.integration.test.ts'],
          },
        },
        {
          // Integration tests: Vue component mounting with happy-dom.
          plugins: [vue(), svgLoader(), VueI18nPlugin({})],
          resolve: { alias: { '@': '/src' } },
          test: {
            name: 'integration',
            environment: 'happy-dom',
            include: ['src/**/*.integration.test.ts'],
            testTimeout: 15000, // integration tests with mount() need >5s under load
          },
        },
      ],
    },
  };
});
