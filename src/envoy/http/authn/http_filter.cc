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

#include "src/envoy/http/authn/http_filter.h"
#include "common/http/utility.h"
#include "src/envoy/http/authn/origin_authenticator.h"
#include "src/envoy/http/authn/peer_authenticator.h"
#include "src/envoy/utils/utils.h"

namespace iaapi = istio::authentication::v1alpha1;

namespace Envoy {
namespace Http {

AuthenticationFilter::AuthenticationFilter(
    const istio::authentication::v1alpha1::Policy& policy)
    : policy_(policy) {}

AuthenticationFilter::~AuthenticationFilter() {}

void AuthenticationFilter::onDestroy() {
  ENVOY_LOG(debug, "Called AuthenticationFilter : {}", __func__);
}

FilterHeadersStatus AuthenticationFilter::decodeHeaders(HeaderMap& headers,
                                                        bool) {
  ENVOY_LOG(debug, "Called AuthenticationFilter : {}", __func__);
  state_ = IstioAuthN::State::PROCESSING;

  setHeaders(&headers);

  authenticator_.reset(createPeerAuthenticator(
      this, [this](bool success) { onPeerAuthenticationDone(success); }));
  authenticator_->run();

  if (state_ == IstioAuthN::State::COMPLETE) {
    return FilterHeadersStatus::Continue;
  }

  stopped_ = true;
  return FilterHeadersStatus::StopIteration;
}

const Network::Connection* AuthenticationFilter::connection() const {
  return decoder_callbacks_->connection();
}

void AuthenticationFilter::onPeerAuthenticationDone(bool success) {
  ENVOY_LOG(debug, "{}: success = {}", __func__, success);
  if (success) {
    authenticator_.reset(createOriginAuthenticator(
        this, [this](bool success) { onOriginAuthenticationDone(success); }));
    authenticator_->run();
  } else {
    rejectRequest("Peer authentication failed.");
  }
}
void AuthenticationFilter::onOriginAuthenticationDone(bool success) {
  ENVOY_LOG(debug, "{}: success = {}", __func__, success);
  if (success) {
    continueDecoding();
  } else {
    rejectRequest("Origin authentication failed.");
  }
}

FilterDataStatus AuthenticationFilter::decodeData(Buffer::Instance&, bool) {
  ENVOY_LOG(debug, "Called AuthenticationFilter : {}", __func__);
  ENVOY_LOG(debug,
            "Called AuthenticationFilter : {} FilterDataStatus::Continue;",
            __FUNCTION__);
  return FilterDataStatus::Continue;
}

FilterTrailersStatus AuthenticationFilter::decodeTrailers(HeaderMap&) {
  ENVOY_LOG(debug, "Called AuthenticationFilter : {}", __func__);
  if (state_ == IstioAuthN::State::PROCESSING) {
    return FilterTrailersStatus::StopIteration;
  }
  return FilterTrailersStatus::Continue;
}

void AuthenticationFilter::setDecoderFilterCallbacks(
    StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG(debug, "Called AuthenticationFilter : {}", __func__);
  decoder_callbacks_ = &callbacks;
}

void AuthenticationFilter::continueDecoding() {
  if (state_ != IstioAuthN::State::PROCESSING) {
    ENVOY_LOG(error, "State {} is not PROCESSING.", state_);
    return;
  }
  state_ = IstioAuthN::State::COMPLETE;
  if (stopped_) {
    decoder_callbacks_->continueDecoding();
  }
}

void AuthenticationFilter::rejectRequest(const std::string& message) {
  if (state_ != IstioAuthN::State::PROCESSING) {
    ENVOY_LOG(error, "State {} is not PROCESSING.", state_);
    return;
  }
  state_ = IstioAuthN::State::REJECTED;
  Utility::sendLocalReply(*decoder_callbacks_, false, Http::Code::Unauthorized,
                          message);
}

IstioAuthN::AuthenticatorBase* AuthenticationFilter::createPeerAuthenticator(
    IstioAuthN::FilterContext* filter_context,
    const IstioAuthN::AuthenticatorBase::DoneCallback& done_callback) {
  return new IstioAuthN::PeerAuthenticator(filter_context, done_callback,
                                           policy_);
}

IstioAuthN::AuthenticatorBase* AuthenticationFilter::createOriginAuthenticator(
    IstioAuthN::FilterContext* filter_context,
    const IstioAuthN::AuthenticatorBase::DoneCallback& done_callback) {
  const auto& rule = IstioAuthN::findCredentialRuleOrDefault(
      policy_, authenticationResult().peer_user());
  return new IstioAuthN::OriginAuthenticator(filter_context, done_callback,
                                             rule);
}

}  // namespace Http
}  // namespace Envoy
