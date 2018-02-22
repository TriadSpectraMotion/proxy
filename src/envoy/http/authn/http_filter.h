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

#include "src/envoy/http/authn/authn_store.h"

#include "common/common/logger.h"
#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Http {

// The authentication filter.
class AuthnFilter : public StreamDecoderFilter,
                    public Logger::Loggable<Logger::Id::http> {
 public:
  AuthnFilter(Upstream::ClusterManager& cm, Auth::AuthnStore& store);
  ~AuthnFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool) override;
  FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  FilterTrailersStatus decodeTrailers(HeaderMap&) override;
  void setDecoderFilterCallbacks(
      StreamDecoderFilterCallbacks& callbacks) override;

 private:
  // The callback funcion.
  StreamDecoderFilterCallbacks* decoder_callbacks_;
  Upstream::ClusterManager& cm_;
  Auth::AuthnStore& store_;

  // The state of the filter handling a HTTP request
  enum State { Initial, HandleHeaders, HandleData, HandleTrailers };
  State state_ = Initial;
};

}  // namespace Http
}  // namespace Envoy
