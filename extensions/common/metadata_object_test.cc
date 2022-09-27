// Copyright Istio Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "extensions/common/metadata_object.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Istio {
namespace Common {

class WorkloadMetadataObjectTest : public testing::Test {
 public:
  WorkloadMetadataObjectTest() { ENVOY_LOG_MISC(info, "test"); }
};

TEST_F(WorkloadMetadataObjectTest, Hash) {
  WorkloadMetadataObject obj1("foo-pod-12345", "my-cluster", "default", "foo",
                              "foo", "latest", "foo-app", "v1",
                              WorkloadType::KUBERNETES_DEPLOYMENT, {}, {});
  WorkloadMetadataObject obj2("foo-pod-12345", "my-cluster", "default", "bar",
                              "baz", "first", "foo-app", "v1",
                              WorkloadType::KUBERNETES_JOB, {}, {});

  EXPECT_EQ(obj1.hash().value(), obj2.hash().value());
}

TEST_F(WorkloadMetadataObjectTest, Baggage) {
  WorkloadMetadataObject deploy(
      "pod-foo-1234", "my-cluster", "default", "foo", "foo-service", "v1alpha3",
      "foo-app", "v1", WorkloadType::KUBERNETES_DEPLOYMENT,
      {"10.10.10.1", "192.168.1.1"}, {"app", "storage"});

  WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "default", "foo",
                             "foo-service", "v1alpha3", "foo-app", "v1",
                             WorkloadType::KUBERNETES_POD,
                             {"10.10.10.1", "192.168.1.1"}, {"app", "storage"});

  WorkloadMetadataObject cronjob(
      "pod-foo-1234", "my-cluster", "default", "foo", "foo-service", "v1alpha3",
      "foo-app", "v1", WorkloadType::KUBERNETES_CRONJOB,
      {"10.10.10.1", "192.168.1.1"}, {"app", "storage"});

  WorkloadMetadataObject job("pod-foo-1234", "my-cluster", "default", "foo",
                             "foo-service", "v1alpha3", "foo-app", "v1",
                             WorkloadType::KUBERNETES_JOB,
                             {"10.10.10.1", "192.168.1.1"}, {"app", "storage"});

  EXPECT_EQ(deploy.baggage(),
            absl::StrCat("k8s.cluster.name=my-cluster,",
                         "k8s.namespace.name=default,k8s.deployment.name=foo,",
                         "service.name=foo-service,service.version=v1alpha3,",
                         "app.name=foo-app,app.version=v1"));

  EXPECT_EQ(pod.baggage(),
            absl::StrCat("k8s.cluster.name=my-cluster,",
                         "k8s.namespace.name=default,k8s.pod.name=foo,",
                         "service.name=foo-service,service.version=v1alpha3,",
                         "app.name=foo-app,app.version=v1"));

  EXPECT_EQ(cronjob.baggage(),
            absl::StrCat("k8s.cluster.name=my-cluster,",
                         "k8s.namespace.name=default,k8s.cronjob.name=foo,"
                         "service.name=foo-service,service.version=v1alpha3,",
                         "app.name=foo-app,app.version=v1"));

  EXPECT_EQ(job.baggage(),
            absl::StrCat("k8s.cluster.name=my-cluster,",
                         "k8s.namespace.name=default,k8s.job.name=foo,",
                         "service.name=foo-service,service.version=v1alpha3,",
                         "app.name=foo-app,app.version=v1"));
}

using ::testing::NiceMock;

TEST_F(WorkloadMetadataObjectTest, FromBaggage) {
  {
    auto obj = WorkloadMetadataObject::fromBaggage(
        absl::StrCat("k8s.cluster.name=my-cluster,k8s.namespace.name=default,",
                     "k8s.deployment.name=foo,service.name=foo-service,",
                     "service.version=v1alpha3"));

    EXPECT_EQ(obj.canonical_name_, "foo-service");
    EXPECT_EQ(obj.canonical_revision_, "v1alpha3");
    EXPECT_EQ(obj.workload_type_, WorkloadType::KUBERNETES_DEPLOYMENT);
    EXPECT_EQ(obj.workload_name_, "foo");
    EXPECT_EQ(obj.namespace_name_, "default");
    EXPECT_EQ(obj.cluster_name_, "my-cluster");
  }

  {
    auto obj = WorkloadMetadataObject::fromBaggage(
        absl::StrCat("k8s.cluster.name=my-cluster,k8s.namespace.name=test,k8s."
                     "pod.name=foo-pod-435,service.name=",
                     "foo-service,service.version=v1beta2"));

    EXPECT_EQ(obj.canonical_name_, "foo-service");
    EXPECT_EQ(obj.canonical_revision_, "v1beta2");
    EXPECT_EQ(obj.workload_type_, WorkloadType::KUBERNETES_POD);
    EXPECT_EQ(obj.workload_name_, "foo-pod-435");
    EXPECT_EQ(obj.instance_name_, "foo-pod-435");
    EXPECT_EQ(obj.namespace_name_, "test");
    EXPECT_EQ(obj.cluster_name_, "my-cluster");
  }

  {
    auto obj = WorkloadMetadataObject::fromBaggage(
        absl::StrCat("k8s.cluster.name=my-cluster,k8s.namespace.name=test,",
                     "k8s.job.name=foo-job-435,service.name=foo-service,",
                     "service.version=v1beta4"));

    EXPECT_EQ(obj.canonical_name_, "foo-service");
    EXPECT_EQ(obj.canonical_revision_, "v1beta4");
    EXPECT_EQ(obj.workload_type_, WorkloadType::KUBERNETES_JOB);
    EXPECT_EQ(obj.workload_name_, "foo-job-435");
    EXPECT_EQ(obj.instance_name_, "foo-job-435");
    EXPECT_EQ(obj.namespace_name_, "test");
    EXPECT_EQ(obj.cluster_name_, "my-cluster");
  }

  {
    auto obj = WorkloadMetadataObject::fromBaggage(
        absl::StrCat("k8s.cluster.name=my-cluster,k8s.namespace.name=test,",
                     "k8s.cronjob.name=foo-cronjob,service.name=foo-service,",
                     "service.version=v1beta4"));

    EXPECT_EQ(obj.canonical_name_, "foo-service");
    EXPECT_EQ(obj.canonical_revision_, "v1beta4");
    EXPECT_EQ(obj.workload_type_, WorkloadType::KUBERNETES_CRONJOB);
    EXPECT_EQ(obj.workload_name_, "foo-cronjob");
    EXPECT_EQ(obj.namespace_name_, "test");
    EXPECT_EQ(obj.cluster_name_, "my-cluster");
    EXPECT_EQ(obj.app_name_, "");
    EXPECT_EQ(obj.app_version_, "");
  }

  {
    auto obj = WorkloadMetadataObject::fromBaggage(absl::StrCat(
        "k8s.namespace.name=default,",
        "k8s.deployment.name=foo,service.name=foo-service,",
        "service.version=v1alpha3,app.name=foo-app,app.version=v1"));

    EXPECT_EQ(obj.canonical_name_, "foo-service");
    EXPECT_EQ(obj.canonical_revision_, "v1alpha3");
    EXPECT_EQ(obj.workload_type_, WorkloadType::KUBERNETES_DEPLOYMENT);
    EXPECT_EQ(obj.workload_name_, "foo");
    EXPECT_EQ(obj.namespace_name_, "default");
    EXPECT_EQ(obj.cluster_name_, "");
    EXPECT_EQ(obj.app_name_, "foo-app");
    EXPECT_EQ(obj.app_version_, "v1");
  }
}

}  // namespace Common
}  // namespace Istio
