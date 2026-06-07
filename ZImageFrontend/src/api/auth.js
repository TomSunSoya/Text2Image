import request from '@/utils/request';

export const authApi = {
  // 用户注册
  register(data) {
    return request({
      url: '/auth/register',
      method: 'post',
      data,
    });
  },

  // 用户登录
  login(data) {
    return request({
      url: '/auth/login',
      method: 'post',
      data,
    });
  },

  refresh(refreshToken) {
    return request({
      url: '/auth/refresh',
      method: 'post',
      data: {
        refresh_token: refreshToken,
      },
      skipAuthRefresh: true,
    });
  },

  logout(refreshToken) {
    return request({
      url: '/auth/logout',
      method: 'post',
      data: {
        refresh_token: refreshToken,
      },
      skipAuthRefresh: true,
    });
  },

  getProfile() {
    return request({
      url: '/auth/me',
      method: 'get',
    });
  },

  changePassword(data) {
    return request({
      url: '/auth/password',
      method: 'put',
      data,
    });
  },
};
