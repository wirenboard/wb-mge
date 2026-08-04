/**
 * Integration tests for PacketSenderPopup.vue.
 *
 * PSP-I-001 — switching the function code never rewrites the value:
 *   Changing the FC — or the read/write mode, which sets the FC — used to overwrite the Count /
 *   Value(s) field with a default whenever the new FC could not accept what was there, throwing
 *   away what the user had typed. The field must now keep the entered text unconditionally; a
 *   value the selected FC rejects is instead reported by an inline message next to the input,
 *   with the send button gated off by the usual rule (a frame that cannot be built). A value the
 *   FC accepts — including the untouched default — must show no message at all.
 *
 * PSP-I-002 — sending waits for the capture:
 *   The sniffer records only what arrives while its WebSocket is open, so the frame must not be
 *   transmitted until the capture is established. The popup awaits the `ensureCapture` prop
 *   BEFORE calling the send endpoint, and announces an auto-start so the UI does not change
 *   state behind the user's back. It reports each of the three outcomes truthfully — in
 *   particular it must NOT claim an auto-start when the capture never came up — and a capture
 *   that cannot be established makes the frame go out unrecorded, never cancels it.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import PacketSenderPopup from '@/components/PacketSenderPopup.vue';
import { senderState } from '@/utils/senderState';
import type { CaptureReadyOutcome } from '@/utils/snifferUtils';

const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

/** The hint shown when the frame went out with no capture recording it. */
const NOT_RUNNING_HINT = 'Sent without a running capture, so the packets will not appear in the list';

/** Inline validation messages shown next to the Count / Value(s) field. */
const COUNT_LIMIT_MSG = 'Count must be between 1 and 2000';
const COIL_LIMIT_MSG = 'Coil value must be 0 or 1';

/** Mount the popup with an `ensureCapture` that reports "already running" and resolves at once. */
function mountPopup(
  ensureCapture: () => Promise<CaptureReadyOutcome> = () => Promise.resolve('already-running'),
) {
  return mount(PacketSenderPopup, {
    props: { portNum: '1', txDisabled: false, ensureCapture },
    global: { plugins: [i18n] },
  });
}

/** The form state is a module-level singleton (it survives popup close/open) — reset it. */
function resetSenderState() {
  senderState.mode = 'read';
  senderState.slaveId = '01';
  senderState.fc = '03';
  senderState.address = '0x0000';
  senderState.value = '10';
}

/** Set the FC via the real <select>, so the component's watcher runs exactly as in the UI. */
async function selectFc(wrapper: ReturnType<typeof mountPopup>, fc: string) {
  await wrapper.find('select.form-field-input').setValue(fc);
  await flushPromises();
}

/** Click the Read / Write segmented buttons. */
async function setMode(wrapper: ReturnType<typeof mountPopup>, label: 'Read' | 'Write') {
  const btn = wrapper.findAll('.sniffer-sender-seg-btn').find(b => b.text() === label);
  expect(btn, `${label} mode button must be found`).toBeDefined();
  await btn!.trigger('click');
  await flushPromises();
}

/** What the Count / Value(s) input actually shows — the rendered text, not just the state. */
function shownValue(wrapper: ReturnType<typeof mountPopup>): string {
  const inputs = wrapper.findAll('input.form-field-input');
  expect(inputs, 'form inputs: slave ID, address, value').toHaveLength(3);
  return (inputs[2].element as HTMLInputElement).value;
}

/** The inline validation message next to the Count / Value(s) field; '' when there is none. */
function valueMessage(wrapper: ReturnType<typeof mountPopup>): string {
  const msg = wrapper.find('.form-field-error');
  return msg.exists() ? msg.text() : '';
}

/** Is the send button gated off? */
function sendIsDisabled(wrapper: ReturnType<typeof mountPopup>): boolean {
  return wrapper.find('.sniffer-sender-foot-send').attributes('disabled') !== undefined;
}

describe('PSP-I-001: switching the function code never rewrites the value', () => {
  beforeEach(() => {
    resetSenderState();
  });

  it('the untouched default is accepted: no message, send enabled', async () => {
    const wrapper = mountPopup();
    await flushPromises();

    expect(shownValue(wrapper)).toBe('10');
    expect(valueMessage(wrapper)).toBe('');
    expect(sendIsDisabled(wrapper)).toBe(false);
    wrapper.unmount();
  });

  it('read mode FC03 -> FC04: a valid count survives the switch with no message', async () => {
    const wrapper = mountPopup();
    senderState.value = '25';
    await flushPromises();

    await selectFc(wrapper, '04');

    expect(senderState.fc).toBe('04');
    expect(senderState.value).toBe('25');
    expect(valueMessage(wrapper)).toBe('');
    wrapper.unmount();
  });

  it('read mode FC01 <-> FC03: the count is preserved both ways and stays valid', async () => {
    const wrapper = mountPopup();
    senderState.value = '125';
    await flushPromises();

    await selectFc(wrapper, '01'); // both read FCs share the 1..2000 count range

    expect(senderState.fc).toBe('01');
    expect(shownValue(wrapper)).toBe('125');
    expect(valueMessage(wrapper)).toBe('');
    expect(sendIsDisabled(wrapper)).toBe(false);

    await selectFc(wrapper, '03');

    expect(senderState.fc).toBe('03');
    expect(shownValue(wrapper)).toBe('125');
    expect(valueMessage(wrapper)).toBe('');
    expect(sendIsDisabled(wrapper)).toBe(false);
    wrapper.unmount();
  });

  it('read mode FC03 -> FC04: an out-of-range count is kept and the limit is spelled out', async () => {
    const wrapper = mountPopup();
    // A read count must be 1..2000; 5000 cannot build a frame for FC04 either.
    senderState.value = '5000';
    await flushPromises();

    await selectFc(wrapper, '04');

    expect(shownValue(wrapper)).toBe('5000');
    expect(valueMessage(wrapper)).toBe(COUNT_LIMIT_MSG);
    expect(sendIsDisabled(wrapper)).toBe(true);
    wrapper.unmount();
  });

  it('write mode FC06 -> FC05: a register value a coil cannot hold is kept and explained', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    senderState.value = '500';
    await flushPromises();

    await selectFc(wrapper, '05');

    expect(senderState.fc).toBe('05');
    // The typed text stays put — the old behaviour replaced it with the coil default '1'.
    expect(senderState.value).toBe('500');
    expect(shownValue(wrapper)).toBe('500');
    expect(valueMessage(wrapper)).toBe(COIL_LIMIT_MSG);
    expect(sendIsDisabled(wrapper)).toBe(true);
    wrapper.unmount();
  });

  it('write mode FC06 -> FC05 -> FC06: the value comes back usable, message and gate lift', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    senderState.value = '500';
    await flushPromises();
    await selectFc(wrapper, '05');

    await selectFc(wrapper, '06'); // back to the FC that accepts it

    expect(senderState.fc).toBe('06');
    expect(shownValue(wrapper)).toBe('500');
    expect(valueMessage(wrapper)).toBe('');
    expect(sendIsDisabled(wrapper)).toBe(false);
    wrapper.unmount();
  });

  it('write mode FC05 -> FC06: a 0/1 value a register accepts is kept (the other direction)', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    await selectFc(wrapper, '05');
    senderState.value = '1';
    await flushPromises();

    await selectFc(wrapper, '06');

    expect(senderState.fc).toBe('06');
    expect(senderState.value).toBe('1');
    expect(valueMessage(wrapper)).toBe('');
    wrapper.unmount();
  });

  it('write mode FC16 -> FC15: a register list a coil list cannot hold is kept and explained', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    await selectFc(wrapper, '10');
    senderState.value = '10, 20, 30';
    await flushPromises();

    await selectFc(wrapper, '0f');

    expect(shownValue(wrapper)).toBe('10, 20, 30');
    expect(valueMessage(wrapper)).toBe(COIL_LIMIT_MSG);
    expect(sendIsDisabled(wrapper)).toBe(true);
    wrapper.unmount();
  });

  it('write mode FC15 -> FC16: a 0/1 list a register list accepts is kept', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    await selectFc(wrapper, '0f');
    senderState.value = '1, 0, 1';
    await flushPromises();

    await selectFc(wrapper, '10');

    expect(senderState.fc).toBe('10');
    expect(senderState.value).toBe('1, 0, 1');
    expect(valueMessage(wrapper)).toBe('');
    wrapper.unmount();
  });

  it('read -> write mode keeps a value both sides accept (mode switch changes the FC too)', async () => {
    const wrapper = mountPopup();
    senderState.value = '25';
    await flushPromises();

    await setMode(wrapper, 'Write'); // FC03 -> FC06, 25 is a valid register value

    expect(senderState.fc).toBe('06');
    expect(senderState.value).toBe('25');
    expect(valueMessage(wrapper)).toBe('');
    wrapper.unmount();
  });

  it('write -> read mode keeps a value the new FC cannot accept and names the read limit', async () => {
    const wrapper = mountPopup();
    // A register value may be up to 65535, a read count only up to 2000, so 5000 is valid for
    // FC06 and invalid for FC03 — the mode switch must keep it and say why it cannot be sent.
    await setMode(wrapper, 'Write');
    senderState.value = '5000'; // valid FC06 register value
    await flushPromises();

    await setMode(wrapper, 'Read'); // FC06 -> FC03, count must be 1..2000

    expect(senderState.fc).toBe('03');
    expect(shownValue(wrapper)).toBe('5000');
    expect(valueMessage(wrapper)).toBe(COUNT_LIMIT_MSG);
    expect(sendIsDisabled(wrapper)).toBe(true);
    wrapper.unmount();
  });
});

describe('PSP-I-002: sending waits for the capture', () => {
  const fetchMock = vi.fn();

  beforeEach(() => {
    resetSenderState();
    fetchMock.mockReset();
    fetchMock.mockResolvedValue({ ok: true, json: async () => ({ sent: 8 }) });
    vi.stubGlobal('fetch', fetchMock);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('does not transmit until ensureCapture resolves, then announces the auto-start', async () => {
    let releaseCapture: ((outcome: CaptureReadyOutcome) => void) | null = null;
    const ensureCapture = vi.fn(
      () => new Promise<CaptureReadyOutcome>((resolve) => {
        releaseCapture = resolve;
      }),
    );

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // The capture was asked for, and NOTHING has gone out on the wire yet.
    expect(ensureCapture).toHaveBeenCalledTimes(1);
    expect(fetchMock).not.toHaveBeenCalled();

    // The capture is now established (and it was this send that started it).
    releaseCapture!('started');
    await flushPromises();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    // The auto-start is visible in the popup hint, replacing the CRC hint.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(
      'Capture started automatically so the packets appear in the list',
    );

    wrapper.unmount();
  });

  it('capture already running: sends normally and shows no auto-start notice', async () => {
    const ensureCapture = vi.fn((): Promise<CaptureReadyOutcome> => Promise.resolve('already-running'));

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    expect(ensureCapture).toHaveBeenCalledTimes(1);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    // The unchanged hint proves nothing claimed to have been started.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe('CRC computed automatically');

    wrapper.unmount();
  });

  it('capture never came up: the frame goes out and the hint says so — no "started" claim', async () => {
    const ensureCapture = vi.fn((): Promise<CaptureReadyOutcome> => Promise.resolve('not-established'));

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // The frame is still transmitted — a capture that cannot be established must not block a send.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    // And the user is told the packets will NOT show up, instead of the opposite.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);

    wrapper.unmount();
  });

  it('ensureCapture throwing degrades to sending unrecorded, it does not cancel the send', async () => {
    // `new WebSocket(url)` throws synchronously on a malformed URL / mixed content, so the prop
    // can reject. Sending a frame never depended on the sniffer socket and must not start now.
    const ensureCapture = vi.fn((): Promise<CaptureReadyOutcome> => Promise.reject(new Error('SecurityError')));

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    // The WebSocket failure is not reported as a send failure; the send itself succeeded.
    expect(wrapper.find('.sniffer-sender-error').exists()).toBe(false);
    expect(wrapper.find('.sniffer-sender-success').text()).toBe('Sent 8 bytes to port 1');
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);

    wrapper.unmount();
  });
});
