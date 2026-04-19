/*
    knxd - KNX daemon
    KNX IP Secure - server-side TCP session handling

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "ipsecure.h"
#include "eibnetip.h"

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/sha.h>
#else
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/bignum.h>
#endif

#include <cstring>

// Use header constants from eibnetip.h
// HEADER_SIZE_10 = 0x06, KNXNETIP_VERSION_10 = 0x10

static void putU16BE(uint8_t* p, uint16_t v) {
  p[0] = (v >> 8) & 0xFF;
  p[1] = v & 0xFF;
}

static uint16_t getU16BE(const uint8_t* p) {
  return ((uint16_t)p[0] << 8) | p[1];
}

static uint64_t getU48BE(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++)
    v = (v << 8) | p[i];
  return v;
}

static void putU48BE(uint8_t* p, uint64_t v) {
  for (int i = 5; i >= 0; i--) {
    p[i] = v & 0xFF;
    v >>= 8;
  }
}

static void buildKNXIPHeader(uint8_t* buf, uint16_t service, uint16_t total_len) {
  buf[0] = HEADER_SIZE_10;
  buf[1] = KNXNETIP_VERSION_10;
  putU16BE(buf + 2, service);
  putU16BE(buf + 4, total_len);
}

// AES-128-CBC encrypt (no padding) — for CBC-MAC

#ifdef HAVE_OPENSSL

static bool aes_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                            const uint8_t* pt, int pt_len,
                            uint8_t* ct, int* ct_len) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return false;
  EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  int len = 0;
  EVP_EncryptUpdate(ctx, ct, &len, pt, pt_len);
  *ct_len = len;
  int fl = 0;
  EVP_EncryptFinal_ex(ctx, ct + len, &fl);
  *ct_len += fl;
  EVP_CIPHER_CTX_free(ctx);
  return true;
}

#else // mbedTLS

static bool aes_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                            const uint8_t* pt, int pt_len,
                            uint8_t* ct, int* ct_len) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, key, 128) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }
  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, pt_len, iv_copy, pt, ct);
  mbedtls_aes_free(&aes);
  *ct_len = pt_len;
  return ret == 0;
}

#endif



// IPSecure implementation


IPSecure::IPSecure()
  : enabled(false), next_session_id(1)
{
  memset(serial_number, 0, 6);
  memset(device_auth_key, 0, IPSEC_KEY_SIZE);
}

IPSecure::~IPSecure() {
  memset(device_auth_key, 0, IPSEC_KEY_SIZE);
#ifdef ESP_PLATFORM
  memset(&pregen_, 0, sizeof(pregen_));
#endif
}

#ifdef ESP_PLATFORM
void IPSecure::pregenECDHKeypair() {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point Q;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;

  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&Q);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);

  bool ok = true;
  if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0) != 0)
    ok = false;
  if (ok && mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0)
    ok = false;
  if (ok && mbedtls_ecdh_gen_public(&grp, &d, &Q, mbedtls_ctr_drbg_random, &drbg) != 0)
    ok = false;
  if (ok) {
    size_t olen;
    if (mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_COMPRESSED, &olen, pregen_.pub, IPSEC_ECDH_SIZE) != 0)
      ok = false;
  }
  if (ok && mbedtls_mpi_write_binary(&d, pregen_.priv, IPSEC_ECDH_SIZE) != 0)
    ok = false;

  mbedtls_ecp_group_free(&grp);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_point_free(&Q);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);

  pregen_.ready = ok;
  printf("[IPSEC] pregenECDH: %s (pub=%02x%02x...) ready=%d\n",
         ok ? "OK" : "FAILED", pregen_.pub[0], pregen_.pub[1], pregen_.ready);
  fflush(stdout);
}
#endif

void IPSecure::setSerialNumber(const uint8_t sno[6]) {
  memcpy(serial_number, sno, 6);
}

static void pbkdf2_derive(const std::string& password, const char* salt,
                          uint8_t key[IPSEC_KEY_SIZE]) {
#ifdef HAVE_OPENSSL
  PKCS5_PBKDF2_HMAC(password.c_str(), password.size(),
                     (const uint8_t*)salt, strlen(salt),
                     65536, EVP_sha256(), IPSEC_KEY_SIZE, key);
#else
  mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
      (const uint8_t*)password.c_str(), password.size(),
      (const uint8_t*)salt, strlen(salt),
      65536, IPSEC_KEY_SIZE, key);
#endif
}

void IPSecure::deriveDeviceAuthKey(const std::string& password, uint8_t key[IPSEC_KEY_SIZE]) {
  pbkdf2_derive(password, "device-authentication-code.1.secure.ip.knx.org", key);
}

void IPSecure::deriveUserPwdHash(const std::string& password, uint8_t key[IPSEC_KEY_SIZE]) {
  pbkdf2_derive(password, "user-password.1.secure.ip.knx.org", key);
}

void IPSecure::setDeviceAuthPassword(const std::string& password) {
  deriveDeviceAuthKey(password, device_auth_key);
  enabled = true;
#ifdef ESP_PLATFORM
  printf("[IPSEC] setDeviceAuthPassword key=%02x%02x%02x%02x...\n",
         device_auth_key[0], device_auth_key[1], device_auth_key[2], device_auth_key[3]);
#endif
}

void IPSecure::setDeviceAuthKey(const uint8_t key[IPSEC_KEY_SIZE]) {
  // Use the 16-byte key directly as the device authentication code.
  // Per 03_08_09 §2.3.1.3.3: in ex-factory state, Device Authentication Code = FDSK
  // (the raw 16 bytes from the certificate, NOT PBKDF2'd from a password).
  memcpy(device_auth_key, key, IPSEC_KEY_SIZE);
  enabled = true;
}

void IPSecure::setUserPassword(uint8_t userId, const std::string& password) {
  std::vector<uint8_t> hash(IPSEC_KEY_SIZE);
  deriveUserPwdHash(password, hash.data());
  user_pwd_hashes[userId] = hash;
}

void IPSecure::setUserPasswordKey(uint8_t userId, const uint8_t key[IPSEC_KEY_SIZE]) {
  user_pwd_hashes[userId] = std::vector<uint8_t>(key, key + IPSEC_KEY_SIZE);
#ifdef ESP_PLATFORM
  printf("[IPSEC] setUserPasswordKey uid=%d key=%02x%02x%02x%02x...\n",
         userId, key[0], key[1], key[2], key[3]);
#endif
}

void IPSecure::xorBytes(uint8_t* out, const uint8_t* a, const uint8_t* b, size_t len) {
  for (size_t i = 0; i < len; i++)
    out[i] = a[i] ^ b[i];
}

uint16_t IPSecure::allocSessionId() {
  if ((int)sessions.size() >= IPSEC_MAX_SESSIONS)
    return 0;
  for (int i = 0; i < 0xFFFE; i++) {
    uint16_t id = next_session_id++;
    if (next_session_id > 0xFFFE) next_session_id = 1;
    if (sessions.find(id) == sessions.end())
      return id;
  }
  return 0;
}

SecureSession* IPSecure::findSession(uint16_t session_id) {
  auto it = sessions.find(session_id);
  if (it == sessions.end()) return nullptr;
  return &it->second;
}

void IPSecure::removeSession(uint16_t session_id) {
#ifdef ESP_PLATFORM
  printf("[IPSEC] removeSession sid=%d (total sessions before: %zu)\n",
         session_id, sessions.size());
#endif
  // SecureSession destructor clears key material
  sessions.erase(session_id);
}


// CBC-MAC (16-byte) for IP Secure


bool IPSecure::computeMAC16(const uint8_t key[IPSEC_KEY_SIZE],
                            const uint8_t b0[16],
                            const uint8_t* aad, size_t aad_len,
                            const uint8_t* payload, size_t payload_len,
                            uint8_t mac[IPSEC_MAC_SIZE]) {
  size_t input_len = 16 + 2 + aad_len + payload_len;
  size_t padded_len = ((input_len + 15) / 16) * 16;

  std::vector<uint8_t> input(padded_len, 0);
  memcpy(input.data(), b0, 16);
  input[16] = (aad_len >> 8) & 0xFF;
  input[17] = aad_len & 0xFF;
  if (aad_len > 0)
    memcpy(input.data() + 18, aad, aad_len);
  if (payload_len > 0)
    memcpy(input.data() + 18 + aad_len, payload, payload_len);

  uint8_t iv[16] = {};
  std::vector<uint8_t> ct(padded_len);
  int ct_len = 0;

  if (!aes_cbc_encrypt(key, iv, input.data(), padded_len, ct.data(), &ct_len))
    return false;
  if (ct_len < 16) return false;

  memcpy(mac, ct.data() + ct_len - 16, IPSEC_MAC_SIZE);
  return true;
}


// CTR encrypt in-place


bool IPSecure::ctrEncrypt(const uint8_t key[IPSEC_KEY_SIZE],
                          const uint8_t ctr0[16],
                          uint8_t* data, size_t data_len) {
  // IP Secure CTR keystream layout:
  //   Block 0 (bytes 0-15) → encrypts MAC (16 bytes)
  //   Block 1+ (bytes 16+) → encrypts payload
  // On the wire: [payload][MAC], so XOR must be applied out of order.
  // For handshake MACs (16 bytes only), just XOR with block 0.

  size_t total_ks_needed = IPSEC_MAC_SIZE + (data_len > IPSEC_MAC_SIZE ? data_len - IPSEC_MAC_SIZE : 0);
  size_t num_blocks = (total_ks_needed + 15) / 16;

  std::vector<uint8_t> keystream(num_blocks * 16);
  uint8_t counter[16];
  memcpy(counter, ctr0, 16);

#ifdef HAVE_OPENSSL
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return false;
  EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);
  EVP_CIPHER_CTX_set_padding(ctx, 0);

  for (size_t b = 0; b < num_blocks; b++) {
    int outl = 0;
    EVP_EncryptUpdate(ctx, keystream.data() + b * 16, &outl, counter, 16);
    for (int j = 15; j >= 0; j--) {
      if (++counter[j] != 0) break;
    }
  }
  EVP_CIPHER_CTX_free(ctx);
#else
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, key, 128) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }
  for (size_t b = 0; b < num_blocks; b++) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, counter, keystream.data() + b * 16);
    for (int j = 15; j >= 0; j--) {
      if (++counter[j] != 0) break;
    }
  }
  mbedtls_aes_free(&aes);
#endif

  if (data_len <= IPSEC_MAC_SIZE) {
    // Handshake MAC only
    for (size_t i = 0; i < data_len; i++)
      data[i] ^= keystream[i];
  } else {
    // SecureWrapper: [payload][MAC] on wire, keystream: [MAC ks][payload ks]
    size_t payload_len = data_len - IPSEC_MAC_SIZE;
    for (size_t i = 0; i < payload_len; i++)
      data[i] ^= keystream[IPSEC_MAC_SIZE + i];
    for (size_t i = 0; i < IPSEC_MAC_SIZE; i++)
      data[payload_len + i] ^= keystream[i];
  }

  return true;
}


// Handle SESSION_REQUEST → generate SESSION_RESPONSE


std::vector<uint8_t> IPSecure::handleSessionRequest(const uint8_t* data, size_t len) {
#ifdef ESP_PLATFORM
  printf("[IPSEC] handleSessionRequest len=%zu\n", len);
#endif
  if (len != 46) return {};
  if (data[0] != HEADER_SIZE_10 || data[1] != KNXNETIP_VERSION_10) return {};
  if (getU16BE(data + 2) != SESSION_REQUEST_SVC) return {};

  const uint8_t* client_pub = data + 14;
#ifdef ESP_PLATFORM
  printf("[IPSEC] client_pub: %02x%02x%02x%02x...\n",
         client_pub[0], client_pub[1], client_pub[2], client_pub[3]);
#endif

  uint8_t server_pub[IPSEC_ECDH_SIZE];
  uint8_t shared_secret[IPSEC_ECDH_SIZE];
  size_t secret_len = IPSEC_ECDH_SIZE;

#ifdef HAVE_OPENSSL
  // Generate server ECDH key pair (X25519)
  EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
  if (!pctx) return {};
  EVP_PKEY_keygen_init(pctx);
  EVP_PKEY* server_key = NULL;
  EVP_PKEY_keygen(pctx, &server_key);
  EVP_PKEY_CTX_free(pctx);
  if (!server_key) return {};

  size_t pub_len = IPSEC_ECDH_SIZE;
  EVP_PKEY_get_raw_public_key(server_key, server_pub, &pub_len);

  EVP_PKEY* client_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL,
                                                       client_pub, IPSEC_ECDH_SIZE);
  if (!client_key) {
    EVP_PKEY_free(server_key);
    return {};
  }

  // ECDH key agreement
  EVP_PKEY_CTX* dctx = EVP_PKEY_CTX_new(server_key, NULL);
  EVP_PKEY_derive_init(dctx);
  EVP_PKEY_derive_set_peer(dctx, client_key);
  EVP_PKEY_derive(dctx, NULL, &secret_len);
  EVP_PKEY_derive(dctx, shared_secret, &secret_len);
  EVP_PKEY_CTX_free(dctx);
  EVP_PKEY_free(client_key);
  EVP_PKEY_free(server_key);
#else
  // mbedTLS X25519 ECDH
  mbedtls_ecp_group grp;
  mbedtls_mpi d, z;
  mbedtls_ecp_point Qp;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;

  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_mpi_init(&z);
  mbedtls_ecp_point_init(&Qp);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);

  bool ok = true;
  if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0) != 0)
    ok = false;
  if (ok && mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0)
    ok = false;

#ifdef ESP_PLATFORM
  // Use pre-generated keypair if available (~200ms saved)
  printf("[IPSEC] pregen_.ready=%d\n", pregen_.ready);
  fflush(stdout);
  if (ok && pregen_.ready) {
    memcpy(server_pub, pregen_.pub, IPSEC_ECDH_SIZE);
    if (mbedtls_mpi_read_binary(&d, pregen_.priv, IPSEC_ECDH_SIZE) != 0)
      ok = false;
    pregen_.ready = false;  // consumed
    memset(pregen_.priv, 0, IPSEC_ECDH_SIZE);
    printf("[IPSEC] using pre-generated keypair\n");
    fflush(stdout);
  } else
#endif
  {
    // Generate fresh keypair (slow path, ~200ms on ESP32)
    mbedtls_ecp_point Q;
    mbedtls_ecp_point_init(&Q);
    if (ok && mbedtls_ecdh_gen_public(&grp, &d, &Q, mbedtls_ctr_drbg_random, &drbg) != 0)
      ok = false;
    if (ok) {
      size_t olen;
      if (mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_COMPRESSED, &olen, server_pub, IPSEC_ECDH_SIZE) != 0)
        ok = false;
    }
    mbedtls_ecp_point_free(&Q);
  }

  if (ok && mbedtls_ecp_point_read_binary(&grp, &Qp, client_pub, IPSEC_ECDH_SIZE) != 0)
    ok = false;
  if (ok && mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d, mbedtls_ctr_drbg_random, &drbg) != 0)
    ok = false;
  if (ok && mbedtls_mpi_write_binary_le(&z, shared_secret, IPSEC_ECDH_SIZE) != 0)
    ok = false;
  secret_len = IPSEC_ECDH_SIZE;

  mbedtls_ecp_group_free(&grp);
  mbedtls_mpi_free(&d);
  mbedtls_mpi_free(&z);
  mbedtls_ecp_point_free(&Qp);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);

  if (!ok) {
    memset(shared_secret, 0, sizeof(shared_secret));
#ifdef ESP_PLATFORM
    printf("[IPSEC] ECDH key exchange FAILED\n");
#endif
    return {};
  }
#endif

#ifdef ESP_PLATFORM
  printf("[IPSEC] ECDH ok, server_pub: %02x%02x%02x%02x...\n",
         server_pub[0], server_pub[1], server_pub[2], server_pub[3]);
#endif
  // Session key = SHA-256(shared_secret)[0:16]
  uint8_t hash[32];
#ifdef HAVE_OPENSSL
  SHA256(shared_secret, secret_len, hash);
#else
  mbedtls_sha256(shared_secret, secret_len, hash, 0);
#endif
  memset(shared_secret, 0, sizeof(shared_secret));

  uint16_t sid = allocSessionId();
#ifdef ESP_PLATFORM
  printf("[IPSEC] allocated session id=%d, session_key: %02x%02x%02x%02x...\n",
         sid, hash[0], hash[1], hash[2], hash[3]);
#endif
  if (sid == 0) return {};

  SecureSession& session = sessions[sid];
  session.session_id = sid;
  memcpy(session.session_key, hash, IPSEC_KEY_SIZE);
  memset(hash, 0, 32);
  session.send_seq = 0;
  session.recv_seq = UINT64_MAX; // so first frame (seq=0) is accepted
  session.user_id = 0;
  session.state = SecureSession::UNAUTHENTICATED;

  xorBytes(session.xor_client_server, client_pub, server_pub, IPSEC_ECDH_SIZE);

  // Build SESSION_RESPONSE: header(6) + session_id(2) + server_pub(32) + mac(16) = 56
  std::vector<uint8_t> resp(56);
  buildKNXIPHeader(resp.data(), SESSION_RESPONSE_SVC, 56);
  putU16BE(resp.data() + 6, sid);
  memcpy(resp.data() + 8, server_pub, IPSEC_ECDH_SIZE);

  // Device authentication MAC
  uint8_t b0[16] = {};
  uint8_t aad[40];
  memcpy(aad, resp.data(), 6);
  memcpy(aad + 6, resp.data() + 6, 2);
  memcpy(aad + 8, session.xor_client_server, IPSEC_ECDH_SIZE);

  uint8_t mac[IPSEC_MAC_SIZE];
  computeMAC16(device_auth_key, b0, aad, 40, nullptr, 0, mac);

  uint8_t ctr0[16] = {};
  ctr0[14] = 0xFF;
  ctrEncrypt(device_auth_key, ctr0, mac, IPSEC_MAC_SIZE);

  memcpy(resp.data() + 40, mac, IPSEC_MAC_SIZE);
  return resp;
}


// Handle SESSION_AUTHENTICATE


bool IPSecure::handleSessionAuthenticate(uint16_t session_id,
                                         const uint8_t* data, size_t len) {
#ifdef ESP_PLATFORM
  printf("[IPSEC] handleSessionAuthenticate sid=%d len=%zu\n", session_id, len);
  if (len >= 8)
    printf("[IPSEC]   hdr: %02x%02x svc=%04x reserved=%02x userId=%d\n",
           data[0], data[1], getU16BE(data+2), data[6], data[7]);
  printf("[IPSEC]   registered user keys:");
  for (auto& kv : user_pwd_hashes)
    printf(" uid=%d(key:%02x%02x...)", kv.first, kv.second[0], kv.second[1]);
  printf("\n");
#endif
  if (len != 24) { printf("[IPSEC] AUTH FAIL: len != 24\n"); return false; }
  if (getU16BE(data + 2) != SESSION_AUTHENTICATE_SVC) { printf("[IPSEC] AUTH FAIL: bad svc\n"); return false; }
  if (data[6] != 0x00) { printf("[IPSEC] AUTH FAIL: reserved != 0\n"); return false; }

  uint8_t userId = data[7];
  if (userId < 1 || userId > 0x7F) { printf("[IPSEC] AUTH FAIL: bad userId %d\n", userId); return false; }

  auto* session = findSession(session_id);
  if (!session) { printf("[IPSEC] AUTH FAIL: session %d not found\n", session_id); return false; }
  if (session->state != SecureSession::UNAUTHENTICATED) {
    printf("[IPSEC] AUTH FAIL: session state=%d (expected UNAUTHENTICATED)\n", session->state);
    return false;
  }

  auto it = user_pwd_hashes.find(userId);
  if (it == user_pwd_hashes.end()) {
    printf("[IPSEC] AUTH FAIL: no key for userId=%d\n", userId);
    return false;
  }
  const uint8_t* user_key = it->second.data();

  uint8_t recv_mac[IPSEC_MAC_SIZE];
  memcpy(recv_mac, data + 8, IPSEC_MAC_SIZE);

  uint8_t b0[16] = {};
  uint8_t aad[40];
  memcpy(aad, data, 6);
  aad[6] = 0x00;
  aad[7] = userId;
  memcpy(aad + 8, session->xor_client_server, IPSEC_ECDH_SIZE);

  uint8_t expected_mac[IPSEC_MAC_SIZE];
  computeMAC16(user_key, b0, aad, 40, nullptr, 0, expected_mac);

  uint8_t ctr0[16] = {};
  ctr0[14] = 0xFF;
  ctrEncrypt(user_key, ctr0, expected_mac, IPSEC_MAC_SIZE);

#ifdef ESP_PLATFORM
  {
    char h1[33], h2[33];
    for(int i=0;i<16;i++){sprintf(h1+i*2,"%02x",recv_mac[i]);sprintf(h2+i*2,"%02x",expected_mac[i]);}
    printf("IPSecure: Auth sid=%d uid=%d recv=%s expt=%s match=%d\n",
           session_id, userId, h1, h2, memcmp(recv_mac, expected_mac, IPSEC_MAC_SIZE) == 0);
  }
#endif
  if (memcmp(recv_mac, expected_mac, IPSEC_MAC_SIZE) != 0)
    return false;

  session->user_id = userId;
  session->state = SecureSession::AUTHENTICATED;
  // Clear xor_client_server — no longer needed after authentication
  memset(session->xor_client_server, 0, IPSEC_ECDH_SIZE);
  return true;
}


// Unwrap SECURE_WRAPPER


std::vector<uint8_t> IPSecure::unwrapSecure(const uint8_t* data, size_t len,
                                             uint16_t& session_id_out) {
#ifdef ESP_PLATFORM
  printf("[IPSEC] unwrapSecure len=%zu\n", len);
#endif
  if (len < 44) return {};
  if (getU16BE(data + 2) != SECURE_WRAPPER_SVC) return {};
  if (getU16BE(data + 4) != len) return {};

  uint16_t sid = getU16BE(data + 6);
  session_id_out = sid;

  auto* session = findSession(sid);
  if (!session) {
#ifdef ESP_PLATFORM
    printf("[IPSEC] unwrap: session %d NOT FOUND\n", sid);
#endif
    return {};
  }

  uint64_t seq = getU48BE(data + 8);
#ifdef ESP_PLATFORM
  printf("[IPSEC] unwrap: sid=%d seq=%llu prev_seq=%llu state=%d\n",
         sid, (unsigned long long)seq, (unsigned long long)session->recv_seq, session->state);
#endif

  // Sequence must be strictly increasing
  if (session->recv_seq != UINT64_MAX && seq <= session->recv_seq) {
#ifdef ESP_PLATFORM
    printf("[IPSEC] unwrap: SEQUENCE REPLAY seq=%llu <= prev=%llu\n",
           (unsigned long long)seq, (unsigned long long)session->recv_seq);
#endif
    return {};
  }

  size_t encrypted_offset = HEADER_SIZE_10 + 2 + 6 + 6 + 2; // = 22
  size_t encrypted_len = len - encrypted_offset;
  if (encrypted_len < IPSEC_MAC_SIZE) return {};
  size_t inner_len = encrypted_len - IPSEC_MAC_SIZE;

  // CTR0: seq(6) + serial(6) + tag(2) + FF 00
  uint8_t ctr0[16];
  memcpy(ctr0, data + 8, 6);
  memcpy(ctr0 + 6, data + 14, 6);
  memcpy(ctr0 + 12, data + 20, 2);
  ctr0[14] = 0xFF;
  ctr0[15] = 0x00;

  std::vector<uint8_t> decrypted(encrypted_len);
  memcpy(decrypted.data(), data + encrypted_offset, encrypted_len);
  ctrEncrypt(session->session_key, ctr0, decrypted.data(), encrypted_len);

  // Verify CBC-MAC
  uint8_t b0[16];
  memcpy(b0, data + 8, 6);
  memcpy(b0 + 6, data + 14, 6);
  memcpy(b0 + 12, data + 20, 2);
  putU16BE(b0 + 14, inner_len);

  uint8_t aad[8];
  memcpy(aad, data, 6);
  putU16BE(aad + 6, sid);

  uint8_t expected_mac[IPSEC_MAC_SIZE];
  computeMAC16(session->session_key, b0, aad, 8,
               decrypted.data(), inner_len, expected_mac);

  if (memcmp(decrypted.data() + inner_len, expected_mac, IPSEC_MAC_SIZE) != 0) {
#ifdef ESP_PLATFORM
    printf("[IPSEC] unwrap: MAC MISMATCH sid=%d inner_len=%zu\n", sid, inner_len);
    printf("[IPSEC]   recv_mac: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
           decrypted[inner_len+0],decrypted[inner_len+1],decrypted[inner_len+2],decrypted[inner_len+3],
           decrypted[inner_len+4],decrypted[inner_len+5],decrypted[inner_len+6],decrypted[inner_len+7],
           decrypted[inner_len+8],decrypted[inner_len+9],decrypted[inner_len+10],decrypted[inner_len+11],
           decrypted[inner_len+12],decrypted[inner_len+13],decrypted[inner_len+14],decrypted[inner_len+15]);
    printf("[IPSEC]   expt_mac: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
           expected_mac[0],expected_mac[1],expected_mac[2],expected_mac[3],
           expected_mac[4],expected_mac[5],expected_mac[6],expected_mac[7],
           expected_mac[8],expected_mac[9],expected_mac[10],expected_mac[11],
           expected_mac[12],expected_mac[13],expected_mac[14],expected_mac[15]);
#endif
    return {};
  }

#ifdef ESP_PLATFORM
  printf("[IPSEC] unwrap: OK sid=%d inner_len=%zu inner_svc=0x%04x\n",
         sid, inner_len, inner_len >= 4 ? getU16BE(decrypted.data() + 2) : 0);
#endif
  session->recv_seq = seq;
  decrypted.resize(inner_len);
  return decrypted;
}


// Wrap frame in SECURE_WRAPPER


std::vector<uint8_t> IPSecure::wrapSecure(uint16_t session_id,
                                           const uint8_t* knxip_frame, size_t frame_len) {
  auto* session = findSession(session_id);
  if (!session) return {};

  uint64_t seq = session->send_seq++;

  size_t total = HEADER_SIZE_10 + 2 + 6 + 6 + 2 + frame_len + IPSEC_MAC_SIZE;
  std::vector<uint8_t> packet(total);
  buildKNXIPHeader(packet.data(), SECURE_WRAPPER_SVC, total);
  putU16BE(packet.data() + 6, session_id);
  putU48BE(packet.data() + 8, seq);
  memcpy(packet.data() + 14, serial_number, 6);
  putU16BE(packet.data() + 20, 0x0000);

  size_t payload_offset = 22;
  memcpy(packet.data() + payload_offset, knxip_frame, frame_len);

  // CBC-MAC
  uint8_t b0[16];
  putU48BE(b0, seq);
  memcpy(b0 + 6, serial_number, 6);
  putU16BE(b0 + 12, 0x0000);
  putU16BE(b0 + 14, frame_len);

  uint8_t aad[8];
  memcpy(aad, packet.data(), 6);
  putU16BE(aad + 6, session_id);

  uint8_t mac[IPSEC_MAC_SIZE];
  computeMAC16(session->session_key, b0, aad, 8, knxip_frame, frame_len, mac);
  memcpy(packet.data() + payload_offset + frame_len, mac, IPSEC_MAC_SIZE);

  // CTR encrypt
  uint8_t ctr0[16];
  putU48BE(ctr0, seq);
  memcpy(ctr0 + 6, serial_number, 6);
  putU16BE(ctr0 + 12, 0x0000);
  ctr0[14] = 0xFF;
  ctr0[15] = 0x00;

  ctrEncrypt(session->session_key, ctr0,
             packet.data() + payload_offset, frame_len + IPSEC_MAC_SIZE);

  return packet;
}


// Build SESSION_STATUS wrapped in SECURE_WRAPPER


std::vector<uint8_t> IPSecure::buildSessionStatus(uint16_t session_id, uint8_t status) {
#ifdef ESP_PLATFORM
  printf("[IPSEC] buildSessionStatus sid=%d status=0x%02x (%s)\n",
         session_id, status,
         status == 0 ? "SUCCESS" : status == 1 ? "AUTH_FAILED" : "OTHER");
#endif
  uint8_t inner[8];
  buildKNXIPHeader(inner, SESSION_STATUS_SVC, 8);
  inner[6] = status;
  inner[7] = 0x00;
  auto result = wrapSecure(session_id, inner, 8);
#ifdef ESP_PLATFORM
  printf("[IPSEC] buildSessionStatus: wrapped len=%zu\n", result.size());
#endif
  return result;
}

// Note: .knxkeys keyring loading was intentionally removed.
// ETS prompts the user for the "Commissioning Password" when connecting
// to an IP Secure device, regardless of whether the keyring is loaded.
// The passwords in the keyring are what ETS programmed into the device,
// but since knxd is not a real KNX device, it was never programmed by ETS.
// The simplest and most reliable approach is to set user-password directly
// in the knxd config, and have the user enter the same password in ETS
// when prompted.
