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
#include "common/common/logger.h"
#include "server/config/network/http_connection_manager.h"
#include "src/envoy/http/authn/authenticator_base.h"

namespace Envoy {
namespace Http {
namespace IstioAuthN {
enum State { INIT, PROCESSING, COMPLETE, REJECTED };
}  // namespace IstioAuthN

// The authentication filter.
class AuthenticationFilter : public StreamDecoderFilter, public FilterContext {
 public:
  AuthenticationFilter(const istio::authentication::v1alpha1::Policy& config);
  ~AuthenticationFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool) override;
  FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  FilterTrailersStatus decodeTrailers(HeaderMap&) override;
  void setDecoderFilterCallbacks(
      StreamDecoderFilterCallbacks& callbacks) override;

  // Implement FilterContext
  const Network::Connection* connection() const override;

 protected:
  // Callback for peer authenticator.
  void onPeerAuthenticationDone(bool success);

  // Callback for origin authenticator.
  void onOriginAuthenticationDone(bool success);

  // Convenient function to call decoder_callbacks_ only when stopped_ is true.
  void continueDecoding();

  // Convenient function to reject request.
  void rejectRequest(const std::string& message);

  // Creates peer authenticator. This is made virtual function for
  // testing.
  virtual AuthenticatorBase* createPeerAuthenticator(
      FilterContext* filter_context,
      const AuthenticatorBase::DoneCallback& done_callback);

  // Creates origin authenticator.
  virtual AuthenticatorBase* createOriginAuthenticator(
      FilterContext* filter_context,
      const AuthenticatorBase::DoneCallback& done_callback);

 private:
  // Store the config.
  const istio::authentication::v1alpha1::Policy& policy_;

  StreamDecoderFilterCallbacks* decoder_callbacks_{};

  // Holds the state of the filter.
  IstioAuthN::State state_{IstioAuthN::State::INIT};

  // Indicates filter is 'stopped', thus (decoder_callbacks_) continueDecoding
  // should be called.
  bool stopped_{false};

  std::unique_ptr<AuthenticatorBase> peer_authenticator_;
  std::unique_ptr<AuthenticatorBase> origin_authenticator_;
};

}  // namespace Http
}  // namespace Envoy
