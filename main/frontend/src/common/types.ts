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

export interface RsStatus {
  is_busy: boolean;
  error_percentage: number;
  server_connections_count: number;
}

export interface Info {
  device_name: string;
  serial_num: number;
  firmware: string;
  hardware: string;
  ethernet: {
    con_eth: boolean;
    eth_ip: string;
    eth_mask: string;
    eth_gw: string;
    eth_mac: string;
  };
  wifi: {
    con_ap: number;
    con_sta: boolean;
    sta_ip: string;
    sta_mask: string;
    sta_gw: string;
    sta_mac: string;
    sta_rssi?: number;
    ap_channel: number;
    ap_mac: string;
  };
  rs485_1: RsStatus;
  rs485_2: RsStatus;
}

export type WiFiSecuityProtocol = 'open' | 'wpa2_psk' | 'wpa3_psk';

export type Baudrate = 1200 | 2400 | 4800 | 9600 | 19200 | 38400 | 57600 | 115200;

export type Stopbits = '1-bit' | '1.5-bit' | '2-bit';

export type Databits = '5-bit' | '6-bit' | '7-bit' | '8-bit';

export type Parity = 'none' | 'even' | 'odd';

export type BridgeMode = 'client' | 'server';

export type WiFiMode = 'none' | 'ap' | 'sta' | 'apsta';

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

export interface WifiScanResult {
  networks: WiFiNetwork[];
  scan_completed: boolean;
  scan_in_progress: boolean;
}

export interface WiFiNetwork {
  ssid: string;
  rssi: number;
  bssid: string;
  channel: number;
}
