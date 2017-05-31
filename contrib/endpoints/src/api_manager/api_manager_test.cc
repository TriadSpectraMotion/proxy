// Copyright 2017 Google Inc. All Rights Reserved.
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
#include "contrib/endpoints/include/api_manager/api_manager.h"
#include "contrib/endpoints/src/api_manager/mock_api_manager_environment.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::Return;

using ::google::api_manager::utils::Status;

namespace google {
namespace api_manager {

namespace {

const char kServerConfigWithServiceNameConfigId[] = R"(
{
  "google_authentication_secret": "{}",
  "metadata_server_config": {
    "enabled": true,
    "url": "http://localhost"
  },
  "service_name": "bookstore.test.appspot.com",
  "config_id": "2017-05-01r0"
}
)";

const char kServiceConfig1[] = R"(
{
  "name": "bookstore.test.appspot.com",
  "title": "Bookstore",
  "control": {
    "environment": "servicecontrol.googleapis.com"
  },
  "id": "2017-05-01r0"
}
)";

const char kServiceConfig2[] = R"(
{
  "name": "different.test.appspot.com",
  "title": "Bookstore",
  "control": {
    "environment": "servicecontrol.googleapis.com"
  },
  "id": "2017-05-01r0"
}
)";

const char kGceMetadataWithServiceNameAndConfigId[] = R"(
{
  "project": {
    "projectId": "test-project"
  },
  "instance": {
    "attributes":{
      "endpoints-service-name": "service_name_from_metadata",
      "endpoints-service-config-id":"2017-05-01r1"
    }
  }
}
)";

const char kServiceForStatistics[] =
    "name: \"service-name\"\n"
    "control: {\n"
    "  environment: \"http://127.0.0.1:8081\"\n"
    "}\n";

class ApiManagerTest : public ::testing::Test {
 protected:
  std::shared_ptr<ApiManager> MakeApiManager(
      std::unique_ptr<ApiManagerEnvInterface> env, const char *service_config);
  std::shared_ptr<ApiManager> MakeApiManager(
      std::unique_ptr<ApiManagerEnvInterface> env, const char *service_config,
      const char *server_config);

 private:
  ApiManagerFactory factory_;
};

std::shared_ptr<ApiManager> ApiManagerTest::MakeApiManager(
    std::unique_ptr<ApiManagerEnvInterface> env, const char *service_config) {
  return factory_.CreateApiManager(std::move(env), service_config, "");
}

std::shared_ptr<ApiManager> ApiManagerTest::MakeApiManager(
    std::unique_ptr<ApiManagerEnvInterface> env, const char *service_config,
    const char *server_config) {
  return factory_.CreateApiManager(std::move(env), service_config,
                                   server_config);
}

TEST_F(ApiManagerTest, EnvironmentLogging) {
  MockApiManagerEnvironment env;

  ::testing::InSequence s;
  EXPECT_CALL(env, Log(ApiManagerEnvInterface::LogLevel::DEBUG, "debug log"));
  EXPECT_CALL(env, Log(ApiManagerEnvInterface::LogLevel::INFO, "info log"));
  EXPECT_CALL(env,
              Log(ApiManagerEnvInterface::LogLevel::WARNING, "warning log"));
  EXPECT_CALL(env, Log(ApiManagerEnvInterface::LogLevel::ERROR, "error log"));

  env.LogDebug("debug log");
  env.LogInfo("info log");
  env.LogWarning("warning log");
  env.LogError("error log");
}

TEST_F(ApiManagerTest, CorrectStatistics) {
  std::unique_ptr<ApiManagerEnvInterface> env(
      new ::testing::NiceMock<MockApiManagerEnvironment>());

  std::shared_ptr<ApiManager> api_manager(
      MakeApiManager(std::move(env), kServiceForStatistics));
  EXPECT_TRUE(api_manager);
  EXPECT_FALSE(api_manager->Enabled());
  api_manager->Init();
  EXPECT_TRUE(api_manager->Enabled());
  ApiManagerStatistics statistics;
  api_manager->GetStatistics(&statistics);
  const service_control::Statistics &service_control_stat =
      statistics.service_control_statistics;
  EXPECT_EQ(0, service_control_stat.total_called_checks);
  EXPECT_EQ(0, service_control_stat.send_checks_by_flush);
  EXPECT_EQ(0, service_control_stat.send_checks_in_flight);
  EXPECT_EQ(0, service_control_stat.total_called_reports);
  EXPECT_EQ(0, service_control_stat.send_reports_by_flush);
  EXPECT_EQ(0, service_control_stat.send_reports_in_flight);
  EXPECT_EQ(0, service_control_stat.send_report_operations);
}

// ApiManager instance should be initialized on ApiManager instance creation
TEST_F(ApiManagerTest, InitializedOnApiManagerInstanceCreation) {
  std::unique_ptr<MockApiManagerEnvironment> env(
      new ::testing::NiceMock<MockApiManagerEnvironment>());

  // ServiceManagement service should not be called
  EXPECT_CALL(*(env.get()), DoRunHTTPRequest(_))
      .Times(0)
      .WillOnce(Invoke([this](HTTPRequest *req) {
        EXPECT_TRUE(false);

      }));

  std::shared_ptr<ApiManager> api_manager(MakeApiManager(
      std::move(env), kServiceConfig1, kServerConfigWithServiceNameConfigId));

  // Already initialized
  EXPECT_TRUE(api_manager);
  EXPECT_EQ("UNKNOWN: Not initialized yet",
            api_manager->ConfigLoadingStatus().ToString());

  api_manager->Init();

  // No change is expected
  EXPECT_EQ("OK", api_manager->ConfigLoadingStatus().ToString());
  EXPECT_TRUE(api_manager->Enabled());
  EXPECT_EQ("2017-05-01r0", api_manager->service("2017-05-01r0").id());
}

// ApiManager was initialized by ConfigManager
TEST_F(ApiManagerTest, InitializedByConfigManager) {
  std::unique_ptr<MockApiManagerEnvironment> env(
      new ::testing::NiceMock<MockApiManagerEnvironment>());

  EXPECT_CALL(*(env.get()), DoRunHTTPRequest(_))
      .WillOnce(Invoke([this](HTTPRequest *req) {
        EXPECT_EQ(
            "https://servicemanagement.googleapis.com/v1/services/"
            "bookstore.test.appspot.com/configs/2017-05-01r0",
            req->url());
        req->OnComplete(Status::OK, {}, std::move(kServiceConfig1));
      }));

  std::shared_ptr<ApiManager> api_manager(
      MakeApiManager(std::move(env), "", kServerConfigWithServiceNameConfigId));

  EXPECT_TRUE(api_manager);
  EXPECT_EQ("UNKNOWN: Not initialized yet",
            api_manager->ConfigLoadingStatus().ToString());
  EXPECT_EQ("bookstore.test.appspot.com", api_manager->service_name());
  EXPECT_EQ("", api_manager->service("2017-05-01r0").id());

  api_manager->Init();

  // Successfully initialized by ConfigManager
  EXPECT_EQ("OK", api_manager->ConfigLoadingStatus().ToString());
  EXPECT_TRUE(api_manager->Enabled());
  EXPECT_EQ("2017-05-01r0", api_manager->service("2017-05-01r0").id());
}

// ApiManager initialization was failed by ConfigManager because of different
// service name
TEST_F(ApiManagerTest,
       InitializationFailedByConfigManagerWithDifferentServiceName) {
  std::unique_ptr<MockApiManagerEnvironment> env(
      new ::testing::NiceMock<MockApiManagerEnvironment>());

  EXPECT_CALL(*env.get(), DoRunHTTPRequest(_))
      .WillOnce(Invoke([this](HTTPRequest *req) {
        EXPECT_EQ(
            "https://servicemanagement.googleapis.com/v1/services/"
            "bookstore.test.appspot.com/configs/2017-05-01r0",
            req->url());
        req->OnComplete(Status::OK, {}, std::move(kServiceConfig2));
      }));

  std::shared_ptr<ApiManager> api_manager(
      MakeApiManager(std::move(env), "", kServerConfigWithServiceNameConfigId));

  EXPECT_TRUE(api_manager);
  EXPECT_EQ("UNKNOWN: Not initialized yet",
            api_manager->ConfigLoadingStatus().ToString());
  EXPECT_EQ("bookstore.test.appspot.com", api_manager->service_name());
  EXPECT_EQ("", api_manager->service("2017-05-01r0").id());

  api_manager->Init();

  EXPECT_EQ("ABORTED: Invalid service config",
            api_manager->ConfigLoadingStatus().ToString());
  EXPECT_FALSE(api_manager->Enabled());

  EXPECT_EQ("", api_manager->service("2017-05-01r0").id());
}

}  // namespace

}  // namespace api_manager
}  // namespace google
