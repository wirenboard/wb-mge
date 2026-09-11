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

export type PortMode = 'disabled' | 'tcp_bridge' | 'passive' | 'repeater';

export interface RsStatus {
  is_busy: boolean;
  error_percentage: number;
  server_connections_count: number;
  port_mode: PortMode;
  cache_enabled: boolean;
}

// Live stats for the transparent RS-485 repeater (Port 1 <-> Port 2 passthrough).
export interface RepeaterStats {
  // true when both ports are in repeater mode
  active: boolean;
  // milliseconds since forwarding became active (0 if inactive)
  uptime_ms: number;
  // bytes forwarded Port 1 -> Port 2 (the TX->RX / forward arrow)
  bytes_1to2: number;
  // bytes forwarded Port 2 -> Port 1 (the RX<-TX / reverse arrow)
  bytes_2to1: number;
  // bytes dropped on Port 1
  dropped_1: number;
  // bytes dropped on Port 2
  dropped_2: number;
}

// What the Z-Wave board last wrote about itself (registers 541–551 of the unit-0xFF map).
// Every field is a raw code straight from the board; the UI decodes them for display.
export interface AirzoneGatewayState {
  valid: boolean; // false until the board has written at least once — the other fields are then meaningless
  age_s: number; // seconds since that write; the board refreshes every 10 s while it is alive
  // The four settings the board is ACTUALLY running on: it validates each of them separately and
  // refuses them one at a time, so these can disagree with the settings that were sent to it.
  slave: number;
  zone: number;
  product: number;
  baud_code: number;
  link_state: number; // 0 init, 1 online, 2 degraded, 3 offline
  errors: number; // consecutive Modbus errors
  last_exception: number; // last Modbus exception code
  scale: number; // 0 °C, 1 °F, 2 not read yet
  dead_runs: number; // 16-bit bitmap of poll runs that are not answering
  bus_load: number; // bus occupancy, percent; reads 0 for the first ten seconds after the board boots
  setpoint_model: number; // 0 single, 1 double, 2 undetermined
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
  rs485_1: RsStatus;
  rs485_2: RsStatus;
  repeater?: RepeaterStats; // optional: older firmware may omit it
  airzone_gw?: AirzoneGatewayState; // optional: older firmware may omit it
  cache_modbus_port: number;
  cache_modbus_server_enabled: boolean;
  cache_value_timeout_s: number;
  psram_available: boolean;
  psram_size_kb: number;
}

export type WiFiSecuityProtocol = 'open' | 'wpa2_psk' | 'wpa3_psk';

export type Baudrate = 1200 | 2400 | 4800 | 9600 | 19200 | 38400 | 57600 | 115200;

export type Stopbits = '1' | '1.5' | '2';

export type Databits = '5' | '6' | '7' | '8';

export type Parity = 'none' | 'even' | 'odd';

export type BridgeMode = 'client' | 'server';

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
  bridge: {
    mode: BridgeMode;
    ip: string;
    port: number;
    modbus: boolean;
  };
}

// Product type of the Airzone unit: 0 Airzone, 1 Aidoo Pro DX/Fan coil,
// 2 Aidoo Pro Air to Water, 3 Aidoo Pro Ventilation.
export type AirzoneProduct = 0 | 1 | 2 | 3;

// The four settings the gateway hands to the Z-Wave board.
export interface AirzoneSettings {
  slave: number; // Modbus address of the Airzone unit, 1…247
  zone: number; // zone number, 1…32
  // Line speed as a multiplier of 1200 baud (4…96). The wire carries the code, never the baud:
  // that is the whole point of the encoding, so the UI converts for display and sends the code.
  baud_code: number;
  product: AirzoneProduct;
}

export interface Settings {
  hostname: string;
  login: string;
  pass?: string;
  web_port: number;
  io_bus: boolean;
  vout: boolean;
  cache_modbus_port: number;
  cache_modbus_server_enabled: boolean;
  cache_value_timeout_s: number;
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
  airzone?: AirzoneSettings; // optional: older firmware may omit it
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
