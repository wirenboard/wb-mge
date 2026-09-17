export type DeepPartial<T> = T extends object ? {
  [P in keyof T]? : DeepPartial<T[P]>
} : T;

export interface Auth {
  auth: boolean;
  error?: string;
}

export interface Session {
  session_id: number;
}

export interface Uptime {
  days: number;
  hours: number;
  minutes: number;
  seconds: number;
}

export interface Info {
  device_name: string;
  signature?: string; // device signature, e.g. 'mge_v3' (WB-MGE) or 'mgu_v1' (WB-MGU)
  serial_num: number;
  firmware: string;
  hardware: string;
  system_voltage: number;
  heap_total: number; // total heap size in bytes
  heap_free: number; // currently free heap bytes
  heap_min_free: number; // minimum free heap since boot (high water mark)
  ethernet: {
    con_eth: boolean;
    ip: string;
    mask: string;
    gw: string;
    mac: string;
  };
  wifi: {
    mode: WiFiMode;
    con_ap: number;
    con_sta: boolean;
    con_sta_ssid: string;
    enabled: boolean;
    perm_disabled?: boolean;
    sta_ip: string;
    sta_mask: string;
    sta_gw: string;
    sta_mac: string;
    sta_rssi?: number;
    ap_ip: string;
    ap_channel: number;
    ap_mac: string;
  };
  // /info still carries "rs485_1"/"rs485_2", but the firmware now reports nothing inside them:
  // the module that measured port activity is gone, so the objects arrive empty and the UI
  // has nothing to read from them.
  psram_available: boolean;
  psram_size_kb: number;
}

export type WiFiSecuityProtocol = 'open' | 'wpa2_psk' | 'wpa3_psk';

export type Baudrate = 1200 | 2400 | 4800 | 9600 | 19200 | 38400 | 57600 | 115200;

export type Stopbits = '1' | '1.5' | '2';

export type Databits = '5' | '6' | '7' | '8';

export type Parity = 'none' | 'even' | 'odd';

export type WiFiMode = 'none' | 'ap' | 'sta';

export type UpdateChannel = 'stable' | 'testing';

export interface RsSettings {
  term: boolean;
  fail_safe: boolean;
  tx_disabled: boolean;
  baudrate: Baudrate;
  stopbits: Stopbits;
  parity: Parity;
  databits: Databits;
}

export interface Settings {
  hostname: string;
  login: string;
  pass?: string;
  web_port: number;
  // Modbus slave identity of the device itself, not of one port: the device answers at
  // `mb_slave_id` (1..247) on BOTH RS-485 ports, and serves Modbus TCP on `mb_tcp_port`.
  // Optional: the Modbus role is chosen when the firmware is built (MB_ROLE=slave|master|none)
  // and only a slave build has a Modbus identity to expose, so a master or Modbus-less build
  // sends neither key. Their presence in the document is the only signal of the role there is —
  // the firmware reports it nowhere else — so readers must treat both as possibly absent.
  mb_slave_id?: number;
  mb_tcp_port?: number;
  io_bus: boolean;
  vout: boolean;
  update_channel: UpdateChannel;
  wifi_perm_disable?: boolean;
  wifi?: {
    mode: WiFiMode;
    ap_ip_static: string;
    ap_mask_static: string;
    ap_gw_static: string;
    ap_ssid: string;
    ap_auth: WiFiSecuityProtocol;
    ap_pass: string;
    sta_ssid: string;
    sta_auth: WiFiSecuityProtocol;
    sta_pass: string;
    sta_dhcpc: boolean;
    sta_ip_static: string;
    sta_gw_static: string;
    sta_mask_static: string;
  };
  ethernet: {
    ip_static: string;
    mask_static: string;
    gw_static: string;
    dhcpc: boolean;
  };
  rs485_1: RsSettings;
  rs485_2: RsSettings;
}

export interface WifiScanStartResponce {
  message: string;
  success: boolean;
}

export interface WifiScanResponce {
  networks: WiFiNetwork[];
  scan_completed: boolean;
  scan_in_progress: boolean;
  error: string;
}

export interface WiFiNetwork {
  ssid: string;
  rssi: number;
  bssid: string;
  channel: number;
}

export interface LogoutResponse {
  logout: boolean;
}

// One advisory warning attached to an ACCEPTED settings write: the firmware saved the settings,
// but something about the resulting configuration needs the user's attention (today: an inherited
// TCP port collision — two services on one port, one of which will not bind).
// `code` is the machine-readable identifier the UI translates; `message` is the firmware's own
// English text, used as a fallback for codes this build does not know yet.
export interface SettingsWarning {
  code: string;
  message: string;
}

export interface UpdateSettingsResponse {
  success: boolean;
  warnings?: SettingsWarning[];
}

export interface CommandResponse {
  command: string;
  success: boolean;
}

export interface UpdateResponse {
  message: string;
  success: boolean;
  bytes_written: number;
}
