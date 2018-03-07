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

#include "src/envoy/http/authn/peer_authenticator.h"
#include "authentication/v1alpha1/policy.pb.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/protobuf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/envoy/http/authn/test_utils.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

namespace iaapi = istio::authentication::v1alpha1;

using testing::_;
using testing::DoAll;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace Envoy {
namespace Http {
namespace {

class MockAuthenticator : public PeerAuthenticator {
 public:
  MockAuthenticator(FilterContext* filter_context,
                        const DoneCallback& done_callback,
                        const istio::authentication::v1alpha1::Policy& policy)
      : PeerAuthenticator(filter_context, done_callback, policy) {}

  MOCK_CONST_METHOD2(validateX509,
                     void(const iaapi::MutualTls&, const MethodDoneCallback&));
  MOCK_CONST_METHOD2(validateJwt,
                     void(const iaapi::Jwt&, const MethodDoneCallback&));
};

class PeerAuthenticatorTest : public testing::Test {
 public:
  PeerAuthenticatorTest()
      : request_headers_{{":method", "GET"}, {":path", "/"}} {}
  ~PeerAuthenticatorTest() {}

  void SetUp() override { filter_context_.setHeaders(&request_headers_); }
  void createAuthenticator() {
    authenticator_.reset(new StrictMock<MockAuthenticator>(
        &filter_context_, on_done_callback_.AsStdFunction(), policy_));
  }

 protected:
  std::unique_ptr<StrictMock<MockAuthenticator>> authenticator_;
  StrictMock<AuthNTestUtilities::MockFilterContext> filter_context_;
  StrictMock<MockFunction<void(bool)>> on_done_callback_;
  Http::TestHeaderMapImpl request_headers_;
  iaapi::Policy policy_;
};

TEST_F(PeerAuthenticatorTest, EmptyPolicy) {
  createAuthenticator();
  EXPECT_CALL(on_done_callback_, Call(true)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(""),
      filter_context_.authenticationResult()));
}
TEST_F(PeerAuthenticatorTest, MTlsOnlyPass) {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
      peers {
        mtls {
        }
      }
    )EOF",
                                                      &policy_));

  createAuthenticator();
    EXPECT_CALL(*authenticator_, validateX509(_, _))
        .Times(1)
        .WillOnce(testing::WithArg<1>(
            testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
              callback(AuthNTestUtilities::CreateX509Payload("foo"), true);
            })));
  EXPECT_CALL(on_done_callback_, Call(true)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(R"(peer_user: "foo")"),
      filter_context_.authenticationResult()));
}
TEST_F(PeerAuthenticatorTest, MTlsOnlyFail) {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
      peers {
        mtls {
        }
      }
    )EOF",
                                                      &policy_));

  createAuthenticator();
    EXPECT_CALL(*authenticator_, validateX509(_, _))
        .Times(1)
        .WillOnce(testing::WithArg<1>(
            testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
              callback(nullptr, false);
            })));
  EXPECT_CALL(on_done_callback_, Call(false)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(""),
      filter_context_.authenticationResult()));
}


TEST_F(PeerAuthenticatorTest, JwtOnlyPass) {
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
    peers {
      jwt {
        issuer: "abc.xyz"
      }
    }
  )EOF",
                                                    &policy_));

  createAuthenticator();
  EXPECT_CALL(*authenticator_, validateJwt( _, _))
      .Times(1)
      .WillOnce(testing::WithArg<1>(
          testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
            callback(AuthNTestUtilities::CreateJwtPayload("foo", "istio.io"), true);
          })));
  EXPECT_CALL(on_done_callback_, Call(true)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(R"(peer_user: "foo")"),
      filter_context_.authenticationResult()));
}

TEST_F(PeerAuthenticatorTest, JwtOnlyFail) {
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
    peers {
      jwt {
        issuer: "abc.xyz"
      }
    }
  )EOF",
                                                    &policy_));

  createAuthenticator();
  EXPECT_CALL(*authenticator_, validateJwt( _, _))
      .Times(1)
      .WillOnce(testing::WithArg<1>(
          testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
            callback(nullptr, false);
          })));
  EXPECT_CALL(on_done_callback_, Call(false)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(""),
      filter_context_.authenticationResult()));
}

TEST_F(PeerAuthenticatorTest, Multiple) {
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
    peers {
      mtls {}
    }
    peers {
      jwt {
        issuer: "abc.xyz"
      }
    }
    peers {
      jwt {
        issuer: "another"
      }
    }
  )EOF",
                                                    &policy_));

  createAuthenticator();
  EXPECT_CALL(*authenticator_, validateX509(_, _))
      .Times(1)
      .WillOnce(testing::WithArg<1>(
          testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
            callback(nullptr, false);
          })));
  EXPECT_CALL(*authenticator_, validateJwt( _, _))
      .Times(1)
      .WillOnce(testing::WithArg<1>(
          testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
            callback(AuthNTestUtilities::CreateJwtPayload("foo", "istio.io"), true);
          })));
  EXPECT_CALL(on_done_callback_, Call(true)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(R"(peer_user: "foo")"),
      filter_context_.authenticationResult()));
}

TEST_F(PeerAuthenticatorTest, MultipleAllFail) {
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
    peers {
      mtls {}
    }
    peers {
      jwt {
        issuer: "abc.xyz"
      }
    }
    peers {
      jwt {
        issuer: "another"
      }
    }
  )EOF",
                                                    &policy_));

  createAuthenticator();
  EXPECT_CALL(*authenticator_, validateX509(_, _))
      .Times(1)
      .WillOnce(testing::WithArg<1>(
          testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
            callback(nullptr, false);
          })));
  EXPECT_CALL(*authenticator_, validateJwt( _, _))
      .Times(2)
      .WillRepeatedly(testing::WithArg<1>(
          testing::Invoke([](const AuthenticatorBase::MethodDoneCallback& callback) {
            callback(nullptr, false);
          })));
  EXPECT_CALL(on_done_callback_, Call(false)).Times(1);
  authenticator_->run();
  EXPECT_TRUE(TestUtility::protoEqual(
      AuthNTestUtilities::AuthNResultFromString(""),
      filter_context_.authenticationResult()));
}

//
// TEST_F(AuthentiationFilterTest, Origin) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     credential_rules [
//       {
//         binding: USE_ORIGIN
//         origins {
//           jwt {
//             issuer: "abc.xyz"
//           }
//         }
//       }
//     ]
//   )EOF",
//                                                     &policy_));
//
//   setup_filter();
//   EXPECT_CALL(*filter_, validateJwt(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateJwtPayload("foo", "istio.io"), true);
//           })));
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue,
//             filter_->decodeHeaders(request_headers_, true));
//   EXPECT_TRUE(TestUtility::protoEqual(
//       ContextFromString(
//           R"EOF(principal: "foo" origin { user: "foo" presenter: "istio.io"
// })EOF"),
//       filter_->context()));
// }
//
// TEST_F(AuthentiationFilterTest, OriginWithNoMethod) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     credential_rules {
//       binding: USE_ORIGIN
//     }
//   )EOF",
//                                                     &policy_));
//
//   setup_filter();
//   EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
//       .Times(1)
//       .WillOnce(testing::Invoke([](Http::HeaderMap& headers, bool) {
//         EXPECT_STREQ("401", headers.Status()->value().c_str());
//       }));
//   EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
//             filter_->decodeHeaders(request_headers_, true));
//   EXPECT_TRUE(
//       TestUtility::protoEqual(ContextFromString(""), filter_->context()));
// }
//
// TEST_F(AuthentiationFilterTest, OriginNoMatchingBindingRule) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     credential_rules {
//       binding: USE_ORIGIN
//       origins {
//         jwt {
//           issuer: "abc.xyz"
//         }
//       }
//       matching_peers: "foo"
//       matching_peers: "bar"
//     }
//   )EOF",
//                                                     &policy_));
//
//   setup_filter();
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue,
//             filter_->decodeHeaders(request_headers_, true));
//   EXPECT_TRUE(
//       TestUtility::protoEqual(ContextFromString(""), filter_->context()));
// }
//
// TEST_F(AuthentiationFilterTest, OriginMatchingBindingRule) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     peers {
//       mtls {
//       }
//     }
//     credential_rules {
//       binding: USE_ORIGIN
//       origins {
//         jwt {
//           issuer: "abc.xyz"
//         }
//       }
//       matching_peers: "foo"
//       matching_peers: "bar"
//     }
//   )EOF",
//                                                     &policy_));
//
//   setup_filter();
//
//   EXPECT_CALL(*filter_, validateX509(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateX509Payload("foo"), true);
//           })));
//   EXPECT_CALL(*filter_, validateJwt(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateJwtPayload("bar", "istio.io"), true);
//           })));
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue,
//             filter_->decodeHeaders(request_headers_, true));
//
//   EXPECT_TRUE(TestUtility::protoEqual(ContextFromString(R"EOF(
//     principal: "bar"
//     peer_user: "foo"
//     origin {
//       user: "bar"
//       presenter: "istio.io"
//     })EOF"),
//                                       filter_->context()));
// }
//
// TEST_F(AuthentiationFilterTest, OriginAughNFail) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     peers {
//       mtls {
//       }
//     }
//     credential_rules {
//       binding: USE_ORIGIN
//       origins {
//         jwt {
//           issuer: "abc.xyz"
//         }
//       }
//     }
//   )EOF",
//                                                     &policy_));
//
//   setup_filter();
//
//   EXPECT_CALL(*filter_, validateX509(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateX509Payload("foo"), true);
//           })));
//   EXPECT_CALL(*filter_, validateJwt(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(nullptr, false);
//           })));
//   EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
//       .Times(1)
//       .WillOnce(testing::Invoke([](Http::HeaderMap& headers, bool) {
//         EXPECT_STREQ("401", headers.Status()->value().c_str());
//       }));
//   EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
//             filter_->decodeHeaders(request_headers_, true));
//   EXPECT_TRUE(TestUtility::protoEqual(ContextFromString("peer_user:
//   \"foo\""),
//                                       filter_->context()));
// }
//
// TEST_F(AuthentiationFilterTest, PeerWithExtraJwtPolicy) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     peers {
//       mtls {
//       }
//     }
//     peers {
//       jwt {
//         issuer: "istio.io"
//       }
//     }
//     credential_rules {
//       binding: USE_PEER
//       origins {
//         jwt {
//           issuer: "foo"
//         }
//       }
//     }
//   )EOF",
//                                                     &policy_));
//   setup_filter();
//
//   // Peer authentication with mTLS/x509 success.
//   EXPECT_CALL(*filter_, validateX509(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateX509Payload("foo"), true);
//           })));
//
//   // Origin authentication also success.
//   EXPECT_CALL(*filter_, validateJwt(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateJwtPayload("frod", "istio.io"), true);
//           })));
//
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue,
//             filter_->decodeHeaders(request_headers_, true));
//   EXPECT_TRUE(TestUtility::protoEqual(ContextFromString(R"EOF(
//     principal: "foo"
//     peer_user: "foo"
//     origin {
//       user: "frod"
//       presenter: "istio.io"
//     }
//   )EOF"),
//                                       filter_->context()));
// }
//
// TEST_F(AuthentiationFilterTest, ComplexPolicy) {
//   ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"EOF(
//     peers {
//       mtls {
//       }
//     }
//     peers {
//       jwt {
//         issuer: "istio.io"
//       }
//     }
//     credential_rules {
//       binding: USE_ORIGIN
//       origins {
//         jwt {
//           issuer: "foo"
//         }
//       }
//       matching_peers: "foo"
//       matching_peers: "bar"
//     }
//     credential_rules {
//       binding: USE_ORIGIN
//       origins {
//         jwt {
//           issuer: "frod"
//         }
//       }
//       origins {
//         jwt {
//           issuer: "fred"
//         }
//       }
//     }
//   )EOF",
//                                                     &policy_));
//   setup_filter();
//
//   // Peerauthentication with mTLS/x509 fails.
//   EXPECT_CALL(*filter_, validateX509(_, _, _))
//       .Times(1)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(nullptr, false);
//           })));
//
//   // validateJwt is called 3 times:
//   // - First for source authentication, which success and set source user to
//   // frod.
//   // - Then twice for origin authentications (selected for rule matching
//   // "frod"), both failed.
//   EXPECT_CALL(*filter_, validateJwt(_, _, _))
//       .Times(3)
//       .WillOnce(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(CreateJwtPayload("frod", "istio.io"), true);
//           })))
//       .WillRepeatedly(testing::WithArg<2>(
//           testing::Invoke([](const AuthenticateDoneCallback& callback) {
//             callback(nullptr, false);
//           })));
//
//   EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
//       .Times(1)
//       .WillOnce(testing::Invoke([](Http::HeaderMap& headers, bool) {
//         EXPECT_STREQ("401", headers.Status()->value().c_str());
//       }));
//   EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
//             filter_->decodeHeaders(request_headers_, true));
//   EXPECT_TRUE(TestUtility::protoEqual(ContextFromString("peer_user:
//   \"frod\""),
//                                       filter_->context()));
// }
}  // namespace
}  // namespace Http
}  // namespace Envoy
