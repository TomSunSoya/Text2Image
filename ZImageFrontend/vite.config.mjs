import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath } from 'url'
import { resolve } from 'path'

const dirname = fileURLToPath(new URL('.', import.meta.url))

const withoutTrailingSlash = (value) => value.replace(/\/+$/, '')

export default defineConfig(({ mode }) => {
  const repoEnv = loadEnv(mode, resolve(dirname, '..'), '')
  const frontendEnv = loadEnv(mode, dirname, '')
  const explicitEnv = { ...process.env, ...frontendEnv }
  const backendPort = explicitEnv.BACKEND_PORT || repoEnv.BACKEND_PORT || '8080'
  const backendProxyTarget = withoutTrailingSlash(
    explicitEnv.VITE_BACKEND_PROXY_TARGET || `http://127.0.0.1:${backendPort}`
  )
  const healthProxyTarget = withoutTrailingSlash(
    explicitEnv.VITE_HEALTH_PROXY_TARGET || backendProxyTarget
  )

  return {
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
  }
})
