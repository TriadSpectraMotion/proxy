/* Copyright 2016 Google Inc. All Rights Reserved.
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
#ifndef API_MANAGER_MIXER_MIXER_H_
#define API_MANAGER_MIXER_MIXER_H_

#include "contrib/endpoints/include/api_manager/env_interface.h"
#include "contrib/endpoints/src/api_manager/service_control/interface.h"

namespace google {
namespace api_manager {
namespace mixer {

// This implementation uses service-control-client-cxx module.
class Mixer : public service_control::Interface {
 public:
  static service_control::Interface* Create(ApiManagerEnvInterface* env);

  virtual ~Mixer();

  virtual utils::Status Report(const service_control::ReportRequestInfo& info);

  virtual void Check(
      const service_control::CheckRequestInfo& info,
      cloud_trace::CloudTraceSpan* parent_span,
      std::function<void(utils::Status,
                         const service_control::CheckResponseInfo&)>
          on_done);

  virtual utils::Status Init();
  virtual utils::Status Close();

  virtual utils::Status GetStatistics(service_control::Statistics* stat) const;

 private:
  // The constructor.
  Mixer(ApiManagerEnvInterface* env);

  // The Api Manager environment interface.
  ApiManagerEnvInterface* env_;
  int64_t request_index_;
};

}  // namespace mixer
}  // namespace api_manager
}  // namespace google

#endif  // API_MANAGER_MIXER_MIXER_H_
