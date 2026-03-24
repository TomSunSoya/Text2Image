import request from '@/utils/request';

export const imageApi = {
  // Generate image (async queue mode)
  generateImage(data) {
    return request({
      url: '/images',
      method: 'post',
      data,
    });
  },

  // Poll task status by image ID
  getImageStatus(id) {
    return request({
      url: `/images/${id}/status`,
      method: 'get',
    });
  },

  // Get image by ID
  getImageById(id) {
    return request({
      url: `/images/${id}`,
      method: 'get',
    });
  },

  // Get protected image binary by ID
  getImageBinary(id) {
    return request({
      url: `/images/${id}/binary`,
      method: 'get',
      responseType: 'blob',
    });
  },

  // Get current user's image list
  getMyImages(params) {
    return request({
      url: '/images/my-list',
      method: 'get',
      params,
    });
  },

  // Get images by status
  getImagesByStatus(status, params) {
    return request({
      url: `/images/my-list/status/${status}`,
      method: 'get',
      params,
    });
  },

  // Delete image
  deleteImage(id) {
    return request({
      url: `/images/${id}`,
      method: 'delete',
    });
  },

  // Cancel queued/running task
  cancelImage(id) {
    return request({
      url: `/images/${id}/cancel`,
      method: 'post',
    });
  },

  // Retry failed/cancelled/timeout task
  retryImage(id) {
    return request({
      url: `/images/${id}/retry`,
      method: 'post',
    });
  },

  // Check health
  checkHealth() {
    return request({
      url: '/images/health',
      method: 'get',
    });
  },
};
