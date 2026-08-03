import { ref } from 'vue';
import type { UpdateResponse } from '@/common/types';
import { api } from '@/utils/api';

// Ceiling picked for a full upload in the repository's own e2e suite (api_tests/22_test_ota.py:337
// uses timeout=180). The previous 30 s could not cover a Wi-Fi upload of a ~1.2 MB image; this also
// applies to the manual "install from file" path.
export const UPDATE_TIMEOUT_MS = 180000;

const isUpdating = ref(false);

// The device answers a refused OTA with a JSON body carrying one of five distinct reasons. Without
// surfacing that text every failure looks the same and support has to ask for a UART log.
export class DeviceUpdateError extends Error {}

const readDeviceError = async (response?: Response): Promise<string | null> => {
  if (!response) {
    return null;
  }
  try {
    const body = await response.clone().json();
    return typeof body?.error === 'string' ? body.error : null;
  } catch {
    return null;
  }
};

export const useFirmware = () => {
  // Blob, not File: the same endpoint takes both the file the user picked and the image the
  // channel installer downloaded. Note that ky's onUploadProgress must NOT be used here — it
  // replaces the body with a ReadableStream, and Chrome rejects such requests over HTTP/1.1.
  const update = async (file: Blob) => {
    isUpdating.value = true;
    await api<UpdateResponse>('update', { method: 'POST', body: file, timeout: UPDATE_TIMEOUT_MS })
      .catch(async (err) => {
        // 409: the device already has a firmware image written and is about to reboot into it, so
        // it refuses to start over. Retrying only makes sense after the reboot.
        if (err.response?.status === 409) {
          throw new Error('update_in_progress');
        }
        const detail = await readDeviceError(err.response);
        if (detail) {
          throw new DeviceUpdateError(detail);
        }
        throw err;
      })
      .finally(() => {
        isUpdating.value = false;
      });
  };

  return {
    update,
    isUpdating,
  };
};
