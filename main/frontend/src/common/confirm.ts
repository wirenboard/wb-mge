import { reactive } from 'vue';

interface ConfirmState {
  open: boolean;
  message: string;
  confirmLabel: string;
}

const confirmState = reactive<ConfirmState>({ open: false, message: '', confirmLabel: '' });
let resolvePending: ((confirmed: boolean) => void) | null = null;

export const useConfirm = () => {
  const settleConfirm = (confirmed: boolean) => {
    const resolve = resolvePending;
    resolvePending = null;
    confirmState.open = false;
    resolve?.(confirmed);
  };

  // One question on screen at a time: a new one answers the one it replaces with "no".
  const confirmDialog = (message: string, confirmLabel: string): Promise<boolean> => {
    resolvePending?.(false);
    return new Promise((resolve) => {
      resolvePending = resolve;
      Object.assign(confirmState, { open: true, message, confirmLabel });
    });
  };

  return {
    confirmState,
    confirmDialog,
    settleConfirm,
  };
};
