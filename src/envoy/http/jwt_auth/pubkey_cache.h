/* Copyright 2018 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <unordered_map>

#include "src/envoy/http/jwt_auth/config.pb.h"
#include "src/envoy/http/jwt_auth/jwt.h"

namespace Envoy {
namespace Http {
namespace JwtAuth {
namespace {
// Default cache expiration time in 5 minutes.
const int kPubkeyCacheExpirationSec = 600;

const std::string kHTTPSchemePrefix("http://");

const std::string kHTTPSSchemePrefix("https://");
}  // namespace

// Struct to hold an issuer cache item.
class PubkeyCacheItem {
 public:
  PubkeyCacheItem(const Config::JWT& jwt_config) : jwt_config_(jwt_config) {
    // Convert proto repeated fields to std::set.
    for (const auto& aud : jwt_config_.audiences()) {
      int beg = 0;
      int end = aud.length() - 1;
      // Searches protocol scheme prefix and trailing slash from aud, and
      // stores aud into audiences_ without these prefix and suffix.
      if (SanitizeAudience(aud, beg, end)) {
        audiences_.insert(aud.substr(beg, end - beg + 1));
      }
    }
  }

  // Return true if cached pubkey is expired.
  bool Expired() const {
    return std::chrono::steady_clock::now() >= expiration_time_;
  }

  // Get the JWT config.
  const Config::JWT& jwt_config() const { return jwt_config_; }

  // Get the pubkey object.
  const Pubkeys* pubkey() const { return pubkey_.get(); }

  // Check if an audience is allowed.
  bool IsAudienceAllowed(const std::vector<std::string>& jwt_audiences) {
    if (audiences_.empty()) {
      return true;
    }
    for (const auto& aud : jwt_audiences) {
      int beg = 0;
      int end = aud.length() - 1;
      // Searches protocol scheme prefix and trailing slash from aud, and
      // stores aud into audiences_ without these prefix and suffix.
      if (SanitizeAudience(aud, beg, end) &&
          audiences_.find(aud.substr(beg, end - beg + 1)) != audiences_.end()) {
        return true;
      }
    }
    return false;
  }

  // Set a pubkey as string.
  Status SetKey(const std::string& pubkey_str) {
    auto pubkey = Pubkeys::CreateFrom(pubkey_str, Pubkeys::JWKS);
    if (pubkey->GetStatus() != Status::OK) {
      return pubkey->GetStatus();
    }
    pubkey_ = std::move(pubkey);

    expiration_time_ = std::chrono::steady_clock::now();
    if (jwt_config_.has_public_key_cache_duration()) {
      const auto& duration = jwt_config_.public_key_cache_duration();
      expiration_time_ += std::chrono::seconds(duration.seconds()) +
                          std::chrono::nanoseconds(duration.nanos());
    } else {
      expiration_time_ += std::chrono::seconds(kPubkeyCacheExpirationSec);
    }
    return Status::OK;
  }

 private:
  // Searches "http://" or "https://" prefix in aud, and stores the position
  // after protocol scheme prefix in beg. Searches trailing slash in aud, and
  // stores the position before trailing slash in end. Returns true if aud has
  // characters other than prefix and suffix.
  bool SanitizeAudience(const std::string& aud, int& beg, int& end) {
    // Point beg to first character after protocol scheme prefix in audience.
    if (aud.compare(0, kHTTPSchemePrefix.length(), kHTTPSchemePrefix) == 0) {
      beg = kHTTPSchemePrefix.length();
    } else if (aud.compare(0, kHTTPSSchemePrefix.length(), kHTTPSSchemePrefix) == 0) {
      beg = kHTTPSSchemePrefix.length();
    }
    // Point end to trailing slash in aud.
    if (end >= 0 && aud[end] == '/') {
      --end;
    }
    return end >= beg;
  }

  // The issuer config
  const Config::JWT& jwt_config_;
  // Use set for fast lookup
  std::set<std::string> audiences_;
  // The generated pubkey object.
  std::unique_ptr<Pubkeys> pubkey_;
  // The pubkey expiration time.
  std::chrono::steady_clock::time_point expiration_time_;
};

// Pubkey cache
class PubkeyCache {
 public:
  // Load the config from envoy config.
  PubkeyCache(const Config::AuthFilterConfig& config) {
    for (const auto& jwt : config.jwts()) {
      pubkey_cache_map_.emplace(jwt.issuer(), jwt);
    }
  }

  // Lookup issuer cache map.
  PubkeyCacheItem* LookupByIssuer(const std::string& name) {
    auto it = pubkey_cache_map_.find(name);
    if (it == pubkey_cache_map_.end()) {
      return nullptr;
    }
    return &it->second;
  }

 private:
  // The public key cache map indexed by issuer.
  std::unordered_map<std::string, PubkeyCacheItem> pubkey_cache_map_;
};

}  // namespace JwtAuth
}  // namespace Http
}  // namespace Envoy
