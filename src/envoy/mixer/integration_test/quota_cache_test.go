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
	"fmt"
	"testing"
)

const (
	okRequestNum = 10
	// Pool may have some prefetched tokens.
	// In order to see rejected request, reject request num > 20
	// the minPrefetch * 2.
	rejectRequestNum = 30
)

func TestQuotaCache(t *testing.T) {
	// Only check cache is enabled, quota cache is enabled.
	s := &TestSetup{
		t:    t,
		conf: basicConfig + "," + checkCacheConfig + "," + quotaCacheConfig,
	}
	if err := s.SetUp(); err != nil {
		t.Fatalf("Failed to setup test: %v", err)
	}
	defer s.TearDown()

	url := fmt.Sprintf("http://localhost:%d/echo", ClientProxyPort)

	// Issues a GET echo request with 0 size body
	tag := "OKGet"
	for i := 0; i < 10; i++ {
		if _, _, err := HTTPGet(url); err != nil {
			t.Errorf("Failed in request %s: %v", tag, err)
		}
	}
	// Less than 5 time of Quota is called.
	if s.mixer.quota.count >= 5 {
		t.Fatalf("%s quota called count %v should not be more than 5",
			tag, s.mixer.quota.count)
	}
}
