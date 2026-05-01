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
      environment: 'node',
      include: ['src/**/*.test.ts'],
    },
  };
});
