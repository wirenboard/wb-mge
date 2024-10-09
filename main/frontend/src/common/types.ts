export interface Auth {
  auth: boolean;
  error?: string;
}

export interface Session {
  session_id: number;
}

interface Package {
  time: number;
  dir: 'TX' | 'RX';
  id: number;
  func: number;
  data: number[];
  crc: number[];
  crc_ok: boolean;
}

export interface Packgages {
  result: boolean;
  packages: Package[];
}

export interface Info {
  device_name: string;
  serial_num: number;
  firmware: string;
  hardware: string;
  con_eth: boolean;
  eth_ip: string;
  eth_mask: string;
  eth_gw: string;
  eth_mac: string;
  con_sta: boolean;
  sta_ip: string;
  sta_mask: string;
  sta_gw: string;
}

export interface Settings {
  hostname: string;
  baudrate: number;
  parity: 'none' | 'even' | 'odd';
  stopbits: '1-bit' | '1.5-bit' | '2-bit';
  databits: '5-bit' | '6-bit' | '7-bit' | '8-bit';
  eth_ip_static: string;
  eth_mask_static: string;
  eth_gw_static: string;
  eth_dhcpc: boolean;
  ap_gw_static: string;
  ap_ip_static: string;
  ap_mask_static: string;
  ap_ssid: string;
  ap_pass: string;
  sta_ssid: string;
  sta_pass: string;
  bridge_mode: 'tcpc-serial' | 'tcps-serial';
  bridge_ip: string;
  bridge_port: number;
  bridge_mb: boolean;
  login: string;
  pass: string;
  wifi_mode: 'none' | 'ap' | 'sta' | 'apsta';
}
