/* Copyright 2020 Istio Authors. All Rights Reserved.
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

#include "absl/strings/str_cat.h"
#include "proxy_wasm_intrinsics.h"
#include "src/envoy/http/authn_wasm/authenticator/peer.h"

namespace Envoy {
namespace Wasm {
namespace Http {
namespace AuthN {

PeerAuthenticator::PeerAuthenticator(
    FilterContextPtr filter_context,
    const istio::authentication::v1alpha1::Policy& policy)
    : AuthenticatorBase(filter_context), policy_(policy) {}

bool PeerAuthenticator::run(istio::authn::Payload* payload) {
  bool success = false;
  if (policy_.peers_size() == 0) {
    logDebug("No method defined. Skip source authentication.");
    success = true;
    return success;
  }
  for (const auto& method : policy_.peers()) {
    switch (method.params_case()) {
      case istio::authentication::v1alpha1::PeerAuthenticationMethod::
          ParamsCase::kMtls:
        success = validateX509(method.mtls(), payload);
        break;
      case istio::authentication::v1alpha1::PeerAuthenticationMethod::
          ParamsCase::kJwt:
        success = validateJwt(method.jwt(), payload);
        break;
      default:
        logError(absl::StrCat("Unknown peer authentication param ",
                              method.DebugString()));
        success = false;
        break;
    }

    if (success) {
      break;
    }
  }

  if (success) {
    filterContext()->setPeerResult(payload);
  }

  return success;
}

}  // namespace AuthN
}  // namespace Http
}  // namespace Wasm
}  // namespace Envoy
