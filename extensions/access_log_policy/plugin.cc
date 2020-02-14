/* Copyright 2019 Istio Authors. All Rights Reserved.
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

#include "extensions/access_log_policy/plugin.h"

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "extensions/common/istio_dimensions.h"
#include "extensions/common/node_info.pb.h"
#include "google/protobuf/util/json_util.h"

#ifndef NULL_PLUGIN

#include "base64.h"

#else

#include "common/common/base64.h"
namespace Envoy {
namespace Extensions {
namespace Wasm {
namespace AccessLogPolicy {
namespace Plugin {
using namespace ::Envoy::Extensions::Common::Wasm::Null::Plugin;
using NullPluginRegistry =
    ::Envoy::Extensions::Common::Wasm::Null::NullPluginRegistry;
using google::protobuf::util::JsonParseOptions;
using google::protobuf::util::Status;

NULL_PLUGIN_REGISTRY;

#endif

namespace {

bool setFilterStateValue(bool log) {
  auto r = setFilterStateStringValue(::Wasm::Common::kAccessLogPolicyKey,
                                     log ? "yes" : "no");
  if (r != WasmResult::Ok) {
    logWarn(toString(r));
    return false;
  }
  return true;
}

}  // namespace

constexpr long long kDefaultLogWindowDurationNanoseconds =
    43200000000000;  // 12h

constexpr StringView kSource = "source";
constexpr StringView kAddress = "address";
constexpr StringView kConnection = "connection";
constexpr StringView kUriSanPeerCertificate = "uri_san_peer_certificate";
constexpr StringView kRootContextId = "accesslog_inbound";

static RegisterContextFactory register_AccessLogPolicy(
    CONTEXT_FACTORY(PluginContext), ROOT_FACTORY(PluginRootContext),
    kRootContextId);

bool PluginRootContext::onConfigure(size_t) {
  if (::Wasm::Common::TrafficDirection::Inbound !=
      ::Wasm::Common::getTrafficDirection()) {
    logError("ASM Acess Logging Policy is an inbound filter only.");
    return false;
  }
  WasmDataPtr configuration = getConfiguration();
  JsonParseOptions json_options;
  Status status =
      JsonStringToMessage(configuration->toString(), &config_, json_options);
  if (status != Status::OK) {
    logWarn("Cannot parse Stackdriver plugin configuration JSON string " +
            configuration->toString() + ", " + status.message().ToString());
    return false;
  }

  if (config_.has_log_window_duration()) {
    log_time_duration_nanos_ =
        ::google::protobuf::util::TimeUtil::DurationToNanoseconds(
            config_.log_window_duration());
  } else {
    log_time_duration_nanos_ = kDefaultLogWindowDurationNanoseconds;
  }

  if (config_.max_client_cache_size() > 0) {
    max_client_cache_size_ = config_.max_client_cache_size();
  }

  return true;
}

void PluginRootContext::updateLastLogTimeNanos(const IstioDimensions& key,
                                               long long last_log_time_nanos) {
  if (int32_t(cache_.size()) > max_client_cache_size_) {
    auto it = cache_.begin();
    cache_.erase(cache_.begin(), std::next(it, max_client_cache_size_ / 4));
    logDebug(absl::StrCat("cleaned cache, new cache_size:", cache_.size()));
  }
  cache_[key] = last_log_time_nanos;
}

void PluginContext::onLog() {
  // Check if request is a failure.
  int64_t response_code = 0;
  // TODO(gargnupur): Add check for gRPC status too.
  getValue({"response", "code"}, &response_code);
  // If request is a failure, log it.
  if (response_code != 200) {
    LOG_TRACE("Setting logging to true as we got error log");
    setFilterStateValue(true);
    return;
  }

  // If request is not a failure, check cache to see if it should be logged or
  // not, based on last time a successful request was logged for this client ip
  // and principal combination.
  std::string source_ip = "";
  getValue({kSource, kAddress}, &source_ip);
  std::string source_principal = "";
  getValue({kConnection, kUriSanPeerCertificate}, &source_principal);
  istio_dimensions_.set_downstream_ip(source_ip);
  istio_dimensions_.set_source_principal(source_principal);
  long long last_log_time_nanos = lastLogTimeNanos();
  auto cur = static_cast<long long>(getCurrentTimeNanoseconds());
  if ((cur - last_log_time_nanos) > logTimeDurationNanos()) {
    LOG_TRACE(absl::StrCat(
        "Setting logging to true as its outside of log windown. SourceIp: ",
        source_ip, " SourcePrincipal: ", source_principal,
        " Window: ", logTimeDurationNanos()));
    if (setFilterStateValue(true)) {
      updateLastLogTimeNanos(cur);
    }
    return;
  }

  setFilterStateValue(false);
}

#ifdef NULL_PLUGIN
}  // namespace Plugin
}  // namespace AccessLogPolicy
}  // namespace Wasm
}  // namespace Extensions
}  // namespace Envoy
#endif
