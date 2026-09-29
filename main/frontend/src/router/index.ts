import { createRouter, createWebHashHistory, } from 'vue-router';
import { useInfo } from '@/common/info';
import { hasSession } from '@/common/session';
import { useSettings } from '@/common/settings';
import type { LogoutResponse } from '@/common/types';
import Access from '@/views/Access.vue';
import Dashboard from '@/views/Dashboard.vue';
import Lock from '@/views/Lock.vue';
import Login from '@/views/Login.vue';
import Network from '@/views/Network.vue';
import SerialPorts from '@/views/SerialPorts.vue';
import System from '@/views/System.vue';
import { api } from '@/utils/api';
import { checkSession } from './checkSession';

const enterFpAcsPage = async () => {
  const { info, fetchInfo } = useInfo();
  const { refresh } = useSettings();
  await Promise.all([
    fetchInfo(),
    refresh(),
  ]);
  // Firmware without the feature has no GET /fp/*: a typed-in or bookmarked URL goes to the overview.
  if (info.value?.fp_acs !== true) {
    return { name: 'dashboard' };
  }
};

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
      // menuName doubles as the post-login redirect path (checkSession, Login.vue), hence equal to the path.
      path: '/access',
      name: 'access',
      component: Access,
      meta: {
        requiresAuth: true,
        menuName: 'access',
        menuGroup: 'overview',
        menuIcon: 'fingerprint',
        requiresInfoFlag: 'fp_acs',
      },
      beforeEnter: [checkSession, enterFpAcsPage],
    },
    {
      path: '/network',
      name: 'network',
      component: Network,
      meta: { requiresAuth: true, menuName: 'network', menuGroup: 'configuration', menuIcon: 'network' },
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
      component: SerialPorts,
      meta: { requiresAuth: true, menuName: 'serial_ports', menuGroup: 'configuration', menuIcon: 'sliders' },
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
      path: '/lock',
      name: 'lock',
      component: Lock,
      meta: {
        requiresAuth: true,
        menuName: 'lock',
        menuGroup: 'configuration',
        menuIcon: 'lock',
        requiresInfoFlag: 'fp_acs',
      },
      beforeEnter: [checkSession, enterFpAcsPage],
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
