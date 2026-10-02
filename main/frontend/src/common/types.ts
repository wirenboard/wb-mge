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
  // Present, and true, only on firmware that has the fingerprint access control and GET /fp/*.
  fp_acs?: boolean;
}

export type FpEnrollState = 'idle' | 'place' | 'lift' | 'processing' | 'done' | 'error' | 'cancelled';

export type FpEventSource = 'fingerprint' | 'telegram' | 'mqtt';

export type FpEventResult = 'granted' | 'denied' | 'unknown';

export interface FpEvent {
  seq: number;
  uptime_s: number; // device uptime at the moment of the event
  source: FpEventSource;
  result: FpEventResult;
  user_id: number | null;
  user_name: string | null;
  score: number | null;
  chat: string | null; // the Telegram chat's title, or its id; null otherwise
}

// A record of GET /fp/log. seq keeps growing across reboots; last_event.seq is the same number.
export interface FpLogEntry {
  seq: number;
  time: number | null; // UTC unix seconds; null when the device clock was not set yet
  source: FpEventSource;
  result: FpEventResult;
  user_id: number | null;
  user_name: string | null; // the name at the time of the attempt
  score: number | null;
  chat: string | null; // the Telegram chat's title, or its id; null otherwise
}

// GET /fp/log: newest first
export interface FpLogResponse {
  entries: FpLogEntry[];
}

// GET /fp/status
export interface FpStatus {
  sensor: {
    online: boolean;
    capacity: number;
    used: number;
  };
  // Changes whenever the user list changes, including from MQTT: the cue to re-read GET /fp/users.
  users_rev: number;
  enroll: {
    state: FpEnrollState;
    user_id: number | null;
    step: number;
    steps: number;
    slot: number | null;
    error: string | null;
  };
  last_event: FpEvent | null;
  // The last image GET /fp/image serves: width * height bytes of 8-bit grayscale.
  image: {
    seq: number;
    event_seq: number; // the last_event seq the image was taken for; 0 for an enrollment take
    width: number;
    height: number;
  } | null;
  lock: {
    on: boolean; // the relay as of the last successful coil write (pulses included) or idle-probe read
    online: boolean; // the relay module answered the last write or idle probe
    // null once the last reconciliation (every 5 s without a lock request while the module answers,
    // and on a lock settings change) found the relay module holding its timer and exit button
    // configuration, with no other input acting on the channel; else the esp_err name of the first
    // read or write that failed: "ESP_ERR_INVALID_RESPONSE" a Modbus exception, as from a firmware
    // older than 1.28.0, or a damaged frame, "ESP_ERR_TIMEOUT" no answer, "ESP_ERR_NOT_FINISHED" no try since boot yet
    config_error: string | null;
  };
  telegram: {
    enabled: boolean; // a bot token is set
    online: boolean; // the last getUpdates got HTTP 200 with "ok": true and no sendMessage since failed to reach the server; false while not enabled
    error: string | null; // why the last request failed, or "invalid token"; null when online or not enabled
  };
}

// POST /fp/lock
export interface FpLockRequest {
  on: boolean;
}

export interface FpUser {
  id: number;
  name: string;
  enabled: boolean;
  allow_fp: boolean;
  allow_tg: boolean;
  tg_username: string;
  fingers: number[]; // sensor slots
}

// GET /fp/users
export interface FpUsersResponse {
  users: FpUser[];
  max_users: number;
  max_fingers: number;
}

// POST /fp/user: without an id it creates a user, with one it updates the fields given.
export interface FpUserRequest {
  id?: number;
  name: string;
  enabled?: boolean;
  allow_fp?: boolean;
  allow_tg?: boolean;
  tg_username?: string;
}

export interface FpUserResponse {
  success: boolean;
  user: FpUser;
}

export interface FpSuccessResponse {
  success: boolean;
}

export interface FpTgChat {
  id: string;
  name: string;
}

// GET /fp/tg_chats
export interface FpTgChatsResponse {
  chats: FpTgChat[];
}

export type WiFiSecuityProtocol = 'open' | 'wpa2_psk' | 'wpa3_psk';

export type Baudrate = 1200 | 2400 | 4800 | 9600 | 19200 | 38400 | 57600 | 115200;

export type Stopbits = '1' | '1.5' | '2';

export type Databits = '5' | '6' | '7' | '8';

export type Parity = 'none' | 'even' | 'odd';

export type WiFiMode = 'none' | 'ap' | 'sta';

export type UpdateChannel = 'stable' | 'testing';

export type LockExitInput = 'off' | '1' | '2' | '3' | '4' | '5' | '6';

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
  // Lock: the Modbus relay that drives it (address 1..247 on RS-485 port 1, channel 1..8), how
  // long an opening lasts (100..60000 ms) and which relay state opens it: 'strike' - relay on
  // opens (electric strike), 'magnet' - relay on locks (electromagnet). lock_exit_input is the
  // relay module input the exit button is wired to, 'off' for none: the device has the module itself
  // put the channel in the open state on its closing.
  lock_addr: number;
  lock_channel: number;
  lock_pulse_ms: number;
  lock_mode: 'strike' | 'magnet';
  lock_exit_input: LockExitInput;
  // Telegram bot: its token, the admins (a comma-separated list of numeric Telegram user IDs) and
  // the Bot API server it talks to, https://api.telegram.org unless that is blocked.
  tg_token: string;
  tg_admins: string;
  tg_api_url: string;
  // MQTT: whether the device connects to a broker, where that broker is, what it logs in as, and
  // the identifier it appears under in /devices/<id>/... . An empty mqtt_id means the firmware
  // builds one from the model and the MAC; an empty mqtt_host means no broker is configured and
  // nothing is started even with mqtt_enabled true.
  mqtt_enabled: boolean;
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_pass: string;
  mqtt_id: string;
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
