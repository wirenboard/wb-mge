/**
 * Unit tests for the mixer program codec — the 4-byte step format the firmware stores in
 * mix_p0..mix_p3.
 *
 * MP-001 — the byte layout itself: [type][speed/10][duration hi][duration lo], big-endian.
 * MP-002 — encode/decode round trip for all three step types, pause speed normalized to zero.
 * MP-003 — an empty program is the empty string in both directions.
 * MP-004 — 11 steps are accepted, a 12th is refused in both directions.
 * MP-005 — corrupt base64: illegal characters, bad padding, a length that is not whole steps,
 *           an unknown type byte, a zero duration.
 * MP-006 — speed boundaries: 0, the 2550 rpm ceiling, and what falls outside the byte.
 * MP-007 — duration boundaries: 1 s, 65535 s, and what falls outside the 16-bit field.
 * MP-008 — the editor helpers: minutes/seconds split, clamping onto the storable grid.
 */

import { describe, expect, it } from 'vitest';
import {
  MIXER_MAX_DURATION_S,
  MIXER_MAX_SPEED_RPM,
  MIXER_MAX_STEPS,
  MIXER_MIN_MOTION_SPEED_RPM,
  MIXER_STEP_FORWARD,
  MIXER_STEP_PAUSE,
  MIXER_STEP_REVERSE,
  MixerProgramError,
  type MixerStep,
  clampDuration,
  clampSpeed,
  clampStepSpeed,
  combineDuration,
  decodeProgram,
  effectiveFrequencyDhz,
  effectiveSpeedRpm,
  encodeProgram,
  rpmToTens,
  splitDuration,
  tensToRpm,
} from './mixerProgram';

const step = (type: MixerStep['type'], speed: number, duration: number): MixerStep =>
  ({ type, speed, duration });

const bytesOf = (encoded: string): number[] => Array.from(atob(encoded), (char) => char.charCodeAt(0));

describe('wire format', () => {
  it('MP-001: one step is [type][speed/10][duration hi][duration lo]', () => {
    // Forward, 200 rpm, one hour: 3600 s is 0x0E10.
    const encoded = encodeProgram([step(MIXER_STEP_FORWARD, 200, 3600)]);
    expect(bytesOf(encoded)).toEqual([0x01, 20, 0x0e, 0x10]);
    expect(encoded).toBe('ARQOEA==');
  });

  it('MP-001: steps are concatenated in order, four bytes each', () => {
    const encoded = encodeProgram([
      step(MIXER_STEP_FORWARD, 100, 1),
      step(MIXER_STEP_REVERSE, 2550, MIXER_MAX_DURATION_S),
    ]);
    expect(bytesOf(encoded)).toEqual([0x01, 10, 0x00, 0x01, 0x02, 255, 0xff, 0xff]);
  });
});

describe('round trip', () => {
  it('MP-002: every step type survives encode → decode', () => {
    const program = [
      step(MIXER_STEP_FORWARD, 600, 120),
      step(MIXER_STEP_PAUSE, 0, 30),
      step(MIXER_STEP_REVERSE, 250, 3600),
    ];
    expect(decodeProgram(encodeProgram(program))).toEqual(program);
  });

  it('MP-002: a pause keeps no speed — the firmware never reads that byte', () => {
    const encoded = encodeProgram([step(MIXER_STEP_PAUSE, 900, 60)]);
    expect(bytesOf(encoded)[1]).toBe(0);
    expect(decodeProgram(encoded)).toEqual([step(MIXER_STEP_PAUSE, 0, 60)]);
  });

  it('MP-003: an empty program is the empty string both ways', () => {
    expect(encodeProgram([])).toBe('');
    expect(decodeProgram('')).toEqual([]);
  });
});

describe('step count', () => {
  const eleven = Array.from({ length: MIXER_MAX_STEPS }, (_, index) =>
    step(MIXER_STEP_FORWARD, (index + 1) * 10, index + 1));

  it('MP-004: eleven steps fit', () => {
    const encoded = encodeProgram(eleven);
    expect(atob(encoded)).toHaveLength(MIXER_MAX_STEPS * 4);
    expect(decodeProgram(encoded)).toEqual(eleven);
  });

  it('MP-004: a twelfth step is refused', () => {
    expect(() => encodeProgram([...eleven, step(MIXER_STEP_PAUSE, 0, 5)])).toThrow(MixerProgramError);
  });

  it('MP-004: a stored program with twelve steps is refused too', () => {
    // Built byte by byte: the firmware cannot produce this, but a corrupted NVS record can.
    const twelve = btoa(String.fromCharCode(...new Array(12 * 4).fill(0).map((_, index) => (index % 4 === 3 ? 1 : 0))));
    expect(() => decodeProgram(twelve)).toThrow(MixerProgramError);
  });
});

describe('corrupt input', () => {
  it('MP-005: characters outside the base64 alphabet', () => {
    expect(() => decodeProgram('ARQ*EA==')).toThrow(MixerProgramError);
    expect(() => decodeProgram('not base64!')).toThrow(MixerProgramError);
  });

  it('MP-005: a length that is not a whole base64 quantum', () => {
    expect(() => decodeProgram('ARQOEA=')).toThrow(MixerProgramError);
    expect(() => decodeProgram('ARQOE')).toThrow(MixerProgramError);
  });

  it('MP-005: valid base64 that is not a whole number of steps', () => {
    expect(() => decodeProgram(btoa('\x01\x14\x0e'))).toThrow(MixerProgramError);
    expect(() => decodeProgram(btoa('\x01\x14\x0e\x10\x01\x14'))).toThrow(MixerProgramError);
  });

  it('MP-005: an unknown step type', () => {
    expect(() => decodeProgram(btoa('\x03\x14\x00\x3c'))).toThrow(MixerProgramError);
  });

  it('MP-005: a zero duration, which no step may have', () => {
    expect(() => decodeProgram(btoa('\x01\x14\x00\x00'))).toThrow(MixerProgramError);
  });
});

describe('speed limits', () => {
  it('MP-006: one grid step and the 2550 rpm ceiling are storable', () => {
    expect(decodeProgram(encodeProgram([step(MIXER_STEP_FORWARD, MIXER_MIN_MOTION_SPEED_RPM, 10)]))[0].speed)
      .toBe(MIXER_MIN_MOTION_SPEED_RPM);
    expect(decodeProgram(encodeProgram([step(MIXER_STEP_FORWARD, MIXER_MAX_SPEED_RPM, 10)]))[0].speed)
      .toBe(MIXER_MAX_SPEED_RPM);
  });

  it('MP-006: above the ceiling, below zero and off the 10 rpm grid are refused', () => {
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, MIXER_MAX_SPEED_RPM + 10, 10)])).toThrow(MixerProgramError);
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, -10, 10)])).toThrow(MixerProgramError);
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, 255, 10)])).toThrow(MixerProgramError);
  });

  // The firmware raises any speed it cannot reach to the drive's 1.0 Hz floor, so "forward at
  // 0 rpm" would turn the shaft. Standing still is what a pause step is for.
  it('MP-006: a step that moves the shaft may not ask for zero rpm', () => {
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, 0, 10)])).toThrow(MixerProgramError);
    expect(() => encodeProgram([step(MIXER_STEP_REVERSE, 0, 10)])).toThrow(MixerProgramError);
    // 01 00 00 0A - what an older firmware could still have stored
    expect(() => decodeProgram(btoa('\x01\x00\x00\x0a'))).toThrow(MixerProgramError);
    expect(() => decodeProgram(btoa('\x02\x00\x00\x0a'))).toThrow(MixerProgramError);
    // A pause keeps its zero, and keeps decoding
    expect(decodeProgram(encodeProgram([step(MIXER_STEP_PAUSE, 0, 10)]))).toEqual([step(MIXER_STEP_PAUSE, 0, 10)]);
  });
});

describe('what the drive will actually run', () => {
  // rpm -> 0.1 Hz is main/mixer/mixer.c's own arithmetic: rpm * 500 / mix_rpm_50hz, truncated,
  // then clamped into the 1.0 .. 99.0 Hz the drive accepts.
  it('MP-009: a speed becomes a frequency the same way the firmware computes it', () => {
    expect(effectiveFrequencyDhz(200, 1500)).toBe(66); // 6.6 Hz
    expect(effectiveFrequencyDhz(1500, 1500)).toBe(500); // 50.0 Hz exactly
    expect(effectiveSpeedRpm(200, 1500)).toBe(198); // 6.6 Hz is 198 rpm, not the 200 asked for
    expect(effectiveSpeedRpm(1500, 1500)).toBe(1500);
  });

  it('MP-009: below the drive floor and above its ceiling the speed is clamped', () => {
    expect(effectiveFrequencyDhz(10, 1500)).toBe(10); // 0.3 Hz would be below 1.0
    expect(effectiveSpeedRpm(10, 1500)).toBe(30); // so the shaft turns at 30 rpm, not 10
    expect(effectiveFrequencyDhz(2550, 1000)).toBe(990); // 127.5 Hz would be above 99.0
    expect(effectiveSpeedRpm(2550, 1000)).toBe(1980);
  });

  it('MP-009: a nonsensical ratio cannot produce a nonsensical frequency', () => {
    expect(effectiveFrequencyDhz(200, 0)).toBe(10);
    expect(effectiveFrequencyDhz(Number.NaN, 1500)).toBe(10);
  });
});

describe('duration limits', () => {
  it('MP-007: one second and 65535 seconds are storable', () => {
    expect(decodeProgram(encodeProgram([step(MIXER_STEP_FORWARD, 100, 1)]))[0].duration).toBe(1);
    expect(decodeProgram(encodeProgram([step(MIXER_STEP_FORWARD, 100, MIXER_MAX_DURATION_S)]))[0].duration)
      .toBe(MIXER_MAX_DURATION_S);
  });

  it('MP-007: zero, an overflow and a fraction are refused', () => {
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, 100, 0)])).toThrow(MixerProgramError);
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, 100, MIXER_MAX_DURATION_S + 1)])).toThrow(MixerProgramError);
    expect(() => encodeProgram([step(MIXER_STEP_FORWARD, 100, 1.5)])).toThrow(MixerProgramError);
  });
});

describe('editor helpers', () => {
  it('MP-008: rpm and the tens the byte holds convert both ways', () => {
    expect(rpmToTens(2550)).toBe(255);
    expect(tensToRpm(255)).toBe(2550);
  });

  it('MP-008: a duration splits into minutes and seconds and back', () => {
    expect(splitDuration(3661)).toEqual({ minutes: 61, seconds: 1 });
    expect(splitDuration(45)).toEqual({ minutes: 0, seconds: 45 });
    expect(combineDuration(61, 1)).toBe(3661);
  });

  it('MP-008: an empty duration cannot go below one second or above the field', () => {
    expect(combineDuration(0, 0)).toBe(1);
    expect(combineDuration(2000, 0)).toBe(MIXER_MAX_DURATION_S);
    expect(clampDuration(Number.NaN)).toBe(1);
  });

  it('MP-008: a typed speed is snapped onto the storable grid', () => {
    expect(clampSpeed(255)).toBe(260);
    expect(clampSpeed(9999)).toBe(MIXER_MAX_SPEED_RPM);
    expect(clampSpeed(-5)).toBe(0);
    expect(clampSpeed(Number.NaN)).toBe(0);
  });

  // The same snapping, but a step that moves the shaft lands on the grid ABOVE zero: what the
  // editor binds must never become a program the encoder would refuse.
  it('MP-008: a step that moves the shaft cannot be snapped down to a standstill', () => {
    expect(clampStepSpeed(MIXER_STEP_FORWARD, 0)).toBe(MIXER_MIN_MOTION_SPEED_RPM);
    expect(clampStepSpeed(MIXER_STEP_REVERSE, 4)).toBe(MIXER_MIN_MOTION_SPEED_RPM);
    expect(clampStepSpeed(MIXER_STEP_FORWARD, 255)).toBe(260);
    expect(clampStepSpeed(MIXER_STEP_PAUSE, 900)).toBe(0);
  });
});
