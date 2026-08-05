import { reactive } from 'vue';

export type SenderMode = 'read' | 'write';

export interface SenderFormState {
  mode: SenderMode;
  slaveId: string;
  fc: string;
  address: string;
  value: string;
}

/**
 * Module-level reactive singleton holding the packet-sender form values.
 *
 * The sender popup is mounted with `v-if` and is therefore destroyed on close,
 * which would reset any component-local refs. Keeping the form state here means
 * the values survive close/open within a session (no localStorage needed).
 * The initial values are the defaults shown on the very first open.
 */
export const senderState = reactive<SenderFormState>({
  mode: 'read',
  slaveId: '01',
  fc: '03',
  address: '0x0000',
  value: '10',
});
