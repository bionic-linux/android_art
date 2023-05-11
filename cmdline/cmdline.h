    return true;    *filename = cache_filename;                             InstructionSet instruction_set,  CHECK(boot_image_location != nullptr);  {    options.push_back(std::make_pair("compilercallbacks", &callbacks));
  {
    std::string boot_image_option;
    boot_image_option += "-Ximage:";
    boot_image_option += boot_image_location;
    options.push_back(std::make_pair(boot_image_option, nullptr));
  }

  // Instruction set.
  options.push_back(
      std::make_pair("imageinstructionset",
                     reinterpret_cast<const void*>(GetInstructionSetString(instruction_set))));

  // Explicit runtime args.
  for (const char* runtime_arg : runtime_args) {
    options.push_back(std::make_pair(runtime_arg, nullptr));
  }

  // None of the command line tools need sig chain. If this changes we'll need
  // to upgrade this option to a proper parameter.
  options.push_back(std::make_pair("-Xno-sig-chain", nullptr));
  if (!Runtime::Create(options, false)) {
    fprintf(stderr, "Failed to create runtime\n");
    return nullptr;
  }

  // Runtime::Create acquired the mutator_lock_ that is normally given away when we Runtime::Start,
  // give it away now and then switch to a more manageable ScopedObjectAccess.
  Thread::Current()->TransitionFromRunnableToSuspended(kNative);

  return Runtime::Current();
}

struct CmdlineArgs {
  enum ParseStatus {
    kParseOk,               // Parse successful. Do not set the error message.
    kParseUnknownArgument,  // Unknown argument. Do not set the error message.
    kParseError,            // Parse ok, but failed elsewhere. Print the set error message.
  };

  bool Parse(int argc, char** argv) {
    // Skip over argv[0].
    argv++;
    argc--;

    if (argc == 0) {
      fprintf(stderr, "No arguments specified\n");
      PrintUsage();
      return false;
    }

    std::string error_msg;
    for (int i = 0; i < argc; i++) {
      const char* const raw_option = argv[i];
      const std::string_view option(raw_option);
      if (StartsWith(option, "--boot-image=")) {
        boot_image_location_ = raw_option + strlen("--boot-image=");
      } else if (StartsWith(option, "--instruction-set=")) {
        const char* const instruction_set_str = raw_option + strlen("--instruction-set=");
        instruction_set_ = GetInstructionSetFromString(instruction_set_str);
        if (instruction_set_ == InstructionSet::kNone) {
          fprintf(stderr, "Unsupported instruction set %s\n", instruction_set_str);
          PrintUsage();
          return false;
        }
      } else if (option == "--runtime-arg") {
        if (i + 1 == argc) {
          fprintf(stderr, "Missing argument for --runtime-arg\n");
          PrintUsage();
          return false;
        }
        ++i;
        runtime_args_.push_back(argv[i]);
      } else if (StartsWith(option, "--output=")) {
        output_name_ = std::string(option.substr(strlen("--output=")));
        const char* filename = output_name_.c_str();
        out_.reset(new std::ofstream(filename));
        if (!out_->good()) {
          fprintf(stderr, "Failed to open output filename %s\n", filename);
          PrintUsage();
          return false;
        }
        os_ = out_.get();
      } else {
        ParseStatus parse_status = ParseCustom(raw_option, option.length(), &error_msg);

        if (parse_status == kParseUnknownArgument) {
          fprintf(stderr, "Unknown argument %s\n", option.data());
        }

        if (parse_status != kParseOk) {
          fprintf(stderr, "%s\n", error_msg.c_str());
          PrintUsage();
          return false;
        }
      }
    }

    DBG_LOG << "will call parse checks";

    {
      ParseStatus checks_status = ParseChecks(&error_msg);
      if (checks_status != kParseOk) {
          fprintf(stderr, "%s\n", error_msg.c_str());
          PrintUsage();
          return false;
      }
    }

    return true;
  }

  virtual std::string GetUsage() const {
    std::string usage;

    usage +=  // Required.
        "  --boot-image=<file.art>: provide the image location for the boot class path.\n"
        "      Do not include the arch as part of the name, it is added automatically.\n"
        "      Example: --boot-image=/system/framework/boot.art\n"
        "               (specifies /system/framework/<arch>/boot.art as the image file)\n"
        "\n";
    usage += android::base::StringPrintf(  // Optional.
        "  --instruction-set=(arm|arm64|mips|mips64|x86|x86_64): for locating the image\n"
        "      file based on the image location set.\n"
        "      Example: --instruction-set=x86\n"
        "      Default: %s\n"
        "\n",
        GetInstructionSetString(kRuntimeISA));
    usage +=
        "  --runtime-arg <argument> used to specify various arguments for the runtime\n"
        "      such as initial heap size, maximum heap size, and verbose output.\n"
        "      Use a separate --runtime-arg switch for each argument.\n"
        "      Example: --runtime-arg -Xms256m\n"
        "\n";
    usage +=  // Optional.
        "  --output=<file> may be used to send the output to a file.\n"
        "      Example: --output=/tmp/oatdump.txt\n"
        "\n";

    return usage;
  }

  // Specified by --boot-image.
  const char* boot_image_location_ = nullptr;
  // Specified by --instruction-set.
  InstructionSet instruction_set_ = InstructionSet::kNone;
  // Runtime arguments specified by --runtime-arg.
  std::vector<const char*> runtime_args_;
  // Specified by --output.
  std::ostream* os_ = &std::cout;
  std::unique_ptr<std::ofstream> out_;  // If something besides cout is used
  std::string output_name_;

  virtual ~CmdlineArgs() {}

  bool ParseCheckBootImage(std::string* error_msg) {
    if (boot_image_location_ == nullptr) {
      *error_msg = "--boot-image must be specified";
      return false;
    }
    if (instruction_set_ == InstructionSet::kNone) {
      LOG(WARNING) << "No instruction set given, assuming " << GetInstructionSetString(kRuntimeISA);
      instruction_set_ = kRuntimeISA;
    }

    DBG_LOG << "boot image location: " << boot_image_location_;

    // Checks for --boot-image location.
    {
      std::string boot_image_location = boot_image_location_;
      size_t file_name_idx = boot_image_location.rfind('/');
      if (file_name_idx == std::string::npos) {  // Prevent a InsertIsaDirectory check failure.
        *error_msg = "Boot image location must have a / in it";
        return false;
      }

      // Don't let image locations with the 'arch' in it through, since it's not a location.
      // This prevents a common error "Could not create an image space..." when initing the Runtime.
      if (file_name_idx != std::string::npos) {
        std::string no_file_name = boot_image_location.substr(0, file_name_idx);
        size_t ancestor_dirs_idx = no_file_name.rfind('/');

        std::string parent_dir_name;
        if (ancestor_dirs_idx != std::string::npos) {
          parent_dir_name = no_file_name.substr(ancestor_dirs_idx + 1);
        } else {
          parent_dir_name = no_file_name;
        }

        DBG_LOG << "boot_image_location parent_dir_name was " << parent_dir_name;

        if (GetInstructionSetFromString(parent_dir_name.c_str()) != InstructionSet::kNone) {
          *error_msg = "Do not specify the architecture as part of the boot image location";
          return false;
        }
      }

      // Check that the boot image location points to a valid file name.
      std::string file_name;
      if (!LocationToFilename(boot_image_location, instruction_set_, &file_name)) {
        *error_msg = android::base::StringPrintf(
            "No corresponding file for location '%s' (filename '%s') exists",
            boot_image_location.c_str(),
            file_name.c_str());
        return false;
      }

      DBG_LOG << "boot_image_filename does exist: " << file_name;
    }

    return true;
  }

  void PrintUsage() {
    fprintf(stderr, "%s", GetUsage().c_str());
  }

 protected:
  virtual ParseStatus ParseCustom(const char* raw_option ATTRIBUTE_UNUSED,
                                  size_t raw_option_length ATTRIBUTE_UNUSED,
                                  std::string* error_msg ATTRIBUTE_UNUSED) {
    return kParseUnknownArgument;
  }

  virtual ParseStatus ParseChecks(std::string* error_msg ATTRIBUTE_UNUSED) {
    return kParseOk;
  }
};

template <typename Args = CmdlineArgs>
struct CmdlineMain {
  int Main(int argc, char** argv) {
    Locks::Init();
    InitLogging(argv, Runtime::Abort);
    std::unique_ptr<Args> args = std::unique_ptr<Args>(CreateArguments());
    args_ = args.get();

    DBG_LOG << "Try to parse";

    if (args_ == nullptr || !args_->Parse(argc, argv)) {
      return EXIT_FAILURE;
    }

    bool needs_runtime = NeedsRuntime();
    std::unique_ptr<Runtime> runtime;


    if (needs_runtime) {
      std::string error_msg;
      if (!args_->ParseCheckBootImage(&error_msg)) {
        fprintf(stderr, "%s\n", error_msg.c_str());
        args_->PrintUsage();
        return EXIT_FAILURE;
      }
      runtime.reset(CreateRuntime(args.get()));
      if (runtime == nullptr) {
        return EXIT_FAILURE;
      }
      if (!ExecuteWithRuntime(runtime.get())) {
        return EXIT_FAILURE;
      }
    } else {
      if (!ExecuteWithoutRuntime()) {
        return EXIT_FAILURE;
      }
    }

    if (!ExecuteCommon()) {
      return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
  }

  // Override this function to create your own arguments.
  // Usually will want to return a subtype of CmdlineArgs.
  virtual Args* CreateArguments() {
    return new Args();
  }

  // Override this function to do something else with the runtime.
  virtual bool ExecuteWithRuntime(Runtime* runtime) {
    CHECK(runtime != nullptr);
    // Do nothing
    return true;
  }

  // Does the code execution need a runtime? Sometimes it doesn't.
  virtual bool NeedsRuntime() {
    return true;
  }

  // Do execution without having created a runtime.
  virtual bool ExecuteWithoutRuntime() {
    return true;
  }

  // Continue execution after ExecuteWith[out]Runtime
  virtual bool ExecuteCommon() {
    return true;
  }

  virtual ~CmdlineMain() {}

 protected:
  Args* args_ = nullptr;

 private:
  Runtime* CreateRuntime(CmdlineArgs* args) {
    CHECK(args != nullptr);

    return StartRuntime(args->boot_image_location_, args->instruction_set_, args_->runtime_args_);
  }
};
}  // namespace art

#endif  // ART_CMDLINE_CMDLINE_H_
