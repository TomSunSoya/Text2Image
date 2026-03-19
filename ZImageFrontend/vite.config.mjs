import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath } from 'url'
import { resolve } from 'path'

const dirname = fileURLToPath(new URL('.', import.meta.url))
const backendProxyTarget = process.env.VITE_BACKEND_PROXY_TARGET || 'http://localhost:8080'
const healthProxyTarget = process.env.VITE_HEALTH_PROXY_TARGET || backendProxyTarget

export default defineConfig({
  plugins: [vue()],
  resolve: {
    alias: {
      '@': resolve(dirname, 'src')
    }
  },
  build: {
    chunkSizeWarningLimit: 1000,
    rollupOptions: {
      output: {
        manualChunks: {
          'vue-vendor': ['vue', 'vue-router', 'pinia'],
          'element-plus': ['element-plus']
        }
      }
    }
  },
  server: {
    port: 3000,
    proxy: {
      '/api': {
        target: backendProxyTarget,
        changeOrigin: true,
        ws: true
      },
      '/health': {
        target: healthProxyTarget,
        changeOrigin: true
      }
    }
  }
})
