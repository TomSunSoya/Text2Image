import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import { authApi } from '@/api/auth';
import router from '@/router';
import { ElMessage } from 'element-plus';
import {
  clearStoredAuth,
  getStoredAccessToken,
  getStoredRefreshToken,
  isTokenExpired,
  setStoredAuth,
} from '@/utils/jwt';
import { closeTaskSocket } from '@/utils/taskSocket';

function normalizeUserInfo(rawUser, fallbackUsername = '') {
  const user = rawUser || {};
  const username = user.username || user.name || fallbackUsername || '';
  return {
    userId: user.userId ?? user.id ?? 0,
    username,
    nickname: user.nickname || username,
    email: user.email || '',
    role: user.role === 'admin' ? 'admin' : 'user',
  };
}

export const useAuthStore = defineStore('auth', () => {
  const token = ref(getStoredAccessToken());
  const refreshToken = ref(getStoredRefreshToken());
  const userInfo = ref(JSON.parse(localStorage.getItem('userInfo') || 'null'));
  const isLoggingIn = ref(false);

  const isAuthenticated = computed(
    () =>
      (!!token.value && !isTokenExpired(token.value)) ||
      (!!refreshToken.value && !isTokenExpired(refreshToken.value))
  );
  const username = computed(() => userInfo.value?.username || '');
  const nickname = computed(() => userInfo.value?.nickname || username.value);
  const isAdmin = computed(() => userInfo.value?.role === 'admin');

  function resetAuthState() {
    token.value = '';
    refreshToken.value = '';
    userInfo.value = null;
    clearStoredAuth();
    closeTaskSocket();
  }

  async function login(credentials) {
    isLoggingIn.value = true;
    try {
      const response = await authApi.login(credentials);
      const payload = response.data || {};

      const loginToken = payload.access_token || payload.accessToken || payload.token || '';
      const loginRefreshToken = payload.refresh_token || payload.refreshToken || '';
      if (!loginToken) {
        throw new Error('Login response missing token');
      }
      if (!loginRefreshToken) {
        throw new Error('Login response missing refresh token');
      }

      const rawUser = payload.user || payload;
      const normalizedUser = normalizeUserInfo(rawUser, credentials?.username || '');

      token.value = loginToken;
      refreshToken.value = loginRefreshToken;
      userInfo.value = normalizedUser;

      setStoredAuth(loginToken, loginRefreshToken, normalizedUser);

      ElMessage.success('登录成功');
      router.push('/');
      return payload;
    } catch (error) {
      ElMessage.error('登录失败: ' + (error.response?.data?.message || error.message));
      throw error;
    } finally {
      isLoggingIn.value = false;
    }
  }

  async function register(userData) {
    try {
      const response = await authApi.register(userData);
      ElMessage.success('注册成功，请登录');
      router.push('/login');
      return response.data;
    } catch (error) {
      ElMessage.error('注册失败: ' + (error.response?.data?.message || error.message));
      throw error;
    }
  }

  async function logout(revokeRefreshToken = true) {
    const tokenToRevoke = refreshToken.value || getStoredRefreshToken();
    try {
      if (revokeRefreshToken && tokenToRevoke) {
        await authApi.logout(tokenToRevoke);
      }
    } catch (error) {
      console.warn('Logout token revocation failed:', error);
    } finally {
      resetAuthState();
      ElMessage.success('已退出登录');
      router.push('/login');
    }
  }

  function checkAuth() {
    const savedToken = getStoredAccessToken();
    const savedRefreshToken = getStoredRefreshToken();
    const savedUserInfo = localStorage.getItem('userInfo');

    if ((!savedToken && !savedRefreshToken) || !savedUserInfo) {
      resetAuthState();
      return false;
    }

    if ((!savedToken || isTokenExpired(savedToken)) && (!savedRefreshToken || isTokenExpired(savedRefreshToken))) {
      resetAuthState();
      return false;
    }

    try {
      const parsed = JSON.parse(savedUserInfo);
      token.value = savedToken;
      refreshToken.value = savedRefreshToken;
      userInfo.value = normalizeUserInfo(parsed);
      return true;
    } catch {
      resetAuthState();
      return false;
    }
  }

  return {
    token,
    refreshToken,
    userInfo,
    isLoggingIn,
    isAuthenticated,
    isAdmin,
    username,
    nickname,
    login,
    register,
    logout,
    checkAuth,
  };
});
