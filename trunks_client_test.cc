// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_client_test.h"

#include <base/logging.h>
#include <base/stl_util.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "trunks/error_codes.h"
#include "trunks/hmac_session.h"
#include "trunks/policy_session.h"
#include "trunks/scoped_key_handle.h"
#include "trunks/tpm_generated.h"
#include "trunks/tpm_state.h"
#include "trunks/tpm_utility.h"
#include "trunks/trunks_factory_impl.h"

namespace trunks {

TrunksClientTest::TrunksClientTest() : factory_(new TrunksFactoryImpl()) {}

TrunksClientTest::TrunksClientTest(scoped_ptr<TrunksFactory> factory)
    : factory_(factory.Pass()) {}

TrunksClientTest::~TrunksClientTest() {}

bool TrunksClientTest::RNGTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  std::string entropy_data("entropy_data");
  std::string random_data;
  size_t num_bytes = 70;
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  session->SetEntityAuthorizationValue("");
  result = utility->StirRandom(entropy_data, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error stirring TPM RNG: " << GetErrorString(result);
    return false;
  }
  session->SetEntityAuthorizationValue("");
  result = utility->GenerateRandom(num_bytes, session->GetDelegate(),
                                   &random_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting random bytes from TPM: "
               << GetErrorString(result);
    return false;
  }
  if (num_bytes != random_data.size()) {
    LOG(ERROR) << "Error not enough random bytes received.";
    return false;
  }
  return true;
}

bool TrunksClientTest::SignTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  std::string key_authorization("sign");
  std::string key_blob;
  session->SetEntityAuthorizationValue("");
  result = utility->CreateRSAKeyPair(TpmUtility::AsymmetricKeyUsage::kSignKey,
                                     2048, 0x10001, key_authorization, "",
                                     session->GetDelegate(), &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating signing key: " << GetErrorString(result);
    return false;
  }
  TPM_HANDLE signing_key;
  result = utility->LoadKey(key_blob, session->GetDelegate(), &signing_key);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading signing key: " << GetErrorString(result);
  }
  ScopedKeyHandle scoped_key(*factory_.get(), signing_key);
  std::string signature;
  session->SetEntityAuthorizationValue(key_authorization);
  result = utility->Sign(scoped_key.get(), TPM_ALG_NULL, TPM_ALG_NULL,
                         std::string(32, 'a'), session->GetDelegate(),
                         &signature);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error using key to sign: " << GetErrorString(result);
    return false;
  }
  result  = utility->Verify(scoped_key.get(), TPM_ALG_NULL, TPM_ALG_NULL,
                            std::string(32, 'a'), signature, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error using key to verify: " << GetErrorString(result);
    return false;
  }
  return true;
}

bool TrunksClientTest::DecryptTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  std::string key_authorization("decrypt");
  std::string key_blob;
  session->SetEntityAuthorizationValue("");
  result = utility->CreateRSAKeyPair(
      TpmUtility::AsymmetricKeyUsage::kDecryptKey, 2048, 0x10001,
      key_authorization, "", session->GetDelegate(), &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating decrypt key: " << GetErrorString(result);
    return false;
  }
  TPM_HANDLE decrypt_key;
  result = utility->LoadKey(key_blob, session->GetDelegate(), &decrypt_key);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading decrypt key: " << GetErrorString(result);
  }
  ScopedKeyHandle scoped_key(*factory_.get(), decrypt_key);
  return PerformRSAEncrpytAndDecrpyt(scoped_key.get(),
                                     key_authorization,
                                     session.get());
}

bool TrunksClientTest::ImportTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  crypto::ScopedRSA rsa(RSA_generate_key(2048, 0x10001, nullptr, nullptr));
  CHECK(rsa.get());
  std::string modulus(BN_num_bytes(rsa.get()->n), 0);
  BN_bn2bin(rsa.get()->n,
            reinterpret_cast<unsigned char*>(string_as_array(&modulus)));
  std::string prime_factor(BN_num_bytes(rsa.get()->p), 0);
  BN_bn2bin(rsa.get()->p,
            reinterpret_cast<unsigned char*>(string_as_array(&prime_factor)));

  std::string key_blob;
  std::string key_authorization("import");
  session->SetEntityAuthorizationValue("");
  result = utility->ImportRSAKey(
      TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey, modulus, 0x10001,
      prime_factor, key_authorization, session->GetDelegate(), &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error importing key into TPM: " << GetErrorString(result);
    return false;
  }
  TPM_HANDLE key_handle;
  result = utility->LoadKey(key_blob, session->GetDelegate(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading key into TPM: " << GetErrorString(result);
    return false;
  }
  ScopedKeyHandle scoped_key(*factory_.get(), key_handle);
  return PerformRSAEncrpytAndDecrpyt(scoped_key.get(), key_authorization,
                                     session.get());
}

bool TrunksClientTest::AuthChangeTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  std::string key_authorization("new_pass");
  std::string key_blob;
  session->SetEntityAuthorizationValue("");
  result = utility->CreateRSAKeyPair(
      TpmUtility::AsymmetricKeyUsage::kDecryptKey, 2048, 0x10001,
      "old_pass", "", session->GetDelegate(), &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating change auth key: " << GetErrorString(result);
    return false;
  }
  TPM_HANDLE key_handle;
  result = utility->LoadKey(key_blob, session->GetDelegate(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading change auth key: " << GetErrorString(result);
  }
  ScopedKeyHandle scoped_key(*factory_.get(), key_handle);
  session->SetEntityAuthorizationValue("old_pass");
  result = utility->ChangeKeyAuthorizationData(key_handle, key_authorization,
                                               session->GetDelegate(),
                                               &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error changing auth data: " << GetErrorString(result);
    return false;
  }
  session->SetEntityAuthorizationValue("");
  result = utility->LoadKey(key_blob, session->GetDelegate(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reloading key: " << GetErrorString(result);
    return false;
  }
  scoped_key.reset(key_handle);
  return PerformRSAEncrpytAndDecrpyt(scoped_key.get(), key_authorization,
                                     session.get());
}


bool TrunksClientTest::SimplePolicyTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<PolicySession> policy_session = factory_->GetPolicySession();
  TPM_RC result;
  result = policy_session->StartUnboundSession(false);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting policy session: " << GetErrorString(result);
    return false;
  }
  result = policy_session->PolicyAuthValue();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to auth value knowledge: "
               << GetErrorString(result);
    return false;
  }
  std::string policy_digest(32, 0);
  result = policy_session->GetDigest(&policy_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(result);
    return false;
  }
  // Now that we have the digest, we can close the policy session and use hmac.
  policy_session.reset();

  scoped_ptr<HmacSession> hmac_session = factory_->GetHmacSession();
  result = hmac_session->StartUnboundSession(false);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }

  std::string key_blob;
  result = utility->CreateRSAKeyPair(
      TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey,
      2048, 0x10001, "password", policy_digest, hmac_session->GetDelegate(),
      &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating RSA key: " << GetErrorString(result);
    return false;
  }

  TPM_HANDLE key_handle;
  result = utility->LoadKey(key_blob, hmac_session->GetDelegate(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading RSA key: " << GetErrorString(result);
    return false;
  }
  ScopedKeyHandle scoped_key(*factory_.get(), key_handle);

  // Now we can reset the hmac_session.
  hmac_session.reset();

  policy_session = factory_->GetPolicySession();
  result = policy_session->StartUnboundSession(true);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting policy session: " << GetErrorString(result);
    return false;
  }
  result = policy_session->PolicyAuthValue();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to auth value knowledge: "
               << GetErrorString(result);
    return false;
  }
  std::string signature;
  policy_session->SetEntityAuthorizationValue("password");
  result = utility->Sign(scoped_key.get(), TPM_ALG_NULL, TPM_ALG_NULL,
                         std::string(32, 0), policy_session->GetDelegate(),
                         &signature);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error signing using RSA key: " << GetErrorString(result);
    return false;
  }
  result = utility->Verify(scoped_key.get(), TPM_ALG_NULL, TPM_ALG_NULL,
                           std::string(32, 0), signature, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error verifying using RSA key: " << GetErrorString(result);
    return false;
  }
  // TODO(usanghi): Investigate resetting of policy_digest. crbug.com/486185.
  result = policy_session->PolicyAuthValue();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to auth value knowledge: "
               << GetErrorString(result);
    return false;
  }
  std::string ciphertext;
  policy_session->SetEntityAuthorizationValue("");
  result = utility->AsymmetricEncrypt(scoped_key.get(), TPM_ALG_NULL,
                                      TPM_ALG_NULL, "plaintext",
                                      policy_session->GetDelegate(),
                                      &ciphertext);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error encrypting using RSA key: " << GetErrorString(result);
    return false;
  }
  result = policy_session->PolicyAuthValue();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to auth value knowledge: "
               << GetErrorString(result);
    return false;
  }
  std::string plaintext;
  policy_session->SetEntityAuthorizationValue("password");
  result = utility->AsymmetricDecrypt(scoped_key.get(), TPM_ALG_NULL,
                                      TPM_ALG_NULL, ciphertext,
                                      policy_session->GetDelegate(),
                                      &plaintext);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error encrypting using RSA key: " << GetErrorString(result);
    return false;
  }
  if (plaintext.compare("plaintext") != 0) {
    LOG(ERROR) << "Plaintext changed after encrypt + decrypt.";
    return false;
  }
  return true;
}

bool TrunksClientTest::PCRTest() {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  // We are using PCR 2 because it is currently not used by ChromeOS.
  uint32_t pcr_index = 2;
  std::string extend_data("data");
  std::string old_data;
  session->SetEntityAuthorizationValue("");
  result = utility->ReadPCR(pcr_index, &old_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading from PCR: " << GetErrorString(result);
    return false;
  }
  result = utility->ExtendPCR(pcr_index, extend_data, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error extending PCR value: " << GetErrorString(result);
    return false;
  }
  std::string pcr_data;
  result = utility->ReadPCR(pcr_index, &pcr_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading from PCR: " << GetErrorString(result);
    return false;
  }
  std::string hashed_extend_data = crypto::SHA256HashString(extend_data);
  std::string expected_pcr_data =
      crypto::SHA256HashString(old_data + hashed_extend_data);
  if (pcr_data.compare(expected_pcr_data) != 0) {
    LOG(ERROR) << "PCR data does not match expected value.";
    return false;
  }
  return true;
}

bool TrunksClientTest::NvramTest(const std::string& owner_password) {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  scoped_ptr<HmacSession> session = factory_->GetHmacSession();
  TPM_RC result = session->StartUnboundSession(true /* enable encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return false;
  }
  uint32_t index = 1;
  session->SetEntityAuthorizationValue(owner_password);
  std::string nv_data("nv_data");
  result = utility->DefineNVSpace(index, nv_data.size(),
                                  session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error defining nvram: " << GetErrorString(result);
    return false;
  }
  session->SetEntityAuthorizationValue(owner_password);
  result = utility->WriteNVSpace(index, 0, nv_data, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error writing nvram: " << GetErrorString(result);
    return false;
  }
  std::string new_nvdata;
  session->SetEntityAuthorizationValue("");
  result = utility->ReadNVSpace(index, 0, nv_data.size(),
                            &new_nvdata, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading nvram: " << GetErrorString(result);
    return false;
  }
  if (nv_data.compare(new_nvdata) != 0) {
    LOG(ERROR) << "NV space had different data than was written.";
    return false;
  }
  result = utility->LockNVSpace(index, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error locking nvram: " << GetErrorString(result);
    return false;
  }
  result = utility->ReadNVSpace(index, 0, nv_data.size(),
                            &new_nvdata, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading nvram: " << GetErrorString(result);
    return false;
  }
  if (nv_data.compare(new_nvdata) != 0) {
    LOG(ERROR) << "NV space had different data than was written.";
    return false;
  }
  session->SetEntityAuthorizationValue(owner_password);
  result = utility->WriteNVSpace(index, 0, nv_data, session->GetDelegate());
  if (result == TPM_RC_SUCCESS) {
    LOG(ERROR) << "Wrote nvram after locking: " << GetErrorString(result);
    return false;
  }
  session->SetEntityAuthorizationValue(owner_password);
  result = utility->DestroyNVSpace(index, session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error destroying nvram: " << GetErrorString(result);
    return false;
  }
  return true;
}

bool TrunksClientTest::PerformRSAEncrpytAndDecrpyt(
    TPM_HANDLE key_handle,
    const std::string& key_authorization,
    HmacSession* session) {
  scoped_ptr<TpmUtility> utility = factory_->GetTpmUtility();
  std::string ciphertext;
  session->SetEntityAuthorizationValue("");
  TPM_RC result = utility->AsymmetricEncrypt(key_handle, TPM_ALG_NULL,
                                             TPM_ALG_NULL, "plaintext",
                                             session->GetDelegate(),
                                             &ciphertext);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error using key to encrypt: " << GetErrorString(result);
    return false;
  }
  std::string plaintext;
  session->SetEntityAuthorizationValue(key_authorization);
  result = utility->AsymmetricDecrypt(key_handle, TPM_ALG_NULL,
                                      TPM_ALG_NULL, ciphertext,
                                      session->GetDelegate(), &plaintext);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error using key to decrypt: " << GetErrorString(result);
    return false;
  }
  if (plaintext.compare("plaintext") != 0) {
    LOG(ERROR) << "Plaintext changed after encrypt + decrypt.";
    return false;
  }
  return true;
}

}  // namespace trunks
