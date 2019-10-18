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

#include <unordered_map>

#include "absl/strings/string_view.h"
#include "extensions/common/node_info.pb.h"

#ifndef NULL_PLUGIN

#include "proxy_wasm_intrinsics.h"

#else  // NULL_PLUGIN

#include "extensions/common/wasm/null/null_plugin.h"

#endif  // NULL_PLUGIN

namespace Wasm {
namespace Common {

const size_t DefaultNodeCacheMaxSize = 500;

class NodeInfoCache {
 public:
  // Fetches and caches Peer information by peerId
  // TODO Remove this when it is cheap to directly get it from StreamInfo.
  // At present this involves de-serializing to google.Protobuf.Struct and
  // then another round trip to NodeInfo. This Should at most hold N entries.
  // Node is owned by the cache. Do not store a reference.
  const wasm::common::NodeInfo& getPeerById(
      absl::string_view peer_metadata_id_key,
      absl::string_view peer_metadata_key);

  // Fetches and caches peer information by peerId, also gets peer metadata id.
  const wasm::common::NodeInfo& getPeerById(
      absl::string_view peer_metadata_id_key,
      absl::string_view peer_metadata_key, std::string* peer_metadata_id);

  inline void setMaxCacheSize(size_t size) {
    max_cache_size_ = size == 0 ? DefaultNodeCacheMaxSize : size;
  }

 private:
  std::unordered_map<std::string, wasm::common::NodeInfo> cache_;
  size_t max_cache_size_ = DefaultNodeCacheMaxSize;
};

}  // namespace Common
}  // namespace Wasm
