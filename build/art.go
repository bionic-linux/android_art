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
	"android/soong"
	"android/soong/android"
	"android/soong/cc"
	"fmt"
	"sync"

	"github.com/google/blueprint"
)

var globalFlagsOnce sync.Once
var globalCflags []string
var globalAsflags []string

func globalFlags(ctx cc.BaseModuleContext) ([]string, []string) {
	globalFlagsOnce.Do(func() {
		var cflags []string
		var asflags []string

		tlab := false

		gcType := envDefault(ctx, "ART_DEFAULT_GC_TYPE", "CMS")

		if envTrue(ctx, "ART_TEST_DEBUG_GC") {
			gcType = "SS"
			tlab = true
		}

		cflags = append(cflags, "-DART_DEFAULT_GC_TYPE_IS_"+gcType)
		if tlab {
			cflags = append(cflags, "-DART_USE_TLAB=1")
		}

		imtSize := envDefault(ctx, "ART_DEFAULT_GC_TYPE", "43")
		cflags = append(cflags, "-DIMT_SIZE="+imtSize)

		if envTrue(ctx, "ART_HEAP_POISIONING") {
			cflags = append(cflags, "-DART_HEAP_POISONING=1")
			asflags = append(asflags, "-DART_HEAP_POISONING=1")
		}

		if envTrue(ctx, "ART_USE_READ_BARRIER") {
			// Used to change the read barrier type. Valid values are BAKER, BROOKS, TABLELOOKUP.
			// The default is BAKER.
			barrierType := envDefault(ctx, "ART_READ_BARRIER_TYPE", "BAKER")
			cflags = append(cflags,
				"-DART_USE_READ_BARRIER=1",
				"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")
			asflags = append(asflags,
				"-DART_USE_READ_BARRIER=1",
				"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")

			// Temporarily override -fstack-protector-strong with -fstack-protector to avoid a major
			// slowdown with the read barrier config. b/26744236.
			cflags = append(cflags, "-fstack-protector")
		}

		cflags = append(cflags,
			"-DART_STACK_OVERFLOW_GAP_arm=8192",
			"-DART_STACK_OVERFLOW_GAP_arm64=8192",
			"-DART_STACK_OVERFLOW_GAP_mips=16384",
			"-DART_STACK_OVERFLOW_GAP_mips64=16384",
			"-DART_STACK_OVERFLOW_GAP_x86=8192",
			"-DART_STACK_OVERFLOW_GAP_x86_64=8192",
		)

		globalCflags = cflags
		globalAsflags = asflags
	})

	return globalCflags, globalAsflags
}

var deviceFlagsOnce sync.Once
var deviceCflags []string

func deviceFlags(ctx cc.BaseModuleContext) []string {
	deviceFlagsOnce.Do(func() {
		var cflags []string
		deviceFrameSizeLimit := 1736
		if len(ctx.AConfig().SanitizeDevice()) > 0 {
			deviceFrameSizeLimit = 6400
		}
		cflags = append(cflags,
			fmt.Sprintf("-Wframe-larger-than=%d", deviceFrameSizeLimit),
			fmt.Sprintf("-DART_FRAME_SIZE_LIMIT=%d", deviceFrameSizeLimit),
		)

		cflags = append(cflags, "-DART_BASE_ADDRESS="+ctx.AConfig().LibartImgDeviceBaseAddress())
		if envTrue(ctx, "ART_TARGET_LINUX)") {
			cflags = append(cflags, "-DART_TARGET_LINUX")
		} else {
			cflags = append(cflags, "-DART_TARGET_ANDROID")
		}
		minDelta := envDefault(ctx, "LIBART_IMG_TARGET_MIN_BASE_ADDRESS_DELTA", "-0x1000000")
		maxDelta := envDefault(ctx, "LIBART_IMG_TARGET_MAX_BASE_ADDRESS_DELTA", "0x1000000")
		cflags = append(cflags, "-DART_BASE_ADDRESS_MIN_DELTA="+minDelta)
		cflags = append(cflags, "-DART_BASE_ADDRESS_MAX_DELTA="+maxDelta)

		deviceCflags = cflags
	})

	return deviceCflags
}

var hostFlagsOnce sync.Once
var hostCflags []string

func hostFlags(ctx cc.BaseModuleContext) []string {
	hostFlagsOnce.Do(func() {
		var cflags []string
		hostFrameSizeLimit := 1736
		cflags = append(cflags,
			fmt.Sprintf("-Wframe-larger-than=%d", hostFrameSizeLimit),
			fmt.Sprintf("-DART_FRAME_SIZE_LIMIT=%d", hostFrameSizeLimit),
		)

		cflags = append(cflags, "-DART_BASE_ADDRESS="+ctx.AConfig().LibartImgHostBaseAddress())
		cflags = append(cflags, "-DART_DEFAULT_INSTRUCTION_SET_FEATURES=default")
		minDelta := envDefault(ctx, "LIBART_IMG_HOST_MIN_BASE_ADDRESS_DELTA", "-0x1000000")
		maxDelta := envDefault(ctx, "LIBART_IMG_HOST_MAX_BASE_ADDRESS_DELTA", "0x1000000")
		cflags = append(cflags, "-DART_BASE_ADDRESS_MIN_DELTA="+minDelta)
		cflags = append(cflags, "-DART_BASE_ADDRESS_MAX_DELTA="+maxDelta)

		hostCflags = cflags
	})

	return hostCflags
}

func (a *artCustomizer) Flags(ctx cc.CustomizerFlagsContext) {
	cflags, asflags := globalFlags(ctx)
	ctx.AppendCflags(cflags...)
	ctx.AppendAsflags(asflags...)

	if ctx.Device() {
		ctx.AppendCflags(deviceFlags(ctx)...)
	}
	if ctx.Host() {
		ctx.AppendCflags(hostFlags(ctx)...)
	}
}

func (a *artCustomizer) Properties() []interface{} {
	return nil
}

func init() {
	soong.RegisterModuleType("art_cc_library", artLibrary)
}

func artLibrary() (blueprint.Module, []interface{}) {
	module, _ := cc.NewLibrary(android.HostAndDeviceSupported, true, true)
	module.Customizer = &artCustomizer{}
	return module.Init()
}

type artCustomizer struct {
}

func envDefault(ctx cc.BaseModuleContext, key string, defaultValue string) string {
	ret := ctx.AConfig().Getenv(key)
	if ret == "" {
		return defaultValue
	}
	return ret
}

func envTrue(ctx cc.BaseModuleContext, key string) bool {
	return ctx.AConfig().Getenv(key) == "true"
}
