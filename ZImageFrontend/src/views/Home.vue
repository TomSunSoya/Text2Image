<template>
  <div class="home-page">
    <header class="header">
      <div class="container">
        <h1 class="logo">
          <el-icon size="32"><Picture /></el-icon>
          AI Image Generator
        </h1>

        <nav class="nav">
          <el-menu :default-active="activeTab" mode="horizontal" @select="handleTabChange">
            <el-menu-item index="generator">生成图像</el-menu-item>
            <el-menu-item index="history">我的作品</el-menu-item>
          </el-menu>

          <!-- 用户信息 -->
          <div class="user-info">
            <el-dropdown @command="handleCommand">
              <div class="user-avatar">
                <el-icon><Avatar /></el-icon>
                <span class="username">{{ authStore.nickname }}</span>
                <el-icon class="arrow"><ArrowDown /></el-icon>
              </div>
              <template #dropdown>
                <el-dropdown-menu>
                  <el-dropdown-item disabled>
                    <div class="user-detail">
                      <div class="user-name">{{ authStore.username }}</div>
                      <div class="user-email">{{ authStore.userInfo?.email || '' }}</div>
                    </div>
                  </el-dropdown-item>
                  <el-dropdown-item divided command="logout">
                    <el-icon><SwitchButton /></el-icon>
                    退出登录
                  </el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>
          </div>
        </nav>
      </div>
    </header>

    <main class="main-content">
      <ImageGenerator v-if="activeTab === 'generator'" />
      <ImageHistory v-if="activeTab === 'history'" />
    </main>

    <footer class="footer">
      <p>© 2024 AI Image Generator. Powered by Z-Image Turbo.</p>
    </footer>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue';
import { useAuthStore } from '@/stores/auth';
import { Picture, Avatar, ArrowDown, SwitchButton } from '@element-plus/icons-vue';
import { ElMessageBox } from 'element-plus';
import ImageGenerator from '@/components/ImageGenerator.vue';
import ImageHistory from '@/components/ImageHistory.vue';

const authStore = useAuthStore();
const activeTab = ref('generator');

const handleTabChange = (key) => {
  activeTab.value = key;
};

const handleCommand = (command) => {
  if (command === 'logout') {
    ElMessageBox.confirm('确定要退出登录吗？', '提示', {
      confirmButtonText: '确定',
      cancelButtonText: '取消',
      type: 'warning',
    })
      .then(() => {
        authStore.logout();
      })
      .catch(() => {
        // 取消操作
      });
  }
};

onMounted(() => {
  // 检查登录状态
  authStore.checkAuth();
});
</script>

<style scoped>
.home-page {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}

.header {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(10px);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  position: sticky;
  top: 0;
  z-index: 1000;
}

.container {
  max-width: 1400px;
  margin: 0 auto;
  padding: 0 20px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.logo {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 24px;
  font-weight: 700;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  margin: 0;
}

.nav {
  flex: 1;
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: 24px;
}

.user-info {
  margin-left: 24px;
}

.user-avatar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  background: #f3f4f6;
  border-radius: 12px;
  cursor: pointer;
  transition: all 0.3s;
}

.user-avatar:hover {
  background: #e5e7eb;
}

.username {
  font-size: 14px;
  font-weight: 600;
  color: #374151;
  max-width: 150px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.arrow {
  font-size: 12px;
  color: #9ca3af;
}

.user-detail {
  padding: 8px 0;
}

.user-name {
  font-weight: 600;
  color: #111827;
  font-size: 14px;
  margin-bottom: 4px;
}

.user-email {
  font-size: 12px;
  color: #6b7280;
}

.main-content {
  flex: 1;
  padding: 40px 0;
  background: white;
  margin: 20px;
  border-radius: 12px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1);
}

.footer {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(10px);
  text-align: center;
  padding: 20px;
  color: #666;
}

:deep(.el-menu--horizontal) {
  border-bottom: none;
  background: transparent;
}

:deep(.el-menu-item) {
  font-size: 16px;
  font-weight: 500;
  border-bottom: 2px solid transparent;
}

:deep(.el-menu-item.is-active) {
  color: #667eea;
  border-bottom-color: #667eea;
}

:deep(.el-menu-item:hover) {
  background: rgba(102, 126, 234, 0.05);
}

:deep(.el-dropdown-menu__item) {
  padding: 10px 20px;
}

:deep(.el-dropdown-menu__item:not(.is-disabled):hover) {
  background: #f3f4f6;
  color: #667eea;
}

/* 响应式 */
@media (max-width: 768px) {
  .container {
    flex-direction: column;
    align-items: flex-start;
    gap: 16px;
  }

  .nav {
    width: 100%;
    justify-content: space-between;
  }

  .user-info {
    margin-left: 0;
  }

  .username {
    display: none;
  }
}
</style>
