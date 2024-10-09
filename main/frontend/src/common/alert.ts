import { reactive } from 'vue';

type AlertType = 'success' | 'error';

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

const alerts = reactive<Alert[]>([]);

export const useAlerts = () => {
  const showAlert = (message: string, settings?: AlertSettings) => {
    alerts.push({ message, type: settings?.type || 'error', interpolation: settings?.interpolation });

    const timeoutId = setTimeout(() => {
      clearTimeout(timeoutId);
      alerts.shift();
    }, settings?.timeout || 4000);
  };

  return {
    alerts,
    showAlert,
  };
};
