import request from '@/utils/request'

export const authApi = {
  // 用户注册
  register(data) {
    return request({
      url: '/auth/register',
      method: 'post',
      data
    })
  },

  // 用户登录
  login(data) {
    return request({
      url: '/auth/login',
      method: 'post',
      data
    })
  },

  // 登出（前端处理）
  logout() {
    localStorage.removeItem('token')
    localStorage.removeItem('userInfo')
  }
}
