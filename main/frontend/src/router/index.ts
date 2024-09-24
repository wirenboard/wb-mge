import { createRouter, createWebHashHistory, } from 'vue-router';
import { hasSession, useSession } from '@/common/session';
import Dashboard from '@/views/Dashboard.vue';
import Traffic from '@/views/Traffic.vue';
import Login from '@/views/Login.vue';
import Serial from '@/views/Serial.vue';
import Bridge from '@/views/Bridge.vue';
import Network from '@/views/Network.vue';
import System from '@/views/System.vue';
import { api } from '@/utils/api';

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    {
      path: '/',
      name: 'dashboard',
      component: Dashboard,
      meta: { requiresAuth: true, menuName: 'dashboard' },
    },
    {
      path: '/traffic',
      name: 'traffic',
      component: Traffic,
      meta: { requiresAuth: true, menuName: 'traffic' },
    },
    {
      path: '/network',
      name: 'network',
      component: Network,
      meta: { requiresAuth: true, menuName: 'network' },
    },
    {
      path: '/serial',
      name: 'serial',
      component: Serial,
      meta: { requiresAuth: true, menuName: 'serial' },
    },
    {
      path: '/bridge',
      name: 'bridge',
      component: Bridge,
      meta: { requiresAuth: true, menuName: 'bridge' },
    },
    {
      path: '/system',
      name: 'system',
      component: System,
      meta: { requiresAuth: true, menuName: 'system' },
    },
    {
      path: '/login',
      name: 'login',
      component: Login,
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

// @ts-ignore
router.beforeEach(async (to) => {
  const session = await useSession();
  if (to.path === '/login' && session) {
    return { path: '' };
  }

  if (to.meta.requiresAuth && !session) {
    return { path: '/login', query: { redirect: to.meta.menuName } };
  }
});

export default router;
