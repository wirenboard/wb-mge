/**
 * Unit tests for the release-manifest parser.
 *
 * The fixture is a trimmed copy of the live
 * https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml (taken 03.08.2026):
 * the `releases:` header plus four real signature blocks — co2_sens_ns8 (a non-semver .compfw
 * version), mdm3_26 (.wbfw), mge_v3 (ours) and mio (the block right after ours).
 *
 * FWC-001 — the mge_v3 block is read out of the real manifest, keys and values intact.
 * FWC-002 — parsing stops at the end of our block: a later duplicate block is not read.
 * FWC-003 — a signature that is not in the manifest yields null, not an error.
 * FWC-004 — an empty / header-only / truncated manifest throws ManifestError.
 * FWC-005 — a malformed line inside a FOREIGN block does not break our parse.
 * FWC-006 — quotes, anchors and flow syntax inside OUR block throw.
 * FWC-007 — the block boundary is the indentation, so a corrupted neighbouring signature line
 *            can never append that neighbour's pairs to ours.
 * FWC-008 — version parsing: .bin only, the Makefile grammar including +wbN / -rcN suffixes.
 * FWC-009 — describeChannels: both channels at once, a missing/unreadable key is null.
 * FWC-010 — resolveRelease: ok / no-signature / unavailable, each with the channels view alongside.
 */

import { describe, expect, it } from 'vitest';
import manifest from './__fixtures__/release-versions.sample.yaml?raw';
import {
  FW_RELEASES_BASE,
  ManifestError,
  describeChannels,
  parseSignatureBlock,
  parseVersionFromPath,
  resolveRelease,
} from './firmwareChannels';

describe('parseSignatureBlock — live manifest fixture', () => {
  it('FWC-001: reads our block out of the real manifest', () => {
    const block = parseSignatureBlock(manifest, 'mge_v3');
    expect(block).not.toBeNull();
    expect(block!.stable).toBe('fw/by-signature/mge_v3/main/1.1.0.bin');
    expect(block!.testing).toBe('fw/by-signature/mge_v3/main/1.1.1.bin');
    // Keys of neighbouring signatures must not be present.
    expect(Object.values(block!).every((path) => path.includes('/mge_v3/'))).toBe(true);
  });

  it('FWC-001: reads a block that is not the first one and not the last one', () => {
    const block = parseSignatureBlock(manifest, 'co2_sens_ns8');
    expect(block!.stable).toBe('fw/by-signature/co2_sens_ns8/main/MF1.03D.compfw');
  });

  it('FWC-002: stops at the end of our block and ignores anything after it', () => {
    // A second mge_v3 block further down the file (which a broken generator could emit) must not be
    // merged into the first one: the parse is already finished by then.
    const withDuplicate = `${manifest}  mge_v3:\n    stable: fw/by-signature/mge_v3/main/9.9.9.bin\n`;
    const block = parseSignatureBlock(withDuplicate, 'mge_v3');
    expect(block!.stable).toBe('fw/by-signature/mge_v3/main/1.1.0.bin');
  });

  it('FWC-003: an absent signature is null, not an error', () => {
    expect(parseSignatureBlock(manifest, 'mgu_v1')).toBeNull();
  });
});

describe('parseSignatureBlock — broken manifests', () => {
  it('FWC-004: an empty body throws', () => {
    expect(() => parseSignatureBlock('', 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-004: a body without the releases: header throws', () => {
    expect(() => parseSignatureBlock('some: thing\nother: value\n', 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-004: a body truncated right after releases: throws instead of reading as no-signature', () => {
    expect(() => parseSignatureBlock('releases:\n', 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-004: a truncated body is distinguishable from an intact one without our signature', () => {
    const intact = 'releases:\n  mio:\n    stable: fw/by-signature/mio/main/1.8.4.wbfw\n';
    expect(parseSignatureBlock(intact, 'mge_v3')).toBeNull();
  });

  it('FWC-005: a malformed line in a foreign block is skipped', () => {
    const text = manifest.replace(
      '    stable: fw/by-signature/mdm3_26/main/2.6.6.wbfw',
      '    stable "fw/by-signature/mdm3_26/main/2.6.6.wbfw',
    );
    const block = parseSignatureBlock(text, 'mge_v3');
    expect(block!.stable).toBe('fw/by-signature/mge_v3/main/1.1.0.bin');
  });

  it('FWC-006: a quoted value inside our block throws', () => {
    const text = manifest.replace(
      '    stable: fw/by-signature/mge_v3/main/1.1.0.bin',
      '    stable: "fw/by-signature/mge_v3/main/1.1.0.bin"',
    );
    expect(() => parseSignatureBlock(text, 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-006: an anchor inside our block throws', () => {
    const text = manifest.replace(
      '    stable: fw/by-signature/mge_v3/main/1.1.0.bin',
      '    stable: &mge_stable',
    );
    expect(() => parseSignatureBlock(text, 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-006: a line with no value inside our block throws', () => {
    const text = manifest.replace(
      '    stable: fw/by-signature/mge_v3/main/1.1.0.bin',
      '    stable:',
    );
    expect(() => parseSignatureBlock(text, 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-007: a trailing space on the NEXT signature line still closes our block', () => {
    // The corrupted line does not match the signature pattern; only its indentation matters.
    const text = manifest.replace('  mio:', '  mio: ');
    const block = parseSignatureBlock(text, 'mge_v3');
    expect(Object.values(block!).every((path) => path.includes('/mge_v3/'))).toBe(true);
  });

  it('FWC-007: a three-space indent on the next signature line fails loudly, it does not leak', () => {
    // With a boundary decided by "does this line look like a signature", the mio pairs would be
    // appended to the mge_v3 block and the UI would offer a foreign device's firmware.
    const text = manifest.replace('  mio:', '   mio:');
    expect(() => parseSignatureBlock(text, 'mge_v3')).toThrow(ManifestError);
  });

  it('FWC-007: a zero-indent line after releases: ends the parse', () => {
    const text = 'releases:\n  mdm3_26:\n    stable: fw/x/1.0.0.wbfw\nother:\n  mge_v3:\n    stable: fw/y/1.0.0.bin\n';
    expect(parseSignatureBlock(text, 'mge_v3')).toBeNull();
  });

  it('FWC-007: everything before releases: is skipped silently', () => {
    const text = `# generated\nsome_new_key: 1\nnested:\n  mge_v3:\n    stable: fw/nope/9.9.9.bin\n${manifest}`;
    const block = parseSignatureBlock(text, 'mge_v3');
    expect(block!.stable).toBe('fw/by-signature/mge_v3/main/1.1.0.bin');
  });

  it('FWC-007: comments and blank lines inside our block are skipped', () => {
    const text = manifest.replace(
      '  mge_v3:\n',
      '  mge_v3:\n    # a comment\n\n',
    );
    expect(parseSignatureBlock(text, 'mge_v3')!.stable).toBe('fw/by-signature/mge_v3/main/1.1.0.bin');
  });

  it('FWC-007: CRLF line endings are tolerated', () => {
    const block = parseSignatureBlock(manifest.replace(/\n/g, '\r\n'), 'mge_v3');
    expect(block!.testing).toBe('fw/by-signature/mge_v3/main/1.1.1.bin');
  });
});

describe('parseVersionFromPath', () => {
  it('FWC-008: reads a plain semver out of a .bin path', () => {
    expect(parseVersionFromPath('fw/by-signature/mge_v3/main/1.1.0.bin')).toBe('1.1.0');
  });

  it('FWC-008: accepts the +wbN and -rcN suffixes the build system produces', () => {
    expect(parseVersionFromPath('fw/by-signature/mge_v3/main/1.0.0-rc29.bin')).toBe('1.0.0-rc29');
    expect(parseVersionFromPath('fw/by-signature/mge_v3/main/1.2.0+wb2.bin')).toBe('1.2.0+wb2');
  });

  it('FWC-008: rejects a non-version name', () => {
    expect(parseVersionFromPath('fw/by-signature/mge_v3/main/latest.bin')).toBeNull();
  });

  it('FWC-008: rejects a file that is not a .bin', () => {
    expect(parseVersionFromPath('fw/by-signature/co2_sens_ns8/main/MF1.03D.compfw')).toBeNull();
    expect(parseVersionFromPath('fw/by-signature/mio/main/1.8.4.wbfw')).toBeNull();
  });
});

describe('describeChannels', () => {
  it('FWC-009: returns both channels from one block', () => {
    const view = describeChannels(parseSignatureBlock(manifest, 'mge_v3')!);
    expect(view.stable).toEqual({
      version: '1.1.0',
      url: `${FW_RELEASES_BASE}fw/by-signature/mge_v3/main/1.1.0.bin`,
    });
    expect(view.testing).toEqual({
      version: '1.1.1',
      url: `${FW_RELEASES_BASE}fw/by-signature/mge_v3/main/1.1.1.bin`,
    });
  });

  it('FWC-009: an unreadable version is null while the other channel keeps working', () => {
    const view = describeChannels({
      stable: 'fw/by-signature/mge_v3/main/1.1.0.bin',
      testing: 'fw/by-signature/co2_sens_ns8/main/MF1.03D.compfw',
    });
    expect(view.stable).not.toBeNull();
    expect(view.testing).toBeNull();
  });

  it('FWC-009: a missing key is null', () => {
    const view = describeChannels({ stable: 'fw/by-signature/mge_v3/main/1.1.0.bin' });
    expect(view.testing).toBeNull();
  });
});

describe('resolveRelease', () => {
  it('FWC-010: resolves the selected channel and reports both channels', () => {
    const stable = resolveRelease(manifest, 'mge_v3', 'stable');
    expect(stable.release).toEqual({
      ok: true,
      version: '1.1.0',
      url: `${FW_RELEASES_BASE}fw/by-signature/mge_v3/main/1.1.0.bin`,
    });
    // The other channel comes out of the same lookup, so the UI needs no second parse.
    expect(stable.channels?.testing?.version).toBe('1.1.1');

    expect(resolveRelease(manifest, 'mge_v3', 'testing').release).toEqual({
      ok: true,
      version: '1.1.1',
      url: `${FW_RELEASES_BASE}fw/by-signature/mge_v3/main/1.1.1.bin`,
    });
  });

  it('FWC-010: a signature absent from the manifest is no-signature with no channels', () => {
    expect(resolveRelease(manifest, 'mgu_v1', 'stable')).toEqual({
      channels: null,
      release: { ok: false, reason: 'no-signature' },
    });
  });

  it('FWC-010: an empty signature is no-signature', () => {
    expect(resolveRelease(manifest, '', 'stable').release).toEqual({ ok: false, reason: 'no-signature' });
  });

  it('FWC-010: an unreadable version in the selected channel is unavailable, the block still resolves', () => {
    const found = resolveRelease(manifest, 'co2_sens_ns8', 'stable');
    expect(found.release).toEqual({ ok: false, reason: 'unavailable' });
    expect(found.channels).not.toBeNull();
  });

  it('FWC-010: a broken manifest throws instead of reporting a missing signature', () => {
    expect(() => resolveRelease('releases:\n', 'mge_v3', 'stable')).toThrow(ManifestError);
  });
});
