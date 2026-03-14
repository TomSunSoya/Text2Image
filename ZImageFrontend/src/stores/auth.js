import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { authApi } from '@/api/auth'
import router from '@/router'
import { ElMessage } from 'element-plus'

function normalizeUserInfo(rawUser, fallbackUsername = '') {
  const user = rawUser || {}
  const username = user.username || user.name || fallbackUsername || ''
  return {
    userId: user.userId ?? user.id ?? 0,
    username,
    nickname: user.nickname || username,
    email: user.email || ''
  }
}

export const useAuthStore = defineStore('auth', () => {
  const token = ref(localStorage.getItem('token') || '')
  const userInfo = ref(JSON.parse(localStorage.getItem('userInfo') || 'null'))
  const isLoggingIn = ref(false)

  const isAuthenticated = computed(() => !!token.value)
  const username = computed(() => userInfo.value?.username || '')
  const nickname = computed(() => userInfo.value?.nickname || username.value)

  async function login(credentials) {
    isLoggingIn.value = true
    try {
      const response = await authApi.login(credentials)
      const payload = response.data || {}

      const loginToken = payload.token || payload.accessToken || ''
      if (!loginToken) {
        throw new Error('Login response missing token')
      }

      const rawUser = payload.user || payload
      const normalizedUser = normalizeUserInfo(rawUser, credentials?.username || '')

      token.value = loginToken
      userInfo.value = normalizedUser

      localStorage.setItem('token', loginToken)
      localStorage.setItem('userInfo', JSON.stringify(normalizedUser))

      ElMessage.success('登录成功')
      router.push('/')
      return payload
    } catch (error) {
      ElMessage.error('登录失败: ' + (error.response?.data?.message || error.message))
      throw error
    } finally {
      isLoggingIn.value = false
    }
  }

  async function register(userData) {
    try {
      const response = await authApi.register(userData)
      ElMessage.success('注册成功，请登录')
      router.push('/login')
      return response.data
    } catch (error) {
      ElMessage.error('注册失败: ' + (error.response?.data?.message || error.message))
      throw error
    }
  }

  function logout() {
    token.value = ''
    userInfo.value = null
    localStorage.removeItem('token')
    localStorage.removeItem('userInfo')
    ElMessage.success('已退出登录')
    router.push('/login')
  }

  function checkAuth() {
    const savedToken = localStorage.getItem('token')
    const savedUserInfo = localStorage.getItem('userInfo')

    if (!savedToken || !savedUserInfo) {
      return false
    }

    try {
      const parsed = JSON.parse(savedUserInfo)
      token.value = savedToken
      userInfo.value = normalizeUserInfo(parsed)
      return true
    } catch {
      token.value = ''
      userInfo.value = null
      localStorage.removeItem('token')
      localStorage.removeItem('userInfo')
      return false
    }
  }

  return {
    token,
    userInfo,
    isLoggingIn,
    isAuthenticated,
    username,
    nickname,
    login,
    register,
    logout,
    checkAuth
  }
})
