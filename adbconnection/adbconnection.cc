null abort
  cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len   = CMSG_LEN(dt_fd_forward::FdSet::kDataLength);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type  = SCM_RIGHTS;

  // Duplicate the fds before sending them.
  android::base::unique_fd read_fd(art::DupCloexec(adb_connection_socket_));
  CHECK_NE(read_fd.get(), -1) << "Failed to dup read_fd_: " << strerror(errno);
  android::base::unique_fd write_fd(art::DupCloexec(adb_connection_socket_));
  CHECK_NE(write_fd.get(), -1) << "Failed to dup write_fd: " << strerror(errno);
  android::base::unique_fd write_lock_fd(art::DupCloexec(adb_write_event_fd_));
  CHECK_NE(write_lock_fd.get(), -1) << "Failed to dup write_lock_fd: " << strerror(errno);

  dt_fd_forward::FdSet {
    read_fd.get(), write_fd.get(), write_lock_fd.get()
  }.WriteData(CMSG_DATA(cmsg));

  int res = TEMP_FAILURE_RETRY(sendmsg(local_agent_control_sock_, &msg, MSG_EOR));
  if (res < 0) {
    PLOG(ERROR) << "Failed to send agent adb connection fds.";
  } else {
    sent_agent_fds_ = true;
    VLOG(jdwp) << "Fds have been sent to jdwp agent!";
  }  int sleep_ms = 50  const char* isa = GetInstructionSetString(art::Runtime::Current()->GetInstructionSet());      {.type = AdbConnectionClientInfoType::      {.type = AdbConnectionClientInfoType::debuggable,      {.type = AdbConnectionClientInfoType::architecture,  };           // connection.
        { (adb_connection_socket_ == -1 ? adbconnection_client_pollfd(control_ctx_.get()) : -1),
          POLLIN | POLLRDHUP, 0 },
        // if we have not loaded the agent either the adb_connection_socket_ is -1 meaning we don't
        // have a real connection yet or the socket through adb needs to be listened to for incoming
        // data that the agent or this plugin can handle.
        { should_listen_on_connection ? adb_connection_socket_ : -1, POLLIN | POLLRDHUP, 0 }
      };
      int res = TEMP_FAILURE_RETRY(poll(pollfds, 4, -1));
      if (res < 0) {
        PLOG(ERROR) << "Failed to poll!";
        return;
      }
      // We don't actually care about doing this we just use it to wake us up.
      // const struct pollfd& sleep_event_poll     = pollfds[0];
      const struct pollfd& agent_control_sock_poll = pollfds[1];
      const struct pollfd& control_sock_poll       = pollfds[2];
      const struct pollfd& adb_socket_poll         = pollfds[3];
      if (FlagsSet(agent_control_sock_poll.revents, POLLIN)) {
        CHECK(IsDebuggingPossible());  // This path is unexpected for a profileable process.
        DCHECK(agent_loaded_);
        char buf[257];
        res = TEMP_FAILURE_RETRY(recv(local_agent_control_sock_, buf, sizeof(buf) - 1, 0));
        if (res < 0) {
          PLOG(ERROR) << "Failed to read message from agent control socket! Retrying";
          continue;
        } else {
          buf[res + 1] = '\0';
          VLOG(jdwp) << "Local agent control sock has data: " << static_cast<const char*>(buf);
        }
        if (memcmp(kListenStartMessage, buf, sizeof(kListenStartMessage)) == 0) {
          agent_listening_ = true;
          if (adb_connection_socket_ != -1) {
            SendAgentFds(/*require_handshake=*/ !performed_handshake_);
          }
        } else if (memcmp(kListenEndMessage, buf, sizeof(kListenEndMessage)) == 0) {
          agent_listening_ = false;
        } else if (memcmp(kHandshakeCompleteMessage, buf, sizeof(kHandshakeCompleteMessage)) == 0) {
          if (agent_has_socket_) {
            performed_handshake_ = true;
          }
        } else if (memcmp(kCloseMessage, buf, sizeof(kCloseMessage)) == 0) {
          CloseFds();
          agent_has_socket_ = false;
        } else if (memcmp(kAcceptMessage, buf, sizeof(kAcceptMessage)) == 0) {
          agent_has_socket_ = true;
          sent_agent_fds_ = false;
          // We will only ever do the handshake once so reset this.
          performed_handshake_ = false;
        } else {
          LOG(ERROR) << "Unknown message received from debugger! '" << std::string(buf) << "'";
        }
      } else if (FlagsSet(control_sock_poll.revents, POLLIN)) {
        if (!IsDebuggingPossible()) {
            // For a profielable process, this path can execute when the adbd restarts.
            control_ctx_.reset();
            break;
        }
        bool maybe_send_fds = false;
        {
          // Hold onto this lock so that concurrent ddm publishes don't try to use an illegal fd.
          ScopedEventFdLock sefdl(adb_write_event_fd_);
          android::base::unique_fd new_fd(adbconnection_client_receive_jdwp_fd(control_ctx_.get()));
          if (new_fd == -1) {
            // Something went wrong. We need to retry getting the control socket.
            control_ctx_.reset();
            break;
          } else if (adb_connection_socket_ != -1) {
            // We already have a connection.
            VLOG(jdwp) << "Ignoring second debugger. Accept then drop!";
            if (new_fd >= 0) {
              new_fd.reset();
            }
          } else {
            VLOG(jdwp) << "Adb connection established with fd " << new_fd;
            adb_connection_socket_ = std::move(new_fd);
            maybe_send_fds = true;
          }
        }
        if (maybe_send_fds && agent_loaded_ && agent_listening_) {
          VLOG(jdwp) << "Sending fds as soon as we received them.";
          // The agent was already loaded so this must be after a disconnection. Therefore have the
          // transport perform the handshake.
          SendAgentFds(/*require_handshake=*/ true);
        }
      } else if (FlagsSet(control_sock_poll.revents, POLLRDHUP)) {
        // The other end of the adb connection just dropped it.
        // Reset the connection since we don't have an active socket through the adb server.
        // Note this path is expected for either debuggable or profileable processes.
        DCHECK(!agent_has_socket_) << "We shouldn't be doing anything if there is already a "
                                   << "connection active";
        control_ctx_.reset();
        break;
      } else if (FlagsSet(adb_socket_poll.revents, POLLIN)) {
        CHECK(IsDebuggingPossible());  // This path is unexpected for a profileable process.
        DCHECK(!agent_has_socket_);
        if (!agent_loaded_) {
          HandleDataWithoutAgent(self);
        } else if (agent_listening_ && !sent_agent_fds_) {
          VLOG(jdwp) << "Sending agent fds again on data.";
          // Agent was already loaded so it can deal with the handshake.
          SendAgentFds(/*require_handshake=*/ true);
        }
      } else if (FlagsSet(adb_socket_poll.revents, POLLRDHUP)) {
        CHECK(IsDebuggingPossible());  // This path is unexpected for a profileable process.
        DCHECK(!agent_has_socket_);
        CloseFds();
      } else {
        VLOG(jdwp) << "Woke up poll without anything to do!";
      }
    }
  }
}

static uint32_t ReadUint32AndAdvance(/*in-out*/uint8_t** in) {
  uint32_t res;
  memcpy(&res, *in, sizeof(uint32_t));
  *in = (*in) + sizeof(uint32_t);
  return ntohl(res);
}

void AdbConnectionState::HandleDataWithoutAgent(art::Thread* self) {
  DCHECK(!agent_loaded_);
  DCHECK(!agent_listening_);
  // TODO Should we check in some other way if we are userdebug/eng?
  CHECK(art::Dbg::IsJdwpAllowed());
  // We try to avoid loading the agent which is expensive. First lets just perform the handshake.
  if (!performed_handshake_) {
    PerformHandshake();
    return;
  }
  // Read the packet header to figure out if it is one we can handle. We only 'peek' into the stream
  // to see if it's one we can handle. This doesn't change the state of the socket.
  alignas(sizeof(uint32_t)) uint8_t packet_header[kPacketHeaderLen];
  ssize_t res = TEMP_FAILURE_RETRY(recv(adb_connection_socket_.get(),
                                        packet_header,
                                        sizeof(packet_header),
                                        MSG_PEEK));
  // We want to be very careful not to change the socket state until we know we succeeded. This will
  // let us fall-back to just loading the agent and letting it deal with everything.
  if (res <= 0) {
    // Close the socket. We either hit EOF or an error.
    if (res < 0) {
      PLOG(ERROR) << "Unable to peek into adb socket due to error. Closing socket.";
    }
    CloseFds();
    return;
  } else if (res < static_cast<int>(kPacketHeaderLen)) {
    LOG(ERROR) << "Unable to peek into adb socket. Loading agent to handle this. Only read " << res;
    AttachJdwpAgent(self);
    return;
  }
  uint32_t full_len = ntohl(*reinterpret_cast<uint32_t*>(packet_header + kPacketSizeOff));
  uint32_t pkt_id = ntohl(*reinterpret_cast<uint32_t*>(packet_header + kPacketIdOff));
  uint8_t pkt_cmd_set = packet_header[kPacketCommandSetOff];
  uint8_t pkt_cmd = packet_header[kPacketCommandOff];
  if (pkt_cmd_set != kDdmCommandSet ||
      pkt_cmd != kDdmChunkCommand ||
      full_len < kPacketHeaderLen) {
    VLOG(jdwp) << "Loading agent due to jdwp packet that cannot be handled by adbconnection.";
    AttachJdwpAgent(self);
    return;
  }
  uint32_t avail = -1;
  res = TEMP_FAILURE_RETRY(ioctl(adb_connection_socket_.get(), FIONREAD, &avail));
  if (res < 0) {
    PLOG(ERROR) << "Failed to determine amount of readable data in socket! Closing connection";
    CloseFds();
    return;
  } else if (avail < full_len) {
    LOG(WARNING) << "Unable to handle ddm command in adbconnection due to insufficent data. "
                 << "Expected " << full_len << " bytes but only " << avail << " are readable. "
                 << "Loading jdwp agent to deal with this.";
    AttachJdwpAgent(self);
    return;
  }
  // Actually read the data.
  std::vector<uint8_t> full_pkt;
  full_pkt.resize(full_len);
  res = TEMP_FAILURE_RETRY(recv(adb_connection_socket_.get(), full_pkt.data(), full_len, 0));
  if (res < 0) {
    PLOG(ERROR) << "Failed to recv data from adb connection. Closing connection";
    CloseFds();
    return;
  }
  DCHECK_EQ(memcmp(full_pkt.data(), packet_header, sizeof(packet_header)), 0);
  size_t data_size = full_len - kPacketHeaderLen;
  if (data_size < (sizeof(uint32_t) * 2)) {
    // This is an error (the data isn't long enough) but to match historical behavior we need to
    // ignore it.
    return;
  }
  uint8_t* ddm_data = full_pkt.data() + kPacketHeaderLen;
  uint32_t ddm_type = ReadUint32AndAdvance(&ddm_data);
  uint32_t ddm_len = ReadUint32AndAdvance(&ddm_data);
  if (ddm_len > data_size - (2 * sizeof(uint32_t))) {
    // This is an error (the data isn't long enough) but to match historical behavior we need to
    // ignore it.
    return;
  }

  if (!notified_ddm_active_) {
    NotifyDdms(/*active=*/ true);
  }
  uint32_t reply_type;
  std::vector<uint8_t> reply;
  if (!art::Dbg::DdmHandleChunk(self->GetJniEnv(),
                                ddm_type,
                                art::ArrayRef<const jbyte>(reinterpret_cast<const jbyte*>(ddm_data),
                                                           ddm_len),
                                /*out*/&reply_type,
                                /*out*/&reply)) {
    // To match historical behavior we don't send any response when there is no data to reply with.
    return;
  }
  SendDdmPacket(pkt_id,
                DdmPacketType::kReply,
                reply_type,
                art::ArrayRef<const uint8_t>(reply));
}

void AdbConnectionState::PerformHandshake() {
  CHECK(!performed_handshake_);
  // Check to make sure we are able to read the whole handshake.
  uint32_t avail = -1;
  int res = TEMP_FAILURE_RETRY(ioctl(adb_connection_socket_.get(), FIONREAD, &avail));
  if (res < 0 || avail < sizeof(kJdwpHandshake)) {
    if (res < 0) {
      PLOG(ERROR) << "Failed to determine amount of readable data for handshake!";
    }
    LOG(WARNING) << "Closing connection to broken client.";
    CloseFds();
    return;
  }
  // Perform the handshake.
  char handshake_msg[sizeof(kJdwpHandshake)];
  res = TEMP_FAILURE_RETRY(recv(adb_connection_socket_.get(),
                                handshake_msg,
                                sizeof(handshake_msg),
                                MSG_DONTWAIT));
  if (res < static_cast<int>(sizeof(kJdwpHandshake)) ||
      strncmp(handshake_msg, kJdwpHandshake, sizeof(kJdwpHandshake)) != 0) {
    if (res < 0) {
      PLOG(ERROR) << "Failed to read handshake!";
    }
    LOG(WARNING) << "Handshake failed!";
    CloseFds();
    return;
  }
  // Send the handshake back.
  res = TEMP_FAILURE_RETRY(send(adb_connection_socket_.get(),
                                kJdwpHandshake,
                                sizeof(kJdwpHandshake),
                                0));
  if (res < static_cast<int>(sizeof(kJdwpHandshake))) {
    PLOG(ERROR) << "Failed to send jdwp-handshake response.";
    CloseFds();
    return;
  }
  performed_handshake_ = true;
}

void AdbConnectionState::AttachJdwpAgent(art::Thread* self) {
  art::Runtime* runtime = art::Runtime::Current();
  self->AssertNoPendingException();
  runtime->AttachAgent(/* env= */ nullptr,
                       MakeAgentArg(),
                       /* class_loader= */ nullptr);
  if (self->IsExceptionPending()) {
    LOG(ERROR) << "Failed to load agent " << agent_name_;
    art::ScopedObjectAccess soa(self);
    self->GetException()->Dump();
    self->ClearException();
    return;
  }
  agent_loaded_ = true;
}

bool ContainsArgument(const std::string& opts, const char* arg) {
  return opts.find(arg) != std::string::npos;
}

bool ValidateJdwpOptions(const std::string& opts) {
  bool res = true;
  // The adbconnection plugin requires that the jdwp agent be configured as a 'server' because that
  // is what adb expects and otherwise we will hit a deadlock as the poll loop thread stops waiting
  // for the fd's to be passed down.
  if (ContainsArgument(opts, "server=n")) {
    res = false;
    LOG(ERROR) << "Cannot start jdwp debugging with server=n from adbconnection.";
  }
  // We don't start the jdwp agent until threads are already running. It is far too late to suspend
  // everything.
  if (ContainsArgument(opts, "suspend=y")) {
    res = false;
    LOG(ERROR) << "Cannot use suspend=y with late-init jdwp.";
  }
  return res;
}

std::string AdbConnectionState::MakeAgentArg() {
  const std::string& opts = art::Runtime::Current()->GetJdwpOptions();
  DCHECK(ValidateJdwpOptions(opts));
  // TODO Get agent_name_ from something user settable?
  return agent_name_ + "=" + opts + (opts.empty() ? "" : ",") +
      "ddm_already_active=" + (notified_ddm_active_ ? "y" : "n") + "," +
      // See the comment above for why we need to be server=y. Since the agent defaults to server=n
      // we will add it if it wasn't already present for the convenience of the user.
      (ContainsArgument(opts, "server=y") ? "" : "server=y,") +
      // See the comment above for why we need to be suspend=n. Since the agent defaults to
      // suspend=y we will add it if it wasn't already present.
      (ContainsArgument(opts, "suspend=n") ? "" : "suspend=n,") +
      "transport=dt_fd_forward,address=" + std::to_string(remote_agent_control_sock_);
}

void AdbConnectionState::StopDebuggerThreads() {
  // The regular agent system will take care of unloading the agent (if needed).
  shutting_down_ = true;
  // Wakeup the poll loop.
  uint64_t data = 1;
  if (sleep_event_fd_ != -1) {
    TEMP_FAILURE_RETRY(write(sleep_event_fd_, &data, sizeof(data)));
  }
}

// The plugin initialization function.
extern "C" bool ArtPlugin_Initialize() {
  DCHECK(art::Runtime::Current()->GetJdwpProvider() == art::JdwpProvider::kAdbConnection);
  // TODO Provide some way for apps to set this maybe?
  gState.emplace(kDefaultJdwpAgentName);
  return ValidateJdwpOptions(art::Runtime::Current()->GetJdwpOptions());
}

extern "C" bool ArtPlugin_Deinitialize() {
  // We don't actually have to do anything here. The debugger (if one was
  // attached) was shutdown by the move to the kDeath runtime phase and the
  // adbconnection threads were shutdown by StopDebugger.
  return true;
}

}  // namespace adbconnection
