import { createRouter, createWebHistory } from 'vue-router';
import { useAuthStore } from '@/stores/auth';
import Login from '@/views/Login.vue';
import Register from '@/views/Register.vue';
import Home from '@/views/Home.vue';
import Profile from '@/views/Profile.vue';
import Admin from '@/views/Admin.vue';
import NotFound from '@/views/NotFound.vue';

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: Login,
    meta: { requiresAuth: false, guestOnly: true },
  },
  {
    path: '/register',
    name: 'Register',
    component: Register,
    meta: { requiresAuth: false, guestOnly: true },
  },
  {
    path: '/',
    name: 'Home',
    component: Home,
    meta: { requiresAuth: true },
  },
  {
    path: '/profile',
    name: 'Profile',
    component: Profile,
    meta: { requiresAuth: true },
  },
  {
    path: '/admin',
    name: 'Admin',
    component: Admin,
    meta: { requiresAuth: true, requiresAdmin: true },
  },
  {
    path: '/403',
    name: 'Forbidden',
    component: NotFound,
    meta: { requiresAuth: true },
  },
  {
    path: '/:pathMatch(.*)*',
    name: 'NotFound',
    component: NotFound,
  },
];

const router = createRouter({
  history: createWebHistory(),
  routes,
});

// 全局前置守卫
router.beforeEach((to, from, next) => {
  const authStore = useAuthStore();
  const isAuthenticated = authStore.checkAuth();

  if (to.meta.requiresAdmin && !authStore.isAdmin) {
    next('/403');
    return;
  }

  if (to.meta.requiresAuth) {
    if (!isAuthenticated) {
      // 未登录，跳转到登录页
      next({
        path: '/login',
        query: { redirect: to.fullPath }, // 保存目标路由，登录后跳转
      });
    } else {
      next();
    }
  }
  // 仅游客可访问的页面（登录、注册）
  else if (to.meta.guestOnly) {
    if (isAuthenticated) {
      // 已登录用户访问登录页，跳转到首页
      next('/');
    } else {
      next();
    }
  }
  // 公开页面
  else {
    next();
  }
});

export default router;
