import { createRouter, createWebHashHistory, } from 'vue-router';
import { useInfo } from '@/common/info';
import { hasSession } from '@/common/session';
import { useSettings } from '@/common/settings';
import type { LogoutResponse } from '@/common/types';
import Dashboard from '@/views/Dashboard.vue';
import Login from '@/views/Login.vue';
import Settings from '@/views/Settings.vue';
import System from '@/views/System.vue';
import { api } from '@/utils/api';
import { checkSession } from './checkSession';

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    {
      path: '/',
      name: 'dashboard',
      component: Dashboard,
      meta: { requiresAuth: true, menuName: 'dashboard', menuGroup: 'overview', menuIcon: 'gauge' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/settings',
      name: 'settings',
      component: Settings,
      meta: { requiresAuth: true, menuName: 'settings', menuGroup: 'configuration', menuIcon: 'sliders' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();

        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/system',
      name: 'system',
      component: System,
      meta: { requiresAuth: true, menuName: 'system', menuGroup: 'configuration', menuIcon: 'cpu' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/login',
      name: 'login',
      component: Login,
      beforeEnter: [checkSession],
    },
    {
      path: '/logout',
      redirect: () => {
        api<LogoutResponse>('logout', { method: 'POST' });
        hasSession.value = false;
        return { path: '/login' };
      },
    },
  ],
});

export default router;
