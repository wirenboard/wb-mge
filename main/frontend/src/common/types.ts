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

export type PortMode = 'disabled' | 'tcp_bridge' | 'sniffer' | 'cache_bus';

export interface RsStatus {
  is_busy: boolean;
  error_percentage: number;
  server_connections_count: number;
  port_mode: PortMode;
}

export interface Info {
  device_name: string;
  serial_num: number;
  firmware: string;
  hardware: string;
  system_voltage: number;
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
}

export type WiFiSecuityProtocol = 'open' | 'wpa2_psk' | 'wpa3_psk';

export type Baudrate = 1200 | 2400 | 4800 | 9600 | 19200 | 38400 | 57600 | 115200;

export type Stopbits = '1' | '1.5' | '2';

export type Databits = '5' | '6' | '7' | '8';

export type Parity = 'none' | 'even' | 'odd';

export type BridgeMode = 'client' | 'server';

export type WiFiMode = 'none' | 'ap' | 'sta';

export interface RsSettings {
  term: boolean;
  fail_safe: boolean;
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

export interface Settings {
  hostname: string;
  login: string;
  pass?: string;
  web_port: number;
  io_bus: boolean;
  vout: boolean;
  wifi: {
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

export interface UpdateSettingsResponse {
  success: boolean;
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
