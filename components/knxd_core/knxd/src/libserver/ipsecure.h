/*
    knxd - KNX daemon
    KNX IP Secure - server-side TCP session handling

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef IPSECURE_H
#define IPSECURE_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <map>
#include <string>

// Session status codes (03_08_09 §5.2.7.5)
constexpr uint8_t STATUS_AUTH_SUCCESS     = 0x00;
constexpr uint8_t STATUS_AUTH_FAILED      = 0x01;
constexpr uint8_t STATUS_UNAUTHENTICATED  = 0x02;
constexpr uint8_t STATUS_TIMEOUT          = 0x03;
constexpr uint8_t STATUS_KEEPALIVE        = 0x04;
constexpr uint8_t STATUS_CLOSE            = 0x05;

// Crypto constants
constexpr size_t IPSEC_MAC_SIZE    = 16;
constexpr size_t IPSEC_KEY_SIZE    = 16;
constexpr size_t IPSEC_ECDH_SIZE   = 32;

// Max concurrent unauthenticated + authenticated sessions
constexpr int IPSEC_MAX_SESSIONS = 16;

/** Per-session state for KNX IP Secure unicast */
struct SecureSession {
  uint16_t session_id;
  uint8_t session_key[IPSEC_KEY_SIZE];

  // ECDH key exchange data (kept for authenticate step, cleared after)
  uint8_t xor_client_server[IPSEC_ECDH_SIZE]; // client_pub XOR server_pub

  // Sequence counters (recv_seq starts at max so first frame with seq=0 is accepted)
  uint64_t send_seq;
  uint64_t recv_seq;

  // Authenticated user
  uint8_t user_id;  // 0 = not yet authenticated

  enum State { IDLE, UNAUTHENTICATED, AUTHENTICATED } state;

  ~SecureSession() {
    memset(session_key, 0, IPSEC_KEY_SIZE);
    memset(xor_client_server, 0, IPSEC_ECDH_SIZE);
  }
};

/** KNX IP Secure crypto and session management */
class IPSecure {
public:
  IPSecure();
  ~IPSecure();

  // Configuration
  void setDeviceAuthPassword(const std::string& password);
  // Set device authentication code directly (16 raw bytes, e.g. FDSK from certificate)
  void setDeviceAuthKey(const uint8_t key[IPSEC_KEY_SIZE]);
  void setUserPassword(uint8_t userId, const std::string& password);
  void setUserPasswordKey(uint8_t userId, const uint8_t key[IPSEC_KEY_SIZE]);
  void setSerialNumber(const uint8_t sno[6]);


  // Session management
  SecureSession* findSession(uint16_t session_id);
  void removeSession(uint16_t session_id);

  // Handle SESSION_REQUEST: returns SESSION_RESPONSE bytes to send
  std::vector<uint8_t> handleSessionRequest(const uint8_t* data, size_t len);

  // Handle SESSION_AUTHENTICATE (already unwrapped from SecureWrapper)
  bool handleSessionAuthenticate(uint16_t session_id,
                                 const uint8_t* data, size_t len);

  // Unwrap a SECURE_WRAPPER frame
  std::vector<uint8_t> unwrapSecure(const uint8_t* data, size_t len,
                                     uint16_t& session_id_out);

  // Wrap a KNXnet/IP frame in SECURE_WRAPPER
  std::vector<uint8_t> wrapSecure(uint16_t session_id,
                                   const uint8_t* knxip_frame, size_t len);

  // Build a SESSION_STATUS frame wrapped in SECURE_WRAPPER
  std::vector<uint8_t> buildSessionStatus(uint16_t session_id, uint8_t status);

  bool isEnabled() const { return enabled; }

#ifdef ESP_PLATFORM
  void pregenECDHKeypair();
#endif

private:
  bool enabled;
  uint8_t serial_number[6];
  uint8_t device_auth_key[IPSEC_KEY_SIZE];

  // user_id -> password hash (16 bytes)
  std::map<uint8_t, std::vector<uint8_t>> user_pwd_hashes;

  // session_id -> session
  std::map<uint16_t, SecureSession> sessions;
  uint16_t next_session_id;

  uint16_t allocSessionId();

  static void deriveDeviceAuthKey(const std::string& password, uint8_t key[IPSEC_KEY_SIZE]);
  static void deriveUserPwdHash(const std::string& password, uint8_t key[IPSEC_KEY_SIZE]);

  static bool computeMAC16(const uint8_t key[IPSEC_KEY_SIZE],
                           const uint8_t b0[16],
                           const uint8_t* aad, size_t aad_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t mac[IPSEC_MAC_SIZE]);

  static bool ctrEncrypt(const uint8_t key[IPSEC_KEY_SIZE],
                         const uint8_t ctr0[16],
                         uint8_t* data, size_t data_len);

  static void xorBytes(uint8_t* out, const uint8_t* a, const uint8_t* b, size_t len);

#ifdef ESP_PLATFORM
  // Pre-generated ECDH keypair for fast session setup.
  struct PregenKey {
    bool ready = false;
    uint8_t pub[IPSEC_ECDH_SIZE];
    uint8_t priv[IPSEC_ECDH_SIZE]; // mbedTLS private key (big-endian MPI)
  };
  PregenKey pregen_;
#endif
};

#endif
