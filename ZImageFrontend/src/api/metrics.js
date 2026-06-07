import request from '@/utils/request';

export const metricsApi = {
  getCacheMetrics() {
    return request({
      url: '/metrics/cache',
      method: 'get',
    });
  },
};
