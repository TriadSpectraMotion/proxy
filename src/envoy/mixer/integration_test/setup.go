// Copyright 2017 Istio Authors. All Rights Reserved.
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

package test

import (
	"log"
	"testing"
)

type TestSetup struct {
	envoy   *Envoy
	mixer   *MixerServer
	backend *HttpServer
	t       *testing.T
}

func SetUp(t *testing.T, conf string, stress bool) (s TestSetup, err error) {
	s.t = t
	s.envoy, err = NewEnvoy(conf, stress)
	if err != nil {
		log.Printf("unable to create Envoy %v", err)
	} else {
		s.envoy.Start()
	}

	s.mixer, err = NewMixerServer(MixerPort, stress)
	if err != nil {
		log.Printf("unable to create mixer server %v", err)
	} else {
		s.mixer.Start()
	}

	s.backend, err = NewHttpServer(BackendPort)
	if err != nil {
		log.Printf("unable to create HTTP server %v", err)
	} else {
		s.backend.Start()
	}
	return s, err
}

func (s *TestSetup) TearDown() {
	s.envoy.Stop()
	s.mixer.Stop()
	s.backend.Stop()
}

func (s *TestSetup) VerifyCheckCount(tag string, expected int) {
	if s.mixer.check.count != expected {
		s.t.Fatalf("%s check count doesn't match: %v\n, expected: %+v",
			tag, s.mixer.check.count, expected)
	}
}

func (s *TestSetup) VerifyCheck(tag string, result string) {
	_ = <-s.mixer.check.ch
	if err := Verify(s.mixer.check.bag, result); err != nil {
		s.t.Fatalf("Failed to verify %s check: %v\n, Attributes: %+v",
			tag, err, s.mixer.check.bag)
	}
}

func (s *TestSetup) VerifyReport(tag string, result string) {
	_ = <-s.mixer.report.ch
	if err := Verify(s.mixer.report.bag, result); err != nil {
		s.t.Fatalf("Failed to verify %s report: %v\n, Attributes: %+v",
			tag, err, s.mixer.report.bag)
	}
}

func (s *TestSetup) VerifyQuota(tag string, name string, amount int64) {
	_ = <-s.mixer.quota.ch
	if s.mixer.qma.Quota != name {
		s.t.Fatalf("Failed to verify %s quota name: %v, expected: %v\n",
			tag, s.mixer.qma.Quota, name)
	}
	if s.mixer.qma.Amount != amount {
		s.t.Fatalf("Failed to verify %s quota amount: %v, expected: %v\n",
			tag, s.mixer.qma.Amount, amount)
	}
}

func (s *TestSetup) DrainMixerAllChannels() {
	go func() {
		for true {
			_ = <-s.mixer.check.ch
		}
	}()
	go func() {
		for true {
			_ = <-s.mixer.report.ch
		}
	}()
	go func() {
		for true {
			_ = <-s.mixer.quota.ch
		}
	}()
}
