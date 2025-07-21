import { createRouter, createWebHashHistory, } from 'vue-router';
import { useInfo } from '@/common/info';
import { hasSession } from '@/common/session';
import { useSettings } from '@/common/settings';
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
      meta: { requiresAuth: true, menuName: 'dashboard' },
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
      meta: { requiresAuth: true, menuName: 'settings' },
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
      meta: { requiresAuth: true, menuName: 'system' },
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
      redirect:  () => {
        api('logout', {});
        hasSession.value = false;
        return { path: '/login' };
      },
    },
  ],
});

// router.beforeRouteLeave(setTitle);

export default router;
