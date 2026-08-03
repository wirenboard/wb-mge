import type { UpdateChannel } from '@/common/types';

export const FW_RELEASES_BASE = 'https://fw-releases.wirenboard.com/';
export const MANIFEST_URL = `${FW_RELEASES_BASE}fw/by-signature/release-versions.yaml`;

// Thrown when the manifest itself looks broken or truncated. It is deliberately distinct from
// "our signature is not in the manifest": the first means "the check could not be performed",
// the second means "this board has no published channels yet", and the UI says different things.
export class ManifestError extends Error {}

// One channel of one signature. null means the key is missing, or the version could not be read
// out of the file name (the manifest also carries non-semver names such as MF1.03D.compfw).
export type ChannelInfo =
  | { version: string; url: string }
  | null;

// Both channels at once: they live in the same manifest block, so no extra request is needed.
export type ChannelsView = { stable: ChannelInfo; testing: ChannelInfo };

export type ReleaseLookup =
  | { ok: true; version: string; url: string }
  | { ok: false; reason: 'no-signature' | 'unavailable' };

// Everything one lookup produces: what to offer, plus what both channels hold. The second half is
// what the UI shows next to the offer, and it exists even when the selected channel does not
// resolve — a manifest that carries a broken `testing` still has a `stable` worth showing.
export type ReleaseResolution = {
  channels: ChannelsView | null; // null when the manifest has no block for this signature
  release: ReleaseLookup;
};

// A signature line: exactly two spaces of indent, a bare key, nothing after the colon.
// Live names contain hyphens and dots (m1w2-21, msv2-4.0, ups-v3-dlg2), hence [^\s:]+.
const SIGNATURE_LINE_RE = /^ {2}([^\s:]+):$/;

// A key/value pair inside a signature block: at least four spaces of indent, one bare token as the
// value. Anything else inside OUR block is treated as corruption, not as something to skip.
const PAIR_LINE_RE = /^ {4,}([^\s:]+): +(\S+)$/;

// YAML sigils that would make the value something other than a plain scalar. The manifest contract
// forbids quotes, anchors, aliases and flow/block syntax, so their presence means the file is not
// the flat three-level document this parser is allowed to read.
const FORBIDDEN_VALUE_PREFIXES = ['"', '\'', '{', '[', '&', '*', '|', '>'];

// Version grammar taken from Makefile:92 (x.y.z, x.y.z+wbN, x.y.z-rcN), not from openapi.yaml,
// whose ^\d+\.\d+\.\d+$ no longer describes the versions this project actually releases.
const VERSION_RE = /^\d+\.\d+\.\d+([+-][A-Za-z0-9.]+)?$/;

const FIRMWARE_SUFFIX = '.bin';

const indentOf = (line: string): number => line.length - line.trimStart().length;

/**
 * Parses ONLY the requested signature's block out of the manifest text and stops there.
 *
 * Block boundaries are decided by indentation, never by "the line matches the signature pattern".
 * A corrupted signature line of a neighbouring device (trailing space, three spaces of indent)
 * must not let that device's pairs leak into ours: with a boundary by indentation such a line
 * either closes our block or fails the strict in-block check, and both outcomes are safe. With a
 * boundary by pattern match it would silently append a foreign device's firmware to our block.
 *
 * Returns null when the manifest is intact but carries no such signature.
 * Throws ManifestError when the manifest looks broken or truncated.
 */
export function parseSignatureBlock(text: string, signature: string): Record<string, string> | null {
  const lines = text.split('\n');
  let inReleases = false;
  let sawNestedLine = false;
  let inOurBlock = false;
  let block: Record<string, string> | null = null;

  for (const rawLine of lines) {
    // Tolerate CRLF: the transport, not the document, decides the line ending.
    const line = rawLine.replace(/\r$/, '');
    const trimmed = line.trim();
    if (trimmed === '' || trimmed.startsWith('#')) {
      continue;
    }

    const indent = indentOf(line);

    // Everything before `releases:` is skipped silently, the same way the reference implementation
    // in wb-mqtt-serial does — new top-level keys at the head of the file must not break the check.
    if (!inReleases) {
      if (indent === 0 && trimmed === 'releases:') {
        inReleases = true;
      }
      continue;
    }

    // A zero-indent line after `releases:` ends the document section we are allowed to read.
    if (indent === 0) {
      break;
    }

    if (indent === 2) {
      sawNestedLine = true;
    }

    // Any line indented by two spaces or less closes the block that was open. It becomes OUR block
    // only when the line is a well-formed signature line whose name is the one we are looking for.
    if (indent <= 2) {
      if (inOurBlock) {
        // Our block is complete: the rest of the file is none of our business.
        return block;
      }
      const match = SIGNATURE_LINE_RE.exec(line);
      if (match && match[1] === signature) {
        inOurBlock = true;
        // Object.create(null), not {}: the keys come from a file downloaded over the network, and a
        // key such as __proto__ must land in the block as data. It cannot reach a prototype through
        // this assignment even with a plain literal, but that is a property of the language, and
        // this is the one place where the guarantee should be a property of the code.
        block = Object.create(null) as Record<string, string>;
      }
      continue;
    }

    if (!inOurBlock) {
      // Outside our block anything goes, including lines this parser cannot make sense of.
      continue;
    }

    const pair = PAIR_LINE_RE.exec(line);
    if (!pair) {
      throw new ManifestError(`malformed line in the ${signature} block: ${line}`);
    }
    const value = pair[2];
    if (FORBIDDEN_VALUE_PREFIXES.includes(value[0])) {
      throw new ManifestError(`unsupported YAML value in the ${signature} block: ${line}`);
    }
    block![pair[1]] = value;
  }

  // "The manifest looks intact" = `releases:` was seen AND at least one line was nested under it.
  // A truncated response from a captive-portal proxy must not read as "this signature is absent".
  if (!inReleases || !sawNestedLine) {
    throw new ManifestError('manifest is empty or truncated');
  }

  return block;
}

/**
 * Reads the version out of an object path such as fw/by-signature/mge_v3/main/1.1.0.bin.
 * Returns null for anything that is not a .bin named after a version this project can build
 * (for example latest.bin, or MF1.03D.compfw from another device family).
 */
export function parseVersionFromPath(path: string): string | null {
  const name = path.split('/').pop() ?? '';
  if (!name.endsWith(FIRMWARE_SUFFIX)) {
    return null;
  }
  const version = name.slice(0, -FIRMWARE_SUFFIX.length);
  return VERSION_RE.test(version) ? version : null;
}

const channelInfo = (block: Record<string, string>, channel: UpdateChannel): ChannelInfo => {
  const path = block[channel];
  if (!path) {
    return null;
  }
  const version = parseVersionFromPath(path);
  if (version === null) {
    console.warn(`[firmware] cannot read a version out of the ${channel} path: ${path}`);
    return null;
  }
  // Concatenate rather than resolve against a base URL: a value from the manifest must never be
  // able to point the download somewhere else.
  return { version, url: `${FW_RELEASES_BASE}${path.replace(/^\/+/, '')}` };
};

export function describeChannels(block: Record<string, string>): ChannelsView {
  return {
    stable: channelInfo(block, 'stable'),
    testing: channelInfo(block, 'testing'),
  };
}

/**
 * Full lookup: manifest text + device signature + selected channel -> what to offer and what both
 * channels hold. The single entry point the application uses, so that what the tests exercise and
 * what the UI runs cannot drift apart.
 *
 * Throws ManifestError (from parseSignatureBlock) when the manifest is unusable; the caller turns
 * that into "update check unavailable". An empty signature is reported as 'no-signature' here, but
 * callers are expected to catch that case earlier: a device that does not report its signature is
 * a failed check, not a board without published channels.
 */
export function resolveRelease(text: string, signature: string, channel: UpdateChannel): ReleaseResolution {
  const block = signature ? parseSignatureBlock(text, signature) : null;
  if (block === null) {
    return { channels: null, release: { ok: false, reason: 'no-signature' } };
  }
  const channels = describeChannels(block);
  const info = channels[channel];
  if (info === null) {
    return { channels, release: { ok: false, reason: 'unavailable' } };
  }
  return { channels, release: { ok: true, version: info.version, url: info.url } };
}
