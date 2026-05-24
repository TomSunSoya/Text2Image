<template>
  <div class="admin-page">
    <section class="admin-shell">
      <header class="admin-header">
        <div>
          <h1>管理面板</h1>
          <p>{{ authStore.username }}</p>
        </div>
        <el-button :icon="ArrowLeft" @click="router.push('/')">返回首页</el-button>
      </header>

      <CacheMetricsPanel />
    </section>
  </div>
</template>

<script setup>
import { useRouter } from 'vue-router';
import { ArrowLeft } from '@element-plus/icons-vue';
import { useAuthStore } from '@/stores/auth';
import CacheMetricsPanel from '@/components/CacheMetricsPanel.vue';

const router = useRouter();
const authStore = useAuthStore();
</script>

<style scoped>
.admin-page {
  min-height: 100vh;
  background: #f5f7fb;
  padding: 32px 20px;
}

.admin-shell {
  max-width: 1200px;
  margin: 0 auto;
}

.admin-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.admin-header h1 {
  margin: 0;
  color: #1f2937;
  font-size: 28px;
}

.admin-header p {
  margin: 6px 0 0;
  color: #6b7280;
}

@media (max-width: 768px) {
  .admin-header {
    align-items: flex-start;
    gap: 12px;
    flex-direction: column;
  }
}
</style>
