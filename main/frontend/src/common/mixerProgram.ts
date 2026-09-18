// A mixer program on the wire is an array of fixed 4-byte steps — [type u8][speed u8]
// [duration u16 big-endian] — concatenated and base64-encoded. That string is exactly what
// GET/POST /settings carries in mix_p0..mix_p3, so everything this module refuses is something
// the firmware could not store anyway.

export const MIXER_STEP_BYTES = 4;
export const MIXER_MAX_STEPS = 11;

// The speed byte counts TENS of rpm. Both the 10 rpm granularity and the 2550 rpm ceiling come
// from that single byte, not from a UI preference.
export const MIXER_SPEED_STEP_RPM = 10;
export const MIXER_MAX_SPEED_RPM = 255 * MIXER_SPEED_STEP_RPM;

// The slowest a step that moves the shaft may ask for. Zero is not a way to stand still: the
// firmware converts the speed into a drive frequency and raises whatever comes out to the
// converter's 1.0 Hz floor, so a "forward, 0 rpm" step turns the motor instead of holding it.
// A pause step is what standing still is for, and it is the only one whose speed may be zero.
export const MIXER_MIN_MOTION_SPEED_RPM = MIXER_SPEED_STEP_RPM;

export const MIXER_MIN_DURATION_S = 1;
export const MIXER_MAX_DURATION_S = 65535;

// The drive's own frequency limits, in 0.1 Hz, and the frequency 50 Hz stands for. The editor
// needs all three to show what a typed speed actually becomes (see effectiveFrequencyDhz).
export const MIXER_VFD_MIN_DHZ = 10;
export const MIXER_VFD_MAX_DHZ = 990;
export const MIXER_DHZ_AT_50HZ = 500;

export const MIXER_STEP_PAUSE = 0;
export const MIXER_STEP_FORWARD = 1;
export const MIXER_STEP_REVERSE = 2;

export type MixerStepType =
  | typeof MIXER_STEP_PAUSE
  | typeof MIXER_STEP_FORWARD
  | typeof MIXER_STEP_REVERSE;

export interface MixerStep {
  type: MixerStepType;
  speed: number; // rpm, a multiple of MIXER_SPEED_STEP_RPM; ignored by a pause step
  duration: number; // seconds
}

export class MixerProgramError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'MixerProgramError';
  }
}

const STEP_TYPES: MixerStepType[] = [MIXER_STEP_PAUSE, MIXER_STEP_FORWARD, MIXER_STEP_REVERSE];

// Padding is allowed only at the very end, and only as one or two characters: `atob` accepts
// several strings a base64 encoder never produces, and a program that does not round-trip is a
// corrupted program, not a tolerable one.
const BASE64_RE = /^[A-Za-z0-9+/]+={0,2}$/;

export const isMixerStepType = (value: unknown): value is MixerStepType =>
  STEP_TYPES.includes(value as MixerStepType);

export const tensToRpm = (tens: number): number => tens * MIXER_SPEED_STEP_RPM;

export const rpmToTens = (rpm: number): number => Math.round(rpm / MIXER_SPEED_STEP_RPM);

// Snaps an edited value onto the grid the speed byte can hold, so the editor can bind a plain
// number input and still never build a program the encoder would refuse.
export const clampSpeed = (rpm: number): number => {
  if (!Number.isFinite(rpm)) return 0;
  const snapped = Math.round(rpm / MIXER_SPEED_STEP_RPM) * MIXER_SPEED_STEP_RPM;
  return Math.min(Math.max(snapped, 0), MIXER_MAX_SPEED_RPM);
};

// The same snapping, but aware of what the step does: a step that moves the shaft may not ask
// for no speed at all, so the grid it lands on starts at one step rather than at zero.
export const clampStepSpeed = (type: MixerStepType, rpm: number): number => {
  if (type === MIXER_STEP_PAUSE) return 0;
  return Math.max(clampSpeed(rpm), MIXER_MIN_MOTION_SPEED_RPM);
};

// What the firmware will actually run a step at: rpm -> the drive's 0.1 Hz setpoint, exactly as
// main/mixer/mixer.c computes it (integer division, then clamped into what the drive accepts).
// The editor shows this next to the typed speed, so that a speed the drive cannot reach is seen
// rather than silently raised to its floor.
export const effectiveFrequencyDhz = (rpm: number, rpm50hz: number): number => {
  if (!Number.isFinite(rpm) || !Number.isFinite(rpm50hz) || rpm50hz < 1) return MIXER_VFD_MIN_DHZ;
  const dhz = Math.floor((Math.max(rpm, 0) * MIXER_DHZ_AT_50HZ) / rpm50hz);
  return Math.min(Math.max(dhz, MIXER_VFD_MIN_DHZ), MIXER_VFD_MAX_DHZ);
};

// And back: the shaft speed that frequency gives, which is what the mixer will really turn at.
export const effectiveSpeedRpm = (rpm: number, rpm50hz: number): number =>
  Math.round((effectiveFrequencyDhz(rpm, rpm50hz) * rpm50hz) / MIXER_DHZ_AT_50HZ);

export const clampDuration = (seconds: number): number => {
  if (!Number.isFinite(seconds)) return MIXER_MIN_DURATION_S;
  return Math.min(Math.max(Math.round(seconds), MIXER_MIN_DURATION_S), MIXER_MAX_DURATION_S);
};

// A duration is stored as one number of seconds but entered as minutes plus seconds.
export const splitDuration = (seconds: number): { minutes: number; seconds: number } => {
  const total = clampDuration(seconds);
  return { minutes: Math.floor(total / 60), seconds: total % 60 };
};

export const combineDuration = (minutes: number, seconds: number): number => {
  const wholeMinutes = Number.isFinite(minutes) ? Math.max(Math.round(minutes), 0) : 0;
  const wholeSeconds = Number.isFinite(seconds) ? Math.max(Math.round(seconds), 0) : 0;
  return clampDuration(wholeMinutes * 60 + wholeSeconds);
};

const bytesToBase64 = (bytes: Uint8Array): string => {
  let binary = '';
  bytes.forEach((byte) => {
    binary += String.fromCharCode(byte);
  });
  return btoa(binary);
};

export const encodeProgram = (steps: MixerStep[]): string => {
  if (steps.length > MIXER_MAX_STEPS) {
    throw new MixerProgramError(`program has ${steps.length} steps, at most ${MIXER_MAX_STEPS} fit`);
  }

  const bytes = new Uint8Array(steps.length * MIXER_STEP_BYTES);

  steps.forEach((step, index) => {
    if (!isMixerStepType(step.type)) {
      throw new MixerProgramError(`step ${index}: unknown type ${step.type}`);
    }
    // The motor stands still during a pause, so the firmware never reads the speed byte of one.
    // Writing a zero there gives every program a single canonical encoding, which is what lets
    // the editor compare an edited program with the saved one as plain strings.
    const speed = step.type === MIXER_STEP_PAUSE ? 0 : step.speed;
    if (!Number.isInteger(speed) || speed < 0 || speed > MIXER_MAX_SPEED_RPM) {
      throw new MixerProgramError(`step ${index}: speed ${step.speed} is outside 0..${MIXER_MAX_SPEED_RPM} rpm`);
    }
    if (speed % MIXER_SPEED_STEP_RPM !== 0) {
      throw new MixerProgramError(`step ${index}: speed ${step.speed} is not a multiple of ${MIXER_SPEED_STEP_RPM} rpm`);
    }
    if (step.type !== MIXER_STEP_PAUSE && speed < MIXER_MIN_MOTION_SPEED_RPM) {
      throw new MixerProgramError(`step ${index}: a step that moves the shaft needs at least ${MIXER_MIN_MOTION_SPEED_RPM} rpm`);
    }
    if (!Number.isInteger(step.duration) || step.duration < MIXER_MIN_DURATION_S || step.duration > MIXER_MAX_DURATION_S) {
      throw new MixerProgramError(`step ${index}: duration ${step.duration} is outside ${MIXER_MIN_DURATION_S}..${MIXER_MAX_DURATION_S} s`);
    }

    const offset = index * MIXER_STEP_BYTES;
    bytes[offset] = step.type;
    bytes[offset + 1] = rpmToTens(speed);
    bytes[offset + 2] = (step.duration >> 8) & 0xff;
    bytes[offset + 3] = step.duration & 0xff;
  });

  return bytesToBase64(bytes);
};

export const decodeProgram = (encoded: string): MixerStep[] => {
  if (encoded === '') return [];

  if (!BASE64_RE.test(encoded) || encoded.length % 4 !== 0) {
    throw new MixerProgramError('program is not valid base64');
  }

  let binary: string;
  try {
    binary = atob(encoded);
  } catch (err) {
    throw new MixerProgramError('program is not valid base64');
  }

  if (binary.length % MIXER_STEP_BYTES !== 0) {
    throw new MixerProgramError(`program is ${binary.length} bytes, not a whole number of ${MIXER_STEP_BYTES}-byte steps`);
  }

  const count = binary.length / MIXER_STEP_BYTES;
  if (count > MIXER_MAX_STEPS) {
    throw new MixerProgramError(`program has ${count} steps, at most ${MIXER_MAX_STEPS} fit`);
  }

  const steps: MixerStep[] = [];
  for (let index = 0; index < count; index += 1) {
    const offset = index * MIXER_STEP_BYTES;
    const type = binary.charCodeAt(offset);
    if (!isMixerStepType(type)) {
      throw new MixerProgramError(`step ${index}: unknown type ${type}`);
    }
    const speed = type === MIXER_STEP_PAUSE ? 0 : tensToRpm(binary.charCodeAt(offset + 1));
    const duration = (binary.charCodeAt(offset + 2) << 8) | binary.charCodeAt(offset + 3);
    if (duration < MIXER_MIN_DURATION_S) {
      throw new MixerProgramError(`step ${index}: duration ${duration} is below ${MIXER_MIN_DURATION_S} s`);
    }
    // The firmware refuses to store this, so a stored program carrying it is a corrupted one -
    // and showing it as such is better than editing a step the device would not run.
    if (type !== MIXER_STEP_PAUSE && speed < MIXER_MIN_MOTION_SPEED_RPM) {
      throw new MixerProgramError(`step ${index}: a step that moves the shaft needs at least ${MIXER_MIN_MOTION_SPEED_RPM} rpm`);
    }
    steps.push({ type, speed, duration });
  }

  return steps;
};
