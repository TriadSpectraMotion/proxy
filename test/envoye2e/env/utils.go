// Copyright 2019 Istio Authors
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

package env

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func GetDefaultEnvoyBin() (string, error) {
	// Get bazel args if any
	buildArgs := os.Getenv("BAZEL_BUILD_ARGS")

	// Note: `bazel info bazel-bin` returns incorrect path to a binary (always fastbuild, not opt or dbg)
	// Instead we rely on symbolic link src/envoy/envoy in the workspace
	workspace, err := exec.Command("bazel", buildArgs, "info", "workspace").Output()
	if err != nil {
		return "", err
	}
	return filepath.Join(strings.TrimSuffix(string(workspace), "\n"), "bazel-bin/src/envoy/"), nil
}

func GetDefaultEnvoyBinOrDie() string {
	p, err := GetDefaultEnvoyBin()
	if err != nil {
		panic(err)
	}
	return p
}

func GetBazelOptOut() (string, error) {
	// Get bazel args if any
	buildArgs := os.Getenv("BAZEL_BUILD_ARGS")

	// `make build_wasm` puts generated wasm modules into k8-opt.
	bazelOutput, err := exec.Command("bazel", buildArgs, "info", "output_path").Output()
	if err != nil {
		return "", err
	}
	return filepath.Join(strings.TrimSuffix(string(bazelOutput), "\n"), "k8-opt/bin/"), nil
}

func GetBazelOptOutOrDie() string {
	p, err := GetBazelOptOut()
	if err != nil {
		panic(err)
	}
	return p
}

func SkipTSanASan(t *testing.T) {
	if os.Getenv("TSAN") != "" || os.Getenv("ASAN") != "" {
		t.Skip("https://github.com/istio/istio/issues/21273")
	}
}

func IsTSanASan() bool {
	return os.Getenv("TSAN") != "" || os.Getenv("ASAN") != ""
}
