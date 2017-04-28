// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////
//
#include "contrib/endpoints/src/api_manager/api_manager_impl.h"

#include "contrib/endpoints/src/api_manager/check_workflow.h"
#include "contrib/endpoints/src/api_manager/request_handler.h"

using ::google::api_manager::proto::ServerConfig;

namespace google {
namespace api_manager {

ApiManagerImpl::ApiManagerImpl(std::unique_ptr<ApiManagerEnvInterface> env,
                               const std::string& server_config)
    : global_context_(
          new context::GlobalContext(std::move(env), server_config)) {
  check_workflow_ = std::unique_ptr<CheckWorkflow>(new CheckWorkflow);
  check_workflow_->RegisterAll();
}

void ApiManagerImpl::AddConfig(std::unique_ptr<Config> config) {
  service_context_ = std::make_shared<context::ServiceContext>(
      global_context_, std::move(config));
}

std::unique_ptr<RequestHandlerInterface> ApiManagerImpl::CreateRequestHandler(
    std::unique_ptr<Request> request_data) {
  return std::unique_ptr<RequestHandlerInterface>(new RequestHandler(
      check_workflow_, service_context_, std::move(request_data)));
}

std::shared_ptr<ApiManager> ApiManagerFactory::GetOrCreateApiManager(
    std::unique_ptr<ApiManagerEnvInterface> env,
    const std::string& service_config, const std::string& server_config) {
  std::unique_ptr<Config> config = Config::Create(env.get(), service_config);
  if (config == nullptr) {
    return nullptr;
  }

  ApiManagerMap::iterator it;
  std::tie(it, std::ignore) = api_manager_map_.emplace(
      config->service_name(), std::weak_ptr<ApiManager>());
  std::shared_ptr<ApiManager> result = it->second.lock();

  if (!result) {
    // TODO: Handle the case where the caller gives us a different
    // config with the same service name.
    auto api_impl = new ApiManagerImpl(std::move(env), server_config);
    api_impl->AddConfig(std::move(config));
    result = std::shared_ptr<ApiManager>(api_impl);
    it->second = result;
  }

  return result;
}

}  // namespace api_manager
}  // namespace google
