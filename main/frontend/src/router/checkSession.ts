import { RouteLocation } from 'vue-router';
import { useSession } from '@/common/session';

export const checkSession = async (to: RouteLocation) => {
  const session = await useSession();

  if (to.path === '/login' && session) {
    return { path: to.query.redirect === 'dashboard' ? '' : to.query.redirect };
  }

  if (to.meta.requiresAuth && !session) {
    const output: any = { path: '/login' };

    if (to.meta.menuName !== 'dashboard') {
      output.query = { redirect: to.meta.menuName };
    }

    return output;
  }
};
