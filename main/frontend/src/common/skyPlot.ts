// Geometry and signal bands of the GPS sky plot, kept free of the DOM so the canvas component only
// renders and the math can be tested on its own.

// dB-Hz: below SNR_LOW a satellite is barely usable, from SNR_HIGH on it is strong.
export const SNR_LOW_DBHZ = 20;
export const SNR_HIGH_DBHZ = 35;

const DEG_TO_RAD = Math.PI / 180;
const ZENITH_DEG = 90;

export type SnrBand = 'none' | 'weak' | 'medium' | 'strong';

export interface SkyPoint {
  x: number;
  y: number;
}

export interface SkyDetail {
  label: string;
  value: string;
}

// A satellite the plot can place: both angles known, its tooltip already localized by the page.
export interface SkySatellite {
  key: string;
  prn: number;
  elevation: number;
  azimuth: number;
  snr: number | null;
  used: boolean;
  details: SkyDetail[];
}

export const snrBand = (snr: number | null): SnrBand => {
  if (snr === null || !Number.isFinite(snr)) {
    return 'none';
  }
  if (snr < SNR_LOW_DBHZ) {
    return 'weak';
  }
  return snr < SNR_HIGH_DBHZ ? 'medium' : 'strong';
};

const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));

// Distance from the centre of the elevation's ring: the zenith is the centre, the horizon is the
// circle of `radius`. An elevation outside 0..90 is pinned to the disc. Linear in the zenith angle
// (equal ring spacing): with R·cos(el) the whole 0..30° band, where about half of the visible
// satellites are, would be narrower than a marker.
export const skyRingRadius = (elevationDeg: number, radius: number) =>
  radius * (1 - (clamp(elevationDeg, 0, ZENITH_DEG) / ZENITH_DEG));

// Oriented like a map, as GNSS sky plots are: north (azimuth 0) at the top, east to the right.
export const skyPolar = (azimuthDeg: number, distance: number, centre: SkyPoint): SkyPoint => {
  const azimuth = azimuthDeg * DEG_TO_RAD;
  return { x: centre.x + (distance * Math.sin(azimuth)), y: centre.y - (distance * Math.cos(azimuth)) };
};

export const skyProject = (elevationDeg: number, azimuthDeg: number, centre: SkyPoint, radius: number) =>
  skyPolar(azimuthDeg, skyRingRadius(elevationDeg, radius), centre);

// The point nearest to `target` within `reach`, or -1. A tie goes to the later point: it is drawn
// on top, so it is the one under the cursor.
export const skyHitTest = (points: SkyPoint[], target: SkyPoint, reach: number): number => {
  let found = -1;
  let nearest = reach;
  points.forEach((point, index) => {
    const distance = Math.hypot(point.x - target.x, point.y - target.y);
    if (distance <= nearest) {
      found = index;
      nearest = distance;
    }
  });
  return found;
};

// Top-left corner of a `width` x `height` box beside `anchor`: to its right, to its left when the
// right has no room, vertically centred on it, and `margin` inside the bounds either way.
export const skyTooltipOrigin = (
  anchor: SkyPoint,
  size: { width: number; height: number },
  bounds: { width: number; height: number },
  gap: number,
  margin: number,
): SkyPoint => {
  let x = anchor.x + gap;
  if ((x + size.width) > (bounds.width - margin)) {
    x = anchor.x - gap - size.width;
  }
  return {
    x: clamp(x, margin, bounds.width - size.width - margin),
    y: clamp(anchor.y - (size.height / 2), margin, bounds.height - size.height - margin),
  };
};
