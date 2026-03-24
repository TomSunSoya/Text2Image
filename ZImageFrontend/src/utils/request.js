import axios from 'axios';
import { ElMessage } from 'element-plus';
import router from '@/router';
import { clearStoredAuth, isTokenExpired } from '@/utils/jwt';
import { closeTaskSocket } from '@/utils/taskSocket';

const request = axios.create({
  baseURL: '/api',
  timeout: 120000,
  withCredentials: false,
});

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
  (config) => {
    const token = localStorage.getItem('token');
    if (token) {
      if (isTokenExpired(token)) {
        clearStoredAuth();
        closeTaskSocket();
        ElMessage.error('登录已过期，请重新登录');
        router.push('/login');
        return Promise.reject(new Error('登录已过期，请重新登录'));
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
      ElMessage.error(message || '登录已过期，请重新登录');
      clearStoredAuth();
      closeTaskSocket();
      router.push('/login');
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
