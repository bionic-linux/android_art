// Copyright (C) 2016 The Android Open Source Project
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

package art

import (
	"path/filepath"
	"sort"
	"strings"

	"android/soong/android"
	"android/soong/cc/config"
)

var (
	pctx                  = android.NewPackageContext("android/soong/art")
	prebuiltToolsForTests = []string{"as", "addr2line", "objdump"}
)

func init() {
	android.RegisterMakeVarsProvider(pctx, makeVarsProvider)
	pctx.Import("android/soong/cc/config")
}

func makeVarsProvider(ctx android.MakeVarsContext) {
	ctx.Strict("LIBART_IMG_HOST_BASE_ADDRESS", ctx.Config().LibartImgHostBaseAddress())
	ctx.Strict("LIBART_IMG_TARGET_BASE_ADDRESS", ctx.Config().LibartImgDeviceBaseAddress())

	testMap := testMap(ctx.Config())
	var testNames []string
	for name := range testMap {
		testNames = append(testNames, name)
	}

	sort.Strings(testNames)

	for _, name := range testNames {
		ctx.Strict("ART_TEST_LIST_"+name, strings.Join(testMap[name], " "))
	}

	// Iterate over the list in deterministic order
	testcasesContent := testcasesContent(ctx.Config())
	var keys []string
	for key := range testcasesContent {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	// Create list of copy commands to install the content of the testcases directory.
	copy_cmds := []string{}
	for _, key := range keys {
		copy_cmds = append(copy_cmds, testcasesContent[key]+":"+key)
	}

	// Add prebuild tools.
	for _, cmd := range prebuiltToolsForTests {
		target := ctx.Config().Targets[android.BuildOs][0]
		toolchain := config.FindToolchain(target.Os, target.Arch)
		path, err := ctx.Eval(filepath.Join(toolchain.GccRoot(), "bin", toolchain.GccTriple()+"-"+cmd))
		if err != nil {
			panic(err)
		}
		copy_cmds = append(copy_cmds, path+":bin/"+toolchain.GccTriple()+"-"+cmd)
	}

	ctx.Strict("ART_TESTCASES_CONTENT", strings.Join(copy_cmds, " "))
}
