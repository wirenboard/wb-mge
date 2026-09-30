export const SDR_FFT_SIZE = 1024;

const INT8_FULL_SCALE = 128;
// Keeps log10 finite for empty bins: 1e-14 is -140 dBFS, the bottom of the display floor range.
const MIN_POWER = 1e-14;

export interface IqSamples {
  i: Float32Array;
  q: Float32Array;
}

// The capture body is interleaved int8 pairs, I then Q.
export const iqFromInt8 = (buffer: ArrayBuffer): IqSamples => {
  const raw = new Int8Array(buffer);
  const count = Math.floor(raw.length / 2);
  const i = new Float32Array(count);
  const q = new Float32Array(count);
  for (let k = 0; k < count; k++) {
    i[k] = raw[2 * k] / INT8_FULL_SCALE;
    q[k] = raw[(2 * k) + 1] / INT8_FULL_SCALE;
  }
  return { i, q };
};

export const hannWindow = (n: number): Float64Array => {
  const taper = new Float64Array(n);
  for (let k = 0; k < n; k++) {
    taper[k] = 0.5 - (0.5 * Math.cos((2 * Math.PI * k) / (n - 1)));
  }
  return taper;
};

// Forward transform, X[k] = sum x[m]·e^(-j·2πkm/n), computed in place; n must be a power of two.
export const fftInPlace = (re: Float64Array, im: Float64Array): void => {
  const n = re.length;
  if ((n & (n - 1)) !== 0) {
    throw new Error(`FFT size ${n} is not a power of two`);
  }

  for (let k = 1, j = 0; k < n; k++) {
    let bit = n >> 1;
    for (; (j & bit) !== 0; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (k < j) {
      const swapRe = re[k];
      const swapIm = im[k];
      re[k] = re[j];
      im[k] = im[j];
      re[j] = swapRe;
      im[j] = swapIm;
    }
  }

  const half = n >> 1;
  const twiddleRe = new Float64Array(half);
  const twiddleIm = new Float64Array(half);
  for (let k = 0; k < half; k++) {
    twiddleRe[k] = Math.cos((2 * Math.PI * k) / n);
    twiddleIm[k] = -Math.sin((2 * Math.PI * k) / n);
  }

  for (let len = 2; len <= n; len <<= 1) {
    const halfLen = len >> 1;
    const stride = n / len;
    for (let start = 0; start < n; start += len) {
      for (let k = 0; k < halfLen; k++) {
        const wr = twiddleRe[k * stride];
        const wi = twiddleIm[k * stride];
        const a = start + k;
        const b = a + halfLen;
        const tr = (re[b] * wr) - (im[b] * wi);
        const ti = (re[b] * wi) + (im[b] * wr);
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
};

// Welch-style average over the whole blocks of the capture, normalised so that a full-scale complex
// tone centred on a bin reads 1 (0 dBFS). Bins are in FFT order: DC first.
export const averagedPowerSpectrum = ({ i, q }: IqSamples, n: number = SDR_FFT_SIZE): Float64Array => {
  const blocks = Math.floor(i.length / n);
  const power = new Float64Array(n);
  if (blocks === 0) {
    return power;
  }

  const taper = hannWindow(n);
  const taperSum = taper.reduce((sum, w) => sum + w, 0);
  const scale = 1 / (taperSum * taperSum * blocks);
  const re = new Float64Array(n);
  const im = new Float64Array(n);

  for (let block = 0; block < blocks; block++) {
    const offset = block * n;
    let meanI = 0;
    let meanQ = 0;
    for (let k = 0; k < n; k++) {
      meanI += i[offset + k];
      meanQ += q[offset + k];
    }
    meanI /= n;
    meanQ /= n;

    for (let k = 0; k < n; k++) {
      re[k] = (i[offset + k] - meanI) * taper[k];
      im[k] = (q[offset + k] - meanQ) * taper[k];
    }
    fftInPlace(re, im);
    for (let k = 0; k < n; k++) {
      power[k] += ((re[k] * re[k]) + (im[k] * im[k])) * scale;
    }
  }
  return power;
};

// Reorders FFT bins so the most negative frequency comes first and DC sits at n/2.
export const fftShift = (bins: Float64Array): Float64Array => {
  const n = bins.length;
  const shifted = new Float64Array(n);
  for (let j = 0; j < n; j++) {
    shifted[j] = bins[(j + (n >> 1)) % n];
  }
  return shifted;
};

export const powerToDbfs = (power: Float64Array): Float32Array =>
  Float32Array.from(power, (p) => 10 * Math.log10(Math.max(MIN_POWER, p)));

export const spectrumDbfs = (buffer: ArrayBuffer, n: number = SDR_FFT_SIZE): Float32Array =>
  powerToDbfs(fftShift(averagedPowerSpectrum(iqFromInt8(buffer), n)));
