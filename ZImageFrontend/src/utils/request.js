import axios from 'axios';
import { ElMessage } from 'element-plus';
import router from '@/router';
import {
  clearStoredAuth,
  getStoredAccessToken,
  getStoredRefreshToken,
  isTokenExpired,
  setStoredTokens,
} from '@/utils/jwt';
import { closeTaskSocket } from '@/utils/taskSocket';

const request = axios.create({
  baseURL: '/api',
  timeout: 120000,
  withCredentials: false,
});

let refreshPromise = null;

const extractErrorMessage = (payload) => {
  if (!payload) {
    return '';
  }

  if (typeof payload === 'string') {
    return payload;
  }

  if (typeof payload.message === 'string' && payload.message) {
    return payload.message;
  }

  if (typeof payload.error === 'string' && payload.error) {
    return payload.error;
  }

  if (payload.error && typeof payload.error === 'object') {
    if (typeof payload.error.message === 'string' && payload.error.message) {
      return payload.error.message;
    }
    if (typeof payload.error.code === 'string' && payload.error.code) {
      return payload.error.code;
    }
  }

  return '';
};

const normalizeAuthPayload = (body) => {
  const payload = body?.data || body || {};
  return {
    accessToken: payload.access_token || payload.accessToken || payload.token || '',
    refreshToken: payload.refresh_token || payload.refreshToken || '',
    expiresIn: payload.expires_in || payload.expiresIn || 0,
    user: payload.user || null,
  };
};

const isAuthTokenRequest = (config = {}) =>
  config.skipAuthRefresh || /^\/?auth\/(login|register|refresh|logout)$/.test(String(config.url || ''));

const redirectToLogin = (message = '登录已过期，请重新登录') => {
  ElMessage.error(message);
  clearStoredAuth();
  closeTaskSocket();
  router.push('/login');
};

const refreshAccessToken = async () => {
  if (refreshPromise) {
    return refreshPromise;
  }

  const refreshToken = getStoredRefreshToken();
  if (!refreshToken || isTokenExpired(refreshToken)) {
    throw new Error('登录已过期，请重新登录');
  }

  refreshPromise = axios
    .post(
      '/auth/refresh',
      { refresh_token: refreshToken },
      {
        baseURL: '/api',
        timeout: 120000,
        withCredentials: false,
      }
    )
    .then((response) => {
      const payload = normalizeAuthPayload(response.data);
      if (!payload.accessToken || !payload.refreshToken) {
        throw new Error('Refresh response missing token');
      }
      setStoredTokens(payload.accessToken, payload.refreshToken);
      if (payload.user) {
        localStorage.setItem('userInfo', JSON.stringify(payload.user));
      }
      return payload.accessToken;
    })
    .finally(() => {
      refreshPromise = null;
    });

  return refreshPromise;
};

const parseErrorPayload = async (payload) => {
  if (!payload || typeof Blob === 'undefined' || !(payload instanceof Blob)) {
    return payload;
  }

  const contentType = payload.type || '';
  if (!contentType.includes('application/json') && !contentType.startsWith('text/')) {
    return payload;
  }

  try {
    const text = await payload.text();
    return text ? JSON.parse(text) : payload;
  } catch (error) {
    return payload;
  }
};

request.interceptors.request.use(
  async (config) => {
    let token = getStoredAccessToken();
    if (token) {
      if (isTokenExpired(token) && !isAuthTokenRequest(config)) {
        try {
          token = await refreshAccessToken();
        } catch (error) {
          redirectToLogin(error.message);
          return Promise.reject(error);
        }
      }

      config.headers['Authorization'] = `Bearer ${token}`;
    }
    return config;
  },
  (error) => {
    console.error('Request error:', error);
    return Promise.reject(error);
  }
);

request.interceptors.response.use(
  (response) => {
    const body = response.data;

    // Compatible with two response shapes:
    // 1) { code, message, data }
    // 2) plain body with HTTP status code
    if (body && typeof body === 'object' && Object.prototype.hasOwnProperty.call(body, 'code')) {
      const businessCode = Number(body.code);
      if (!Number.isNaN(businessCode) && businessCode !== 200) {
        const message = body.message || body.error || 'Error occurred';
        ElMessage.error(message);
        return Promise.reject(new Error(message));
      }
      return body;
    }

    return {
      code: response.status,
      data: body,
      message: extractErrorMessage(body),
    };
  },
  async (error) => {
    console.error('Response error:', error);

    const parsedPayload = await parseErrorPayload(error.response?.data);
    if (error.response) {
      error.response.data = parsedPayload;
    }

    const message = extractErrorMessage(parsedPayload) || error.message || 'Network error';
    error.message = message;

    if (error.response?.status === 401) {
      const originalConfig = error.config || {};
      if (!originalConfig._retry && !isAuthTokenRequest(originalConfig)) {
        try {
          const token = await refreshAccessToken();
          originalConfig._retry = true;
          originalConfig.headers = originalConfig.headers || {};
          originalConfig.headers.Authorization = `Bearer ${token}`;
          return request(originalConfig);
        } catch (refreshError) {
          redirectToLogin(refreshError.message || message || '登录已过期，请重新登录');
          return Promise.reject(refreshError);
        }
      }

      redirectToLogin(message || '登录已过期，请重新登录');
      return Promise.reject(error);
    }

    if (error.response?.status === 403) {
      ElMessage.error('没有权限访问');
      return Promise.reject(error);
    }

    ElMessage.error(message);
    return Promise.reject(error);
  }
);

export default request;
