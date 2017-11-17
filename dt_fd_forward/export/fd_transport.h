// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef ART_DT_FD_FORWARD_EXPORT_FD_TRANSPORT_H_
#define ART_DT_FD_FORWARD_EXPORT_FD_TRANSPORT_H_

#include <stdint.h>

namespace dt_fd_forward {

// This message is sent over the fd associated with the transport when we are listening for fds.
static constexpr char kListenStartMessage[] = "dt_fd_forward:START-LISTEN";

// This message is sent over the fd associated with the transport when we stop listening for fds.
static constexpr char kListenEndMessage[] = "dt_fd_forward:END-LISTEN";

}  // namespace dt_fd_forward

#endif  // ART_DT_FD_FORWARD_EXPORT_FD_TRANSPORT_H_
