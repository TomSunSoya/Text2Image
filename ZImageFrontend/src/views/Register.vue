<template>
  <div class="register-page">
    <!-- 背景装饰 -->
    <div class="bg-decoration">
      <div class="grid-overlay"></div>
      <div class="shape shape-1"></div>
      <div class="shape shape-2"></div>
    </div>

    <!-- 注册卡片 -->
    <div class="register-container">
      <div class="register-card">
        <!-- 返回按钮 -->
        <router-link to="/login" class="back-btn">
          <el-icon><ArrowLeft /></el-icon>
          返回登录
        </router-link>

        <!-- 头部 -->
        <div class="header">
          <div class="logo-wrapper">
            <el-icon :size="40" class="logo-icon">
              <Picture />
            </el-icon>
          </div>
          <h1 class="title">创建账号</h1>
          <p class="subtitle">开始你的 AI 创作之旅</p>
        </div>

        <!-- 注册表单 -->
        <el-form
          ref="registerFormRef"
          :model="registerForm"
          :rules="rules"
          class="register-form"
          label-position="top"
        >
          <el-form-item prop="username" label="用户名">
            <el-input
              v-model="registerForm.username"
              placeholder="请输入用户名（3-20个字符）"
              size="large"
              :prefix-icon="User"
              clearable
            />
          </el-form-item>

          <el-form-item prop="email" label="邮箱">
            <el-input
              v-model="registerForm.email"
              type="email"
              placeholder="请输入邮箱地址"
              size="large"
              :prefix-icon="Message"
              clearable
            />
          </el-form-item>

          <el-form-item prop="nickname" label="昵称（可选）">
            <el-input
              v-model="registerForm.nickname"
              placeholder="请输入昵称"
              size="large"
              :prefix-icon="Avatar"
              clearable
            />
          </el-form-item>

          <el-form-item prop="password" label="密码">
            <el-input
              v-model="registerForm.password"
              type="password"
              placeholder="请输入密码（至少6个字符）"
              size="large"
              :prefix-icon="Lock"
              show-password
            />
          </el-form-item>

          <el-form-item prop="confirmPassword" label="确认密码">
            <el-input
              v-model="registerForm.confirmPassword"
              type="password"
              placeholder="请再次输入密码"
              size="large"
              :prefix-icon="Lock"
              show-password
              @keyup.enter="handleRegister"
            />
          </el-form-item>

          <el-form-item class="terms">
            <el-checkbox v-model="agreeTerms">
              我已阅读并同意
              <a href="#" class="terms-link" @click.prevent>服务条款</a>
              和
              <a href="#" class="terms-link" @click.prevent>隐私政策</a>
            </el-checkbox>
          </el-form-item>

          <el-button
            type="primary"
            size="large"
            class="register-btn"
            :loading="isRegistering"
            :disabled="!agreeTerms"
            @click="handleRegister"
          >
            {{ isRegistering ? '注册中...' : '注册' }}
          </el-button>
        </el-form>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue';
import { useAuthStore } from '@/stores/auth';
import { Picture, User, Lock, Message, Avatar, ArrowLeft } from '@element-plus/icons-vue';
import { ElMessage } from 'element-plus';

const authStore = useAuthStore();
const registerFormRef = ref(null);
const isRegistering = ref(false);
const agreeTerms = ref(false);

const registerForm = reactive({
  username: '',
  email: '',
  nickname: '',
  password: '',
  confirmPassword: '',
});

// 验证确认密码
const validateConfirmPassword = (rule, value, callback) => {
  if (value === '') {
    callback(new Error('请再次输入密码'));
  } else if (value !== registerForm.password) {
    callback(new Error('两次输入的密码不一致'));
  } else {
    callback();
  }
};

const rules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 3, max: 20, message: '用户名长度应在 3-20 个字符', trigger: 'blur' },
    { pattern: /^[a-zA-Z0-9_]+$/, message: '用户名只能包含字母、数字和下划线', trigger: 'blur' },
  ],
  email: [
    { required: true, message: '请输入邮箱', trigger: 'blur' },
    { type: 'email', message: '请输入有效的邮箱地址', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, max: 20, message: '密码长度应在 6-20 个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { validator: validateConfirmPassword, trigger: 'blur' },
  ],
};

const handleRegister = async () => {
  if (!agreeTerms.value) {
    ElMessage.warning('请阅读并同意服务条款和隐私政策');
    return;
  }

  try {
    await registerFormRef.value.validate();
    isRegistering.value = true;

    const { confirmPassword, ...userData } = registerForm;
    await authStore.register(userData);
  } catch (error) {
    if (error !== false) {
      console.error('Register failed:', error);
    }
  } finally {
    isRegistering.value = false;
  }
};
</script>

<style scoped>
.register-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  position: relative;
  overflow: hidden;
  font-family:
    'SF Pro Display',
    -apple-system,
    BlinkMacSystemFont,
    sans-serif;
  padding: 40px 20px;
}

/* 背景装饰 */
.bg-decoration {
  position: absolute;
  inset: 0;
  overflow: hidden;
  pointer-events: none;
}

.grid-overlay {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.05) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.05) 1px, transparent 1px);
  background-size: 50px 50px;
  animation: gridMove 20s linear infinite;
}

@keyframes gridMove {
  0% {
    transform: translate(0, 0);
  }
  100% {
    transform: translate(50px, 50px);
  }
}

.shape {
  position: absolute;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(40px);
}

.shape-1 {
  width: 600px;
  height: 600px;
  top: -300px;
  right: -200px;
  animation: float 25s infinite ease-in-out;
}

.shape-2 {
  width: 400px;
  height: 400px;
  bottom: -200px;
  left: -150px;
  animation: float 20s infinite ease-in-out reverse;
}

@keyframes float {
  0%,
  100% {
    transform: translate(0, 0);
  }
  50% {
    transform: translate(50px, -50px);
  }
}

/* 注册容器 */
.register-container {
  position: relative;
  z-index: 1;
  width: 100%;
  max-width: 480px;
  animation: slideUp 0.6s ease-out;
}

@keyframes slideUp {
  from {
    opacity: 0;
    transform: translateY(30px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.register-card {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(20px);
  border-radius: 24px;
  padding: 40px;
  box-shadow:
    0 20px 60px rgba(0, 0, 0, 0.3),
    0 0 0 1px rgba(255, 255, 255, 0.1) inset;
  position: relative;
}

/* 返回按钮 */
.back-btn {
  position: absolute;
  top: 20px;
  left: 20px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #6b7280;
  text-decoration: none;
  font-size: 14px;
  font-weight: 500;
  transition: all 0.3s;
  padding: 8px 12px;
  border-radius: 8px;
}

.back-btn:hover {
  color: #667eea;
  background: rgba(102, 126, 234, 0.1);
}

/* 头部 */
.header {
  text-align: center;
  margin-bottom: 32px;
  padding-top: 20px;
}

.logo-wrapper {
  display: inline-block;
  padding: 16px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border-radius: 16px;
  margin-bottom: 20px;
  box-shadow: 0 8px 24px rgba(102, 126, 234, 0.3);
}

.logo-icon {
  color: white;
  display: block;
}

.title {
  font-size: 28px;
  font-weight: 700;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  margin: 0 0 8px 0;
  letter-spacing: -0.5px;
}

.subtitle {
  color: #6b7280;
  font-size: 15px;
  margin: 0;
  font-weight: 400;
}

/* 表单 */
.register-form {
  margin-bottom: 0;
}

:deep(.el-form-item__label) {
  color: #374151;
  font-weight: 600;
  font-size: 14px;
  margin-bottom: 8px;
}

:deep(.el-input__wrapper) {
  background: #f9fafb;
  border-radius: 12px;
  padding: 12px 16px;
  box-shadow: none;
  transition: all 0.3s;
}

:deep(.el-input__wrapper:hover) {
  background: #f3f4f6;
}

:deep(.el-input__wrapper.is-focus) {
  background: white;
  box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
}

:deep(.el-form-item) {
  margin-bottom: 20px;
}

.terms {
  margin-bottom: 24px;
}

:deep(.el-checkbox__label) {
  color: #6b7280;
  font-size: 13px;
  line-height: 1.6;
}

.terms-link {
  color: #667eea;
  text-decoration: none;
  font-weight: 600;
}

.terms-link:hover {
  text-decoration: underline;
}

/* 注册按钮 */
.register-btn {
  width: 100%;
  height: 48px;
  font-size: 16px;
  font-weight: 600;
  border-radius: 12px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border: none;
  box-shadow: 0 4px 14px rgba(102, 126, 234, 0.4);
  transition: all 0.3s;
  letter-spacing: 0.5px;
}

.register-btn:not(:disabled):hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(102, 126, 234, 0.5);
}

.register-btn:not(:disabled):active {
  transform: translateY(0);
}

.register-btn:disabled {
  background: #d1d5db;
  box-shadow: none;
  cursor: not-allowed;
}

/* 响应式 */
@media (max-width: 640px) {
  .register-page {
    padding: 20px 16px;
  }

  .register-card {
    padding: 32px 24px;
  }

  .title {
    font-size: 24px;
  }

  .back-btn {
    position: relative;
    top: 0;
    left: 0;
    margin-bottom: 16px;
  }

  .header {
    padding-top: 0;
  }
}
</style>
