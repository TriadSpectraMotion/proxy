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

#include <future>
#include "gtest/gtest.h"
#include "int_client.h"
#include "int_server.h"
#include "mixer/v1/mixer.pb.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

namespace Mixer {
namespace Integration {

enum class NetworkFailPolicy { FAIL_OPEN = 0, FAIL_CLOSED = 1 };

inline static int networkFailPolicyToInt(NetworkFailPolicy policy) {
  switch (policy) {
    case NetworkFailPolicy::FAIL_OPEN:
      return 0;
    default:
      return 1;
  }
}

class MixerFaultTest : public Envoy::HttpIntegrationTest, public testing::Test {
 public:
  MixerFaultTest()
      : HttpIntegrationTest(Envoy::Http::CodecClient::Type::HTTP1, Envoy::Network::Address::IpVersion::v4,
                            std::make_unique<Envoy::Event::TestRealTimeSystem>()),
        transport_socket_factory_(),
        client_("client") {
    Envoy::Http::CodecClient::Type origin_protocol = Envoy::Http::CodecClient::Type::HTTP2;
    setUpstreamProtocol(Envoy::Http::CodecClient::Type::HTTP2 == origin_protocol
                            ? Envoy::FakeHttpConnection::Type::HTTP2
                            : Envoy::FakeHttpConnection::Type::HTTP1);

    // Tell the base class that we will create our own upstream origin server.
    fake_upstreams_count_ = 0;

    origin_listeners_.emplace_back(new LocalListenSocket());
    origin_servers_.emplace_back(
        new Server(fmt::sprintf("origin-0"), *origin_listeners_.back(), transport_socket_factory_, origin_protocol));
  }

  virtual ~MixerFaultTest() {}

  // TODO modify BaseIntegrationTest in Envoy to eliminate this copy of the createEnvoy function.
  virtual void createEnvoy() override {
    std::vector<uint32_t> ports;

    // TODO modify BaseIntegrationTest to add additional ports without having to make them fake upstreams
    addPorts(ports);

    config_helper_.finalize(ports);

    // TODO modify BaseIntegrationTest use protected inheritance for Envoy::Logger::Loggable so tests can use ENVOY_LOG
    // fprintf(stderr, "Running Envoy with configuration:\n%s", config_helper_.bootstrap().DebugString().c_str());

    const std::string bootstrap_path = Envoy::TestEnvironment::writeStringToFileForTest(
        "bootstrap.json", Envoy::MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));

    std::vector<std::string> named_ports;
    const auto &static_resources = config_helper_.bootstrap().static_resources();
    for (int i = 0; i < static_resources.listeners_size(); ++i) {
      named_ports.push_back(static_resources.listeners(i).name());
    }
    createGeneratedApiTestServer(bootstrap_path, named_ports);
  }

  void extractMixerCounters(const Envoy::Stats::Store &stats, std::unordered_map<std::string, uint64_t> &map) {
    const std::string prefix("http_mixer_filter");

    for (auto counter : stats.counters()) {
      if (!std::equal(prefix.begin(), prefix.begin() + prefix.size(), counter->name().begin())) {
        continue;
      }

      map[counter->name()] = counter->value();
    }
  }

 protected:
  LoadGeneratorPtr startServers(NetworkFailPolicy fail_policy, ServerCallbackHelper &origin_callbacks,
                                ClusterHelper &policy_cluster, ClusterHelper &telemetry_cluster) {
    for (size_t i = 0; i < origin_servers_.size(); ++i) {
      origin_servers_[i]->start(origin_callbacks);
    }

    for (size_t i = 0; i < policy_cluster.servers().size(); ++i) {
      policy_listeners_.emplace_back(new LocalListenSocket());
      policy_servers_.emplace_back(new Server(fmt::sprintf("policy-%d", i), *policy_listeners_.back(),
                                              transport_socket_factory_, Envoy::Http::CodecClient::Type::HTTP2));
      policy_servers_.back()->start(*policy_cluster.servers()[i]);
    }

    for (size_t i = 0; i < telemetry_cluster.servers().size(); ++i) {
      telemetry_listeners_.emplace_back(new LocalListenSocket());
      telemetry_servers_.emplace_back(new Server(fmt::sprintf("telemetry-%d", i), *telemetry_listeners_.back(),
                                                 transport_socket_factory_, Envoy::Http::CodecClient::Type::HTTP2));
      telemetry_servers_.back()->start(*telemetry_cluster.servers()[i]);
    }

    std::string telemetry_name("telemetry-backend");
    std::string policy_name("policy-backend");

    addNodeMetadata();
    configureMixerFilter(fail_policy, policy_name, telemetry_name);
    addCluster(telemetry_name, telemetry_listeners_);
    addCluster(policy_name, policy_listeners_);

    // This calls createEnvoy() (see below) and then starts envoy
    HttpIntegrationTest::initialize();

    auto addr = Envoy::Network::Utility::parseInternetAddress("127.0.0.1", static_cast<uint16_t>(lookupPort("http")));
    return std::make_unique<LoadGenerator>(client_, transport_socket_factory_, HttpVersion::HTTP1, addr);
  }

 private:
  void addPorts(std::vector<uint32_t> &ports) {
    // origin must come first.  The order of the rest depends on the order their cluster was added to the config.
    for (size_t i = 0; i < origin_listeners_.size(); ++i) {
      ports.push_back(origin_listeners_[i]->localAddress()->ip()->port());
    }

    for (size_t i = 0; i < telemetry_listeners_.size(); ++i) {
      ports.push_back(telemetry_listeners_[i]->localAddress()->ip()->port());
    }

    for (size_t i = 0; i < policy_listeners_.size(); ++i) {
      ports.push_back(policy_listeners_[i]->localAddress()->ip()->port());
    }
  }

  void addNodeMetadata() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap &bootstrap) {
      ::google::protobuf::Struct meta;

      Envoy::MessageUtil::loadFromJson(R"({
        "ISTIO_VERSION": "1.0.1",
        "NODE_UID": "pod",
        "NODE_NAMESPACE": "kubernetes://dest.pod"
      })",
                                       meta);

      bootstrap.mutable_node()->mutable_metadata()->MergeFrom(meta);
    });
  }

  void configureMixerFilter(NetworkFailPolicy fail_policy, const std::string &policy_name,
                            const std::string &telemetry_name) {
    constexpr char sourceUID[] = "kubernetes://src.pod";

    std::string mixer_conf{fmt::sprintf(R"EOF(
  name: mixer
  config:
    defaultDestinationService: "default"
    mixerAttributes:
      attributes: {}
    serviceConfigs: {
      "default": {}
    }
    transport:
      attributes_for_mixer_proxy:
        attributes: {
          "source.uid": {
            string_value: %s
          }
        }
      network_fail_policy: {
        policy: %d
      }
      report_cluster: %s
      check_cluster: %s
                  )EOF",
                                        sourceUID, networkFailPolicyToInt(fail_policy), telemetry_name.c_str(),
                                        policy_name.c_str())};
    config_helper_.addFilter(mixer_conf);
  }

  void addCluster(const std::string &name, const std::vector<Envoy::Network::TcpListenSocketPtr> &listeners) {
    constexpr uint32_t max_uint32 = 2147483647U;  // protobuf max, not language max

    // See https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/cds.proto#cluster

    // TODO something in the base class clobbers the connection timeout here
    std::string cluster_conf{fmt::sprintf(R"EOF(
                      name: %s
                      type: STATIC
                      lb_policy: ROUND_ROBIN
                      http2_protocol_options: {
                         max_concurrent_streams: %u
                      }
                      connect_timeout: 1s
                      max_requests_per_connection: %u
                      hosts:
                  )EOF",
                                          name.c_str(), max_uint32, max_uint32)};

    for (size_t i = 0; i < listeners.size(); ++i) {
      cluster_conf.append({fmt::sprintf(R"EOF(
                        - socket_address:
                            address: %s
                            port_value: %d
                  )EOF",
                                        Envoy::Network::Test::getLoopbackAddressString(version_),
                                        listeners[i]->localAddress()->ip()->port())});
    }

    config_helper_.addConfigModifier([cluster_conf](envoy::config::bootstrap::v2::Bootstrap &bootstrap) {
      bootstrap.mutable_static_resources()->add_clusters()->CopyFrom(
          Envoy::TestUtility::parseYaml<envoy::api::v2::Cluster>(cluster_conf));
    });
  }

  Envoy::Network::RawBufferSocketFactory transport_socket_factory_;
  Client client_;
  std::vector<Envoy::Network::TcpListenSocketPtr> origin_listeners_;
  std::vector<Envoy::Network::TcpListenSocketPtr> policy_listeners_;
  std::vector<Envoy::Network::TcpListenSocketPtr> telemetry_listeners_;
  // These three vectors could store Server directly if Envoy::Stats::IsolatedStoreImpl was made movable.
  std::vector<ServerPtr> origin_servers_;
  std::vector<ServerPtr> policy_servers_;
  std::vector<ServerPtr> telemetry_servers_;
  Envoy::Network::Address::InstanceConstSharedPtr envoy_address_;  // at most 1 envoy
};

TEST_F(MixerFaultTest, HappyPath) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::warn);

  constexpr NetworkFailPolicy fail_policy = NetworkFailPolicy::FAIL_CLOSED;
  constexpr uint32_t connections_to_initiate = 30;
  constexpr uint32_t requests_to_send = 30 * connections_to_initiate;

  // Origin server immediately sends a simple 200 OK to every request
  ServerCallbackHelper origin_callbacks;

  ClusterHelper policy_cluster(
      {new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
        // Send a gRPC success response immediately to every policy check
        ::istio::mixer::v1::CheckResponse response;
        response.mutable_precondition()->mutable_status()->set_code(google::protobuf::util::error::Code::OK);
        stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
      })});

  ClusterHelper telemetry_cluster(
      {new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
        // Send a gRPC success response immediately to every telemetry report.
        ::istio::mixer::v1::ReportResponse response;
        stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
      })});

  LoadGeneratorPtr client = startServers(fail_policy, origin_callbacks, policy_cluster, telemetry_cluster);

  //
  // Exec test and wait for it to finish
  //

  Envoy::Http::HeaderMapPtr request{new Envoy::Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}}};
  client->run(connections_to_initiate, requests_to_send, std::move(request));

  // shutdown envoy by destroying it
  test_server_ = nullptr;
  // wait until the upstreams have closed all connections they accepted.  shutting down envoy should close them all
  origin_callbacks.wait();
  policy_cluster.wait();
  telemetry_cluster.wait();

  //
  // Evaluate test
  //

  // All client connections are successfully established.
  EXPECT_EQ(client->connectSuccesses(), connections_to_initiate);
  EXPECT_EQ(0, client->connectFailures());
  // Client close callback called for every client connection.
  EXPECT_EQ(client->localCloses(), connections_to_initiate);
  // Client response callback is called for every request sent
  EXPECT_EQ(client->responsesReceived(), requests_to_send);
  // Every response was a 2xx class
  EXPECT_EQ(client->class2xxResponses(), requests_to_send);
  EXPECT_EQ(0, client->class4xxResponses());
  EXPECT_EQ(0, client->class5xxResponses());
  EXPECT_EQ(0, client->responseTimeouts());
  // No client sockets are rudely closed by server / no client sockets are reset.
  EXPECT_EQ(0, client->remoteCloses());

  // assert that the origin request callback is called for every client request sent
  EXPECT_EQ(origin_callbacks.requestsReceived(), requests_to_send);

  // assert that the policy request callback is called for every client request sent
  EXPECT_EQ(policy_cluster.requestsReceived(), requests_to_send);
}

TEST_F(MixerFaultTest, FailClosedAndClosePolicySocketAfterAccept) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::warn);

  constexpr NetworkFailPolicy fail_policy = NetworkFailPolicy::FAIL_CLOSED;
  constexpr uint32_t connections_to_initiate = 30;
  constexpr uint32_t requests_to_send = 30 * connections_to_initiate;

  //
  // Setup
  //

  // Origin server immediately sends a simple 200 OK to every request
  ServerCallbackHelper origin_callbacks;

  ClusterHelper policy_cluster(
      {// Policy server immediately closes any connection accepted.
       new ServerCallbackHelper(
           [](ServerConnection &, ServerStream &, Envoy::Http::HeaderMapPtr &&) {
             GTEST_FATAL_FAILURE_("Connections immediately closed so no response should be received");
           },
           [](ServerConnection &) -> ServerCallbackResult { return ServerCallbackResult::CLOSE; })});

  ClusterHelper telemetry_cluster(
      {// Telemetry server sends a gRPC success response immediately to every telemetry report.
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::ReportResponse response;
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
       })});

  LoadGeneratorPtr client = startServers(fail_policy, origin_callbacks, policy_cluster, telemetry_cluster);
  //
  // Exec test and wait for it to finish
  //

  Envoy::Http::HeaderMapPtr request{new Envoy::Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}}};
  client->run(connections_to_initiate, requests_to_send, std::move(request));

  // shutdown envoy by destroying it
  test_server_ = nullptr;
  // wait until the upstreams have closed all connections they accepted.  shutting down envoy should close them all
  origin_callbacks.wait();
  policy_cluster.wait();
  telemetry_cluster.wait();

  //
  // Evaluate test
  //

  // All client connections are successfully established.
  EXPECT_EQ(client->connectSuccesses(), connections_to_initiate);
  EXPECT_EQ(0, client->connectFailures());
  // Client close callback called for every client connection.
  EXPECT_EQ(client->localCloses(), connections_to_initiate);
  // Client response callback is called for every request sent
  EXPECT_EQ(client->responsesReceived(), requests_to_send);
  // Every response was a 5xx class
  EXPECT_EQ(0, client->class2xxResponses());
  EXPECT_EQ(0, client->class4xxResponses());
  EXPECT_EQ(requests_to_send, client->class5xxResponses());
  EXPECT_EQ(0, client->responseTimeouts());
  // No client sockets are rudely closed by server / no client sockets are reset.
  EXPECT_EQ(0, client->remoteCloses());

  // Origin server should see no requests since the mixer filter is configured to fail closed.
  EXPECT_EQ(0, origin_callbacks.requestsReceived());

  // Policy server accept callback is called for every client connection initiated.
  EXPECT_GE(policy_cluster.connectionsAccepted(), connections_to_initiate);
  // Policy server request callback is never called
  EXPECT_EQ(0, policy_cluster.requestsReceived());
  // Policy server closes every connection
  EXPECT_EQ(policy_cluster.connectionsAccepted(), policy_cluster.localCloses());
  EXPECT_EQ(0, policy_cluster.remoteCloses());
}

TEST_F(MixerFaultTest, FailClosedAndSendPolicyResponseSlowly) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::warn);

  // TODO fix crash seen in https://drive.google.com/open?id=1_Heqpm2OJnrB5racoH05R0BlGzI55Akv with 10,000 connections

  constexpr NetworkFailPolicy fail_policy = NetworkFailPolicy::FAIL_CLOSED;
  constexpr uint32_t connections_to_initiate = 30 * 30;
  constexpr uint32_t requests_to_send = 1 * connections_to_initiate;

  // Origin server immediately sends a simple 200 OK to every request
  ServerCallbackHelper origin_callbacks;

  ClusterHelper policy_cluster(
      {// Send a gRPC success response after 60 seconds to every policy check
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::CheckResponse response;
         response.mutable_precondition()->mutable_status()->set_code(google::protobuf::util::error::Code::OK);
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(60'000));
       })});

  ClusterHelper telemetry_cluster(
      {// Sends a gRPC success response immediately to every telemetry report.
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::ReportResponse response;
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
       })});

  LoadGeneratorPtr client = startServers(fail_policy, origin_callbacks, policy_cluster, telemetry_cluster);

  //
  // Exec test and wait for it to finish
  //

  Envoy::Http::HeaderMapPtr request{new Envoy::Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}}};
  // TODO configure Mixer filter to timeout faster and remove the increased timeout from the load generator.

  // TODO create another test case where the client goes away before the policy check can complete.
  // TODO fix memory leaks associated with the above:
  // https://drive.google.com/file/d/1n8QIyoxqbgDYk0WxVtZ0ktgfGdp9RqvP/view?usp=sharing
  client->run(connections_to_initiate, requests_to_send, std::move(request), std::chrono::milliseconds(10'000));

  // shutdown envoy by destroying it
  test_server_ = nullptr;
  // wait until the upstreams have closed all connections they accepted.  shutting down envoy should close them all
  origin_callbacks.wait();
  policy_cluster.wait();
  telemetry_cluster.wait();

  //
  // Evaluate test
  //

  // All client connections are successfully established.
  EXPECT_EQ(client->connectSuccesses(), connections_to_initiate);
  EXPECT_EQ(0, client->connectFailures());
  // Client close callback called for every client connection.
  EXPECT_EQ(client->localCloses(), connections_to_initiate);
  // Client response callback is called for every request sent
  EXPECT_EQ(client->responsesReceived(), requests_to_send);
  // Every response was a 5xx class
  EXPECT_EQ(0, client->class2xxResponses());
  EXPECT_EQ(0, client->class4xxResponses());
  EXPECT_EQ(requests_to_send, client->class5xxResponses());
  EXPECT_EQ(0, client->responseTimeouts());
  // No client sockets are rudely closed by server / no client sockets are reset.
  EXPECT_EQ(0, client->remoteCloses());

  // Origin server should see no requests since the mixer filter is configured to fail closed.
  EXPECT_EQ(0, origin_callbacks.requestsReceived());

  // Policy server accept callback is called at least once (h2 socket reuse means may only be called once)
  EXPECT_GE(policy_cluster.connectionsAccepted(), 1);
  // Policy server request callback sees every policy check
  EXPECT_EQ(requests_to_send, policy_cluster.requestsReceived());
  // Policy server closes every connection
  EXPECT_EQ(policy_cluster.connectionsAccepted(), policy_cluster.localCloses() + policy_cluster.remoteCloses());
}

TEST_F(MixerFaultTest, TolerateTelemetryBlackhole) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::warn);

  constexpr NetworkFailPolicy fail_policy = NetworkFailPolicy::FAIL_CLOSED;
  constexpr uint32_t connections_to_initiate = 300;
  constexpr uint32_t requests_to_send = 30 * connections_to_initiate;

  // Origin server immediately sends a simple 200 OK to every request
  ServerCallbackHelper origin_callbacks;

  // Over provision the policy cluster to reduce the change it becomes a source of error

  ClusterHelper policy_cluster(
      {// Send a gRPC success response immediately to every policy check
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::CheckResponse response;
         response.mutable_precondition()->mutable_status()->set_code(google::protobuf::util::error::Code::OK);
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
       }),
       // Send a gRPC success response immediately to every policy check
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::CheckResponse response;
         response.mutable_precondition()->mutable_status()->set_code(google::protobuf::util::error::Code::OK);
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
       }),
       // Send a gRPC success response immediately to every policy check
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::CheckResponse response;
         response.mutable_precondition()->mutable_status()->set_code(google::protobuf::util::error::Code::OK);
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
       })});

  ClusterHelper telemetry_cluster(
      {// Telemetry receives the telemetry report requests but never sends a response.
       new ServerCallbackHelper([](ServerConnection &, ServerStream &, Envoy::Http::HeaderMapPtr &&) {
         // eat the request and do nothing
       })});

  LoadGeneratorPtr client = startServers(fail_policy, origin_callbacks, policy_cluster, telemetry_cluster);

  //
  // Exec test and wait for it to finish
  //

  Envoy::Http::HeaderMapPtr request{new Envoy::Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}}};
  client->run(connections_to_initiate, requests_to_send, std::move(request), std::chrono::milliseconds(600'000));

  // shutdown envoy by destroying it
  test_server_ = nullptr;
  // wait until the upstreams have closed all connections they accepted.  shutting down envoy should close them all
  origin_callbacks.wait();
  policy_cluster.wait();
  telemetry_cluster.wait();

  //
  // Evaluate test
  //

  // All client connections are successfully established.
  EXPECT_EQ(client->connectSuccesses(), connections_to_initiate);
  EXPECT_EQ(0, client->connectFailures());
  // Client close callback called for every client connection.
  EXPECT_EQ(client->localCloses(), connections_to_initiate);
  // Client response callback is called for every request sent
  EXPECT_EQ(client->responsesReceived(), requests_to_send);
  // Every response was a 2xx class
  EXPECT_EQ(client->class2xxResponses(), requests_to_send);
  EXPECT_EQ(0, client->class4xxResponses());
  EXPECT_EQ(0, client->class5xxResponses());
  EXPECT_EQ(0, client->responseTimeouts());
  // No client sockets are rudely closed by server / no client sockets are reset.
  EXPECT_EQ(0, client->remoteCloses());

  // assert that the origin request callback is called for every client request sent
  EXPECT_EQ(origin_callbacks.requestsReceived(), requests_to_send);

  // Policy server accept callback is called at least once (h2 socket reuse means may only be called once)
  EXPECT_GE(policy_cluster.connectionsAccepted(), 1);
  // Policy server request callback sees every policy check
  EXPECT_EQ(requests_to_send, policy_cluster.requestsReceived());
  // Policy server closes every connection
  EXPECT_EQ(policy_cluster.connectionsAccepted(), policy_cluster.localCloses() + policy_cluster.remoteCloses());

  // Telemetry server accept callback is called at least once (h2 socket reuse means may only be called once)
  EXPECT_GE(telemetry_cluster.connectionsAccepted(), 1);
}

TEST_F(MixerFaultTest, FailOpenAndSendPolicyResponseSlowly) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::warn);

  // TODO fix crash seen in https://drive.google.com/open?id=1_Heqpm2OJnrB5racoH05R0BlGzI55Akv with 10,000 connections

  constexpr NetworkFailPolicy fail_policy = NetworkFailPolicy::FAIL_OPEN;
  constexpr uint32_t connections_to_initiate = 30;
  constexpr uint32_t requests_to_send = 3 * connections_to_initiate;

  // Origin server immediately sends a simple 200 OK to every request
  ServerCallbackHelper origin_callbacks;

  ClusterHelper policy_cluster(
      {// Policy server sends a gRPC success response after 60 seconds to every policy check
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::CheckResponse response;
         response.mutable_precondition()->mutable_status()->set_code(google::protobuf::util::error::Code::OK);
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(60'000));
       })});

  ClusterHelper telemetry_cluster(
      {// Telemetry server sends a gRPC success response immediately to every telemetry report.
       new ServerCallbackHelper([](ServerConnection &, ServerStream &stream, Envoy::Http::HeaderMapPtr &&) {
         ::istio::mixer::v1::ReportResponse response;
         stream.sendGrpcResponse(Envoy::Grpc::Status::Ok, response, std::chrono::milliseconds(0));
       })});

  LoadGeneratorPtr client = startServers(fail_policy, origin_callbacks, policy_cluster, telemetry_cluster);

  //
  // Exec test and wait for it to finish
  //

  Envoy::Http::HeaderMapPtr request{new Envoy::Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}}};
  // TODO configure Mixer filter to timeout faster and remove the increased timeout from the load generator.

  // TODO create another test case where the client goes away before the policy check can complete.
  // TODO fix memory leaks associated with the above:
  // https://drive.google.com/file/d/1n8QIyoxqbgDYk0WxVtZ0ktgfGdp9RqvP/view?usp=sharing
  client->run(connections_to_initiate, requests_to_send, std::move(request), std::chrono::milliseconds(10'000));

  std::unordered_map<std::string, uint64_t> mixer_counters;
  extractMixerCounters(test_server_->stat_store(), mixer_counters);

    // shutdown envoy by destroying it
  test_server_ = nullptr;
  // wait until the upstreams have closed all connections they accepted.  shutting down envoy should close them all
  origin_callbacks.wait();
  policy_cluster.wait();
  telemetry_cluster.wait();

  //
  // Evaluate test
  //

  for (auto it : mixer_counters) {
    std::cerr << it.first << " = " << it.second << std::endl;
  }

  // All client connections are successfully established.
  EXPECT_EQ(client->connectSuccesses(), connections_to_initiate);
  EXPECT_EQ(0, client->connectFailures());
  // Client close callback called for every client connection.
  EXPECT_EQ(client->localCloses(), connections_to_initiate);
  // Client response callback is called for every request sent
  EXPECT_EQ(client->responsesReceived(), requests_to_send);
  // Every response was a 2xx class
  EXPECT_EQ(client->class2xxResponses(), requests_to_send);
  EXPECT_EQ(0, client->class4xxResponses());
  EXPECT_EQ(0, client->class5xxResponses());
  EXPECT_EQ(0, client->responseTimeouts());
  // No client sockets are rudely closed by server / no client sockets are reset.
  EXPECT_EQ(0, client->remoteCloses());

  // Origin server should see every requests since the mixer filter is configured to fail open.
  EXPECT_EQ(origin_callbacks.requestsReceived(), requests_to_send);

  // Policy server accept callback is called at least once (h2 socket reuse means may only be called once)
  EXPECT_GE(policy_cluster.connectionsAccepted(), 1);
  // Policy server request callback sees every policy check
  EXPECT_EQ(requests_to_send, policy_cluster.requestsReceived());
  // Policy server closes every connection
  EXPECT_EQ(policy_cluster.connectionsAccepted(), policy_cluster.localCloses() + policy_cluster.remoteCloses());
}

}  // namespace Integration
}  // namespace Mixer
