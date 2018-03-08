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

#include "authentication/v1alpha1/policy.pb.h"
#include "src/envoy/http/authn/authenticator_base.h"

namespace Envoy {
namespace Http {
namespace IstioAuthN {

class OriginAuthenticator : public AuthenticatorBase {
 public:
  OriginAuthenticator(
      FilterContext* filter_context, const DoneCallback& done_callback,
      const istio::authentication::v1alpha1::CredentialRule& credential_rule);

  void run() override;

 protected:
  void runMethod(
      const istio::authentication::v1alpha1::OriginAuthenticationMethod& method,
      const MethodDoneCallback& callback);

  void onMethodDone(const Payload* payload, bool success);

 private:
  const istio::authentication::v1alpha1::CredentialRule& credential_rule_;
  int method_index_{0};
};

}  // namespace IstioAuthN
}  // namespace Http
}  // namespace Envoy
