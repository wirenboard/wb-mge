/**
 * Integration tests for the "Last event" card and the enrollment dialog of Access.vue. The status
 * poll (useFpStatus) is the real one; the device is stubbed: api answers GET /fp/status, /fp/users,
 * /uptime and POST /fp/enroll, and ky (which fetchFpImage uses for the raw image) answers GET
 * /fp/image. The two modals are teleported to document.body, so their content is looked up there.
 *
 * ACC-I-001 — the source row: Fingerprint, MQTT, Telegram, and "Telegram · <chat>".
 * ACC-I-002 — the result pill and the user of the event; nobody reads as a dash.
 * ACC-I-003 — the image is shown beside the event it was taken for, and only beside that one: a
 *              scan with no image of its own does not borrow the previous one.
 * ACC-I-004 — no event since boot: no pill, the "no events" note, no image.
 * ACC-I-005 — "Add finger" opens the enrollment dialog over the editor, at the first take at once,
 *              even before a status answer has replaced the previous enrollment's outcome.
 * ACC-I-006 — an enrollment started elsewhere opens the dialog alone, not the editor.
 * ACC-I-007 — the dialog's prompt, state class (the ring colour), picture and progress per state.
 * ACC-I-008 — the dialog shows only an image taken during its enrollment: never an older one, the
 *              5th take once it is uploaded, not that one once the next enrollment starts, and not
 *              a scan at the door that came after it.
 * ACC-I-009 — Cancel while the enrollment runs (Esc does not close it), Close once it is over.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import ky from 'ky';
import { hasSession } from '@/common/session';
import { messages } from '@/i18n/messages';
import { api } from '@/utils/api';
import type { FpEnrollState, FpEvent, FpStatus, FpUser } from '@/common/types';

vi.mock('@/utils/api', () => ({
  ApiError: class ApiError extends Error {
    deviceError?: string;
  },
  api: vi.fn(),
}));

vi.mock('ky', () => ({
  default: vi.fn(),
}));

const STATUS_POLL_MS = 1000;
const IMAGE_WIDTH = 4;
const IMAGE_HEIGHT = 2;
const ENROLL_STEPS = 5;
const ANNA_ID = 1;

interface EnrollRequest {
  action: 'start' | 'cancel';
  user_id?: number;
}

const makeEvent = (seq: number, patch: Partial<FpEvent> = {}): FpEvent => ({
  seq,
  uptime_s: 50,
  source: 'fingerprint',
  result: 'granted',
  user_id: 1,
  user_name: 'Anna',
  score: 120,
  chat: null,
  ...patch,
});

const makeEnroll = (state: FpEnrollState, patch: Partial<FpStatus['enroll']> = {}): FpStatus['enroll'] => ({
  state, user_id: ANNA_ID, step: 1, steps: ENROLL_STEPS, slot: null, error: null, ...patch,
});

const IDLE_ENROLL: FpStatus['enroll'] = { state: 'idle', user_id: null, step: 0, steps: ENROLL_STEPS, slot: null, error: null };

const makeStatus = (
  lastEvent: FpEvent | null,
  image: FpStatus['image'] = null,
  enroll: FpStatus['enroll'] = IDLE_ENROLL,
): FpStatus => ({
  sensor: { online: true, capacity: 200, used: 3 },
  users_rev: 1,
  enroll,
  last_event: lastEvent,
  image,
  lock: { on: false, online: true, config_error: null },
  telegram: { enabled: false, online: false, error: null },
});

const makeImage = (seq: number, eventSeq: number): FpStatus['image'] => ({
  seq, event_seq: eventSeq, width: IMAGE_WIDTH, height: IMAGE_HEIGHT,
});

const ANNA: FpUser = {
  id: ANNA_ID, name: 'Anna', enabled: true, allow_fp: true, allow_tg: false, tg_username: '', fingers: [3],
};

let status: FpStatus;
let users: FpUser[];
let imageRequests: number;
let enrollRequests: EnrollRequest[];
// While set, GET /fp/status waits for it: a device that has not answered the poll yet.
let statusGate: Promise<void> | null;

const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

// Every wrapper is unmounted after its test, a failed one included: a test that stops half-way must
// not leave its modals in document.body, or its status poll running, for the next one.
const mounted = new Set<{ unmount: () => void }>();

const unmountAccess = (wrapper: { unmount: () => void }) => {
  if (mounted.delete(wrapper)) {
    wrapper.unmount();
  }
};

const mountAccess = async () => {
  const { default: Access } = await import('@/views/Access.vue');
  const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/', component: { template: '<div/>' } }] });
  await router.push('/');
  await router.isReady();
  const wrapper = mount(Access, {
    global: {
      plugins: [i18n, router],
      stubs: { Layout: { template: '<div><slot /></div>' }, Heading: true },
    },
  });
  mounted.add(wrapper);
  await flushPromises();
  return wrapper;
};

type AccessWrapper = Awaited<ReturnType<typeof mountAccess>>;

// The "Last event" card: the second card of the top row.
const eventCard = (wrapper: AccessWrapper) => wrapper.findAll('.grid-2 section.card')[1];

const sourceRow = (wrapper: AccessWrapper) => {
  const text = eventCard(wrapper).text();
  const match = /Source\s*(.*?)\s*User/.exec(text);
  return match ? match[1] : null;
};

const nextPoll = async () => {
  await vi.advanceTimersByTimeAsync(STATUS_POLL_MS);
  await flushPromises();
};

const text = (element: Element | null | undefined) => element?.textContent?.trim() ?? null;

const buttonIn = (root: ParentNode | null | undefined, label: string) =>
  Array.from(root?.querySelectorAll('button') ?? []).find((button) => text(button) === label);

// The enrollment dialog's content, present only while the dialog is open.
const enrollState = () => document.body.querySelector<HTMLElement>('.enrollDialog-state');
const enrollModal = () => enrollState()?.closest('dialog');
const enrollTitle = () => text(enrollModal()?.querySelector('.modal-title'));
const enrollPrompt = () => text(enrollState()?.querySelector('.enrollDialog-prompt'));
const enrollReason = () => text(enrollState()?.querySelector('.enrollDialog-reason'));
const enrollStepLabel = () => text(enrollState()?.querySelector('.enrollDialog-stepLabel'));
const enrollCanvas = () => enrollState()?.querySelector('canvas') ?? null;
const enrollMarkers = () => {
  const markers = Array.from(enrollState()?.querySelectorAll('.enrollDialog-step') ?? []);
  return { total: markers.length, filled: markers.filter((marker) => marker.classList.contains('enrollDialog-stepFilled')).length };
};
const editorForm = () => document.body.querySelector('#user_form');

const openEditorOfAnna = async (wrapper: AccessWrapper) => {
  const edit = wrapper.findAll('button').find((button) => button.text() === 'Edit');
  await edit!.trigger('click');
  await flushPromises();
};

const click = async (button: HTMLButtonElement | undefined) => {
  expect(button).toBeDefined();
  button!.click();
  await flushPromises();
};

beforeEach(() => {
  hasSession.value = true;
  status = makeStatus(null);
  users = [];
  imageRequests = 0;
  enrollRequests = [];
  statusGate = null;
  vi.mocked(api).mockReset().mockImplementation(async (path: string, options?: { json?: unknown }) => {
    if (path === 'fp/status') {
      if (statusGate) {
        await statusGate;
      }
      // A new object on every answer, as from JSON on the wire.
      return structuredClone(status);
    }
    if (path === 'fp/users') {
      return { users: structuredClone(users), max_users: 100, max_fingers: 10 };
    }
    if (path === 'uptime') {
      return { days: 0, hours: 0, minutes: 1, seconds: 0 };
    }
    if (path === 'fp/enroll') {
      const request = options?.json as EnrollRequest;
      enrollRequests.push(request);
      // As the device does: a start resets the enrollment to its first take at once, a cancel ends
      // the wait for the finger.
      if (request.action === 'start') {
        status = { ...status, enroll: makeEnroll('place', { user_id: request.user_id ?? null }) };
      } else {
        status = { ...status, enroll: { ...status.enroll, state: 'cancelled' } };
      }
      return { success: true };
    }
    throw new Error(`unexpected request ${path}`);
  });
  vi.mocked(ky).mockReset().mockImplementation((() => {
    imageRequests++;
    return { arrayBuffer: async () => new Uint8Array(IMAGE_WIDTH * IMAGE_HEIGHT).buffer };
  }) as unknown as typeof ky);
});

afterEach(() => {
  [...mounted].forEach(unmountAccess);
  vi.useRealTimers();
  hasSession.value = false;
});

describe('Access.vue — the last event', () => {
  it('ACC-I-001: names the source, and the Telegram chat the door was opened from', async () => {
    const cases: [Partial<FpEvent>, string][] = [
      [{ source: 'telegram', chat: 'Office', user_id: null, user_name: null }, 'Telegram · Office'],
      [{ source: 'telegram', chat: '555' }, 'Telegram · 555'],
      [{ source: 'telegram', chat: null }, 'Telegram'],
      [{ source: 'mqtt', user_id: null, user_name: null }, 'MQTT'],
      [{ source: 'fingerprint' }, 'Fingerprint'],
      [{ source: 'nfc' as FpEvent['source'] }, 'nfc'],
    ];
    for (const [patch, label] of cases) {
      status = makeStatus(makeEvent(1, patch));
      const wrapper = await mountAccess();
      expect(sourceRow(wrapper), label).toBe(label);
      unmountAccess(wrapper);
    }
  });

  it('ACC-I-002: the pill carries the outcome; an attempt by nobody shows a dash for the user', async () => {
    status = makeStatus(makeEvent(1, { source: 'mqtt', result: 'granted', user_id: null, user_name: null }));
    const wrapper = await mountAccess();

    const pill = eventCard(wrapper).find('.card-header .pill');
    expect(pill.text()).toBe('Granted');
    expect(pill.classes()).toContain('ok');
    expect(eventCard(wrapper).text()).toMatch(/User\s*—/);
  });

  it('ACC-I-003: the image is shown beside its own event and dropped when a later one has none', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    status = makeStatus(makeEvent(7), makeImage(3, 7));
    const wrapper = await mountAccess();

    expect(imageRequests).toBe(1);
    const canvas = eventCard(wrapper).find('canvas');
    expect(canvas.exists()).toBe(true);
    expect(canvas.attributes('width')).toBe(String(IMAGE_WIDTH));
    expect(canvas.attributes('height')).toBe(String(IMAGE_HEIGHT));

    // A Telegram opening: an event with no image; the old image (of event 7) stays on the device.
    status = makeStatus(makeEvent(8, { source: 'telegram', chat: 'Office' }), makeImage(3, 7));
    await nextPoll();
    expect(sourceRow(wrapper)).toBe('Telegram · Office');
    expect(eventCard(wrapper).find('canvas').exists()).toBe(false);
    // The same image is not downloaded again.
    expect(imageRequests).toBe(1);

    // The next scan brings its own image.
    status = makeStatus(makeEvent(9), makeImage(4, 9));
    await nextPoll();
    expect(imageRequests).toBe(2);
    expect(eventCard(wrapper).find('canvas').exists()).toBe(true);
  });

  it('ACC-I-003: an enrollment take is never shown as the image of an event', async () => {
    status = makeStatus(makeEvent(7), makeImage(5, 0));
    const wrapper = await mountAccess();

    expect(eventCard(wrapper).find('canvas').exists()).toBe(false);
  });

  it('ACC-I-004: no event since boot: a note instead of the event, no pill, no image', async () => {
    status = makeStatus(null, makeImage(5, 0));
    const wrapper = await mountAccess();

    expect(eventCard(wrapper).text()).not.toContain('Source');
    expect(eventCard(wrapper).find('.pill').exists()).toBe(false);
    expect(eventCard(wrapper).find('canvas').exists()).toBe(false);
  });
});

describe('Access.vue — the enrollment dialog', () => {
  it('ACC-I-005: "Add finger" opens the dialog over the editor, at the first take at once', async () => {
    users = [ANNA];
    // The previous enrollment ended done: the dialog must not open on that outcome.
    status = makeStatus(null, null, makeEnroll('done', { step: ENROLL_STEPS, slot: 3 }));
    const wrapper = await mountAccess();
    await openEditorOfAnna(wrapper);
    expect(editorForm()).not.toBeNull();
    expect(enrollState()).toBeNull();

    // The device takes its time to answer the poll that follows the start.
    let answer = () => {};
    statusGate = new Promise((resolve) => {
      answer = resolve;
    });
    await click(buttonIn(document.body, 'Add finger'));

    expect(enrollRequests).toEqual([{ action: 'start', user_id: ANNA_ID }]);
    expect(enrollState()).not.toBeNull();
    expect(enrollTitle()).toBe('Adding a finger: Anna');
    expect(enrollPrompt()).toBe('Place your finger');
    expect(enrollStepLabel()).toBe('Step 1 of 5');
    // The editor stays open under the dialog.
    expect(editorForm()).not.toBeNull();

    statusGate = null;
    answer();
    await flushPromises();
    expect(enrollPrompt()).toBe('Place your finger');
  });

  it('ACC-I-006: an enrollment started elsewhere opens the dialog alone, not the editor', async () => {
    users = [ANNA];
    status = makeStatus(null, null, makeEnroll('lift', { step: 3 }));
    await mountAccess();

    expect(enrollState()).not.toBeNull();
    expect(enrollTitle()).toBe('Adding a finger: Anna');
    expect(enrollPrompt()).toBe('Lift your finger');
    expect(editorForm()).toBeNull();
  });

  it('ACC-I-006: an enrollment of a user not listed yet names nobody in the title', async () => {
    status = makeStatus(null, null, makeEnroll('place'));
    await mountAccess();

    expect(enrollTitle()).toBe('Adding a finger: —');
  });

  it('ACC-I-007: prompt, state class, picture and progress follow the enrollment', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    users = [ANNA];
    status = makeStatus(null, null, makeEnroll('place'));
    await mountAccess();

    const cases: [FpStatus['enroll'], string, string, number, string | null][] = [
      [makeEnroll('place', { step: 2 }), 'Place your finger', 'enrollDialog-statePlace', 2, 'Step 2 of 5'],
      [makeEnroll('processing', { step: 2 }), 'Processing…', 'enrollDialog-stateProcessing',
        2, 'Step 2 of 5'],
      [makeEnroll('lift', { step: 2 }), 'Lift your finger', 'enrollDialog-stateLift', 2, 'Step 2 of 5'],
      [makeEnroll('done', { step: 5, slot: 7 }), 'Finger added to slot 7', 'enrollDialog-stateDone', 5, null],
      [makeEnroll('error', { step: 3, error: 'timeout' }), 'Enrollment failed', 'enrollDialog-stateError', 3, null],
      [makeEnroll('cancelled', { step: 1 }), 'Enrollment cancelled', 'enrollDialog-stateCancelled', 1, null],
      [makeEnroll('calibrating' as FpEnrollState, { step: 0 }), 'calibrating', 'enrollDialog-stateIdle', 0, null],
    ];
    for (const [enroll, prompt, stateClass, filled, stepLabel] of cases) {
      status = makeStatus(null, null, enroll);
      await nextPoll();
      expect(enrollPrompt(), enroll.state).toBe(prompt);
      expect(enrollState()!.classList.contains(stateClass), enroll.state).toBe(true);
      expect(enrollMarkers(), enroll.state).toEqual({ total: ENROLL_STEPS, filled });
      expect(enrollStepLabel(), enroll.state).toBe(stepLabel);
      // The picture's ring is the element the state class colours.
      expect(enrollState()!.querySelector('.enrollDialog-art .enrollArt-ring'), enroll.state).not.toBeNull();
    }
  });

  it('ACC-I-007: a failed enrollment names the reason under the prompt', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    status = makeStatus(null, null, makeEnroll('place'));
    await mountAccess();

    status = makeStatus(null, null, makeEnroll('error', { error: 'timeout' }));
    await nextPoll();
    expect(enrollReason()).toBe('no finger was placed on the sensor in time');

    // A reason the page has no translation for is shown as the device sent it.
    status = makeStatus(null, null, makeEnroll('error', { error: 'RegModel: the takes do not match' }));
    await nextPoll();
    expect(enrollReason()).toBe('RegModel: the takes do not match');
  });

  it('ACC-I-008: no older image during the enrollment; its 5th take is shown once uploaded', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    users = [ANNA];
    // The device still holds the last take of an earlier enrollment.
    status = makeStatus(null, makeImage(5, 0));
    const wrapper = await mountAccess();
    expect(imageRequests).toBe(1);

    await openEditorOfAnna(wrapper);
    await click(buttonIn(document.body, 'Add finger'));
    expect(enrollState()).not.toBeNull();
    expect(enrollCanvas()).toBeNull();

    for (const state of ['processing', 'lift', 'place'] as FpEnrollState[]) {
      status = makeStatus(null, makeImage(5, 0), makeEnroll(state, { step: 4 }));
      await nextPoll();
      expect(enrollCanvas(), state).toBeNull();
    }

    // The 5th take is uploaded while it is processed.
    status = makeStatus(null, makeImage(6, 0), makeEnroll('lift', { step: 5 }));
    await nextPoll();
    expect(imageRequests).toBe(2);
    expect(enrollCanvas()).not.toBeNull();

    status = makeStatus(null, makeImage(6, 0), makeEnroll('done', { step: 5, slot: 4 }));
    await nextPoll();
    expect(enrollPrompt()).toBe('Finger added to slot 4');
    const canvas = enrollCanvas();
    expect(canvas).not.toBeNull();
    expect(canvas!.getAttribute('width')).toBe(String(IMAGE_WIDTH));
    expect(canvas!.getAttribute('aria-label')).toBe('Fingerprint taken during this enrollment');

    // The next enrollment, started elsewhere while the dialog is still open: the take just shown
    // belongs to the one before.
    status = makeStatus(null, makeImage(6, 0), makeEnroll('place'));
    await nextPoll();
    expect(enrollPrompt()).toBe('Place your finger');
    expect(enrollCanvas()).toBeNull();
  });

  it('ACC-I-008: an enrollment following another between two polls does not show its take', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    users = [ANNA];
    status = makeStatus(null, makeImage(5, 0));
    const wrapper = await mountAccess();
    await openEditorOfAnna(wrapper);
    await click(buttonIn(document.body, 'Add finger'));

    status = makeStatus(null, makeImage(6, 0), makeEnroll('lift', { step: 5 }));
    await nextPoll();
    expect(enrollCanvas()).not.toBeNull();

    // The next enrollment is already at its first take: the page never saw the first one end.
    status = makeStatus(null, makeImage(6, 0), makeEnroll('place', { step: 1 }));
    await nextPoll();
    expect(enrollCanvas()).toBeNull();

    // Its own last take is not uploaded yet: the one on the device is still the earlier one.
    status = makeStatus(null, makeImage(6, 0), makeEnroll('place', { step: 5 }));
    await nextPoll();
    expect(enrollCanvas()).toBeNull();

    status = makeStatus(null, makeImage(7, 0), makeEnroll('lift', { step: 5 }));
    await nextPoll();
    expect(enrollCanvas()).not.toBeNull();

    // Someone else's enrollment already past its last take, still before its own upload.
    status = makeStatus(null, makeImage(7, 0), makeEnroll('processing', { user_id: ANNA_ID + 1, step: 5 }));
    await nextPoll();
    expect(enrollCanvas()).toBeNull();
  });

  it('ACC-I-008: an enrollment found running does not show the take the device held before', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    status = makeStatus(null, makeImage(5, 0), makeEnroll('place', { step: 2 }));
    const wrapper = await mountAccess();
    expect(imageRequests).toBe(1);
    expect(enrollCanvas()).toBeNull();

    status = makeStatus(null, makeImage(6, 0), makeEnroll('done', { step: 5, slot: 4 }));
    await nextPoll();
    expect(enrollCanvas()).not.toBeNull();

    // A scan at the door while the dialog still shows the outcome: its image replaces the take on
    // the device, and it belongs to the event, not to the enrollment.
    status = makeStatus(makeEvent(9), makeImage(7, 9), makeEnroll('done', { step: 5, slot: 4 }));
    await nextPoll();
    expect(imageRequests).toBe(3);
    expect(enrollCanvas()).toBeNull();
    expect(eventCard(wrapper).find('canvas').exists()).toBe(true);
  });

  it('ACC-I-009: Cancel while the enrollment runs, Close once it is over', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    users = [ANNA];
    status = makeStatus(null, null, makeEnroll('place', { step: 2 }));
    await mountAccess();

    expect(buttonIn(enrollModal(), 'Close')).toBeUndefined();
    // Esc asks the dialog to close: a running enrollment keeps it open.
    enrollModal()!.dispatchEvent(new Event('cancel', { cancelable: true }));
    await flushPromises();
    expect(enrollState()).not.toBeNull();

    await click(buttonIn(enrollModal(), 'Cancel'));
    expect(enrollRequests).toEqual([{ action: 'cancel' }]);
    await nextPoll();
    expect(enrollPrompt()).toBe('Enrollment cancelled');
    expect(buttonIn(enrollModal(), 'Cancel')).toBeUndefined();

    await click(buttonIn(enrollModal(), 'Close'));
    expect(enrollState()).toBeNull();
    expect(editorForm()).toBeNull();
  });
});
