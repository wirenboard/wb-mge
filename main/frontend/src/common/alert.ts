import { reactive } from 'vue';

// 'warning' is for an ACCEPTED operation whose result still needs the user's attention — amber,
// not green and not red: nothing failed, but it must not read as a clean success either.
type AlertType = 'success' | 'error' | 'warning';

interface Alert {
  message: string;
  type?: AlertType;
  interpolation?: string;
}

interface AlertSettings {
  type?: AlertType;
  timeout?: number;
  interpolation?: string;
}

// How long an alert stays up when the caller does not ask for a specific lifetime.
export const DEFAULT_ALERT_TIMEOUT_MS = 4000;

const alerts = reactive<Alert[]>([]);

export const useAlerts = () => {
  const showAlert = (message: string, settings?: AlertSettings) => {
    const alert: Alert = {
      message,
      type: settings?.type || 'error',
      interpolation: settings?.interpolation,
    };
    alerts.push(alert);

    // Remove THIS alert, by identity — not alerts.shift(). The head of the array is the OLDEST
    // alert, which is the one this timer was armed for only as long as every alert lives exactly
    // as long as every other one. The moment two lifetimes differ, the expiry order and the FIFO
    // order stop agreeing: a short-lived toast's timer would take a longer-lived alert down ahead
    // of its time, and that alert's own timer would later take an innocent third one.
    setTimeout(() => {
      const index = alerts.indexOf(alert);
      if (index >= 0) {
        alerts.splice(index, 1);
      }
    }, settings?.timeout || DEFAULT_ALERT_TIMEOUT_MS);
  };

  return {
    alerts,
    showAlert,
  };
};
