import type { NavigationGuardReturn, RouteLocation, RouteLocationRaw } from 'vue-router';
import { useSession } from '@/common/session';

export const checkSession = async (to: RouteLocation): Promise<NavigationGuardReturn> => {
  const session = await useSession();

  if (to.path === '/login' && session) {
    return { path: to.query.redirect === 'dashboard' ? '' : to.query.redirect as string };
  }

  if (to.meta.requiresAuth && !session) {
    const output: RouteLocationRaw = { path: '/login' };

    if (to.meta.menuName !== 'dashboard') {
      output.query = { redirect: to.meta.menuName as string };
    }

    return output;
  }
};
