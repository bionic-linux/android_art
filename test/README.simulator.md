# ART VIXL Simulator Integration

This file documents the use of the VIXL Simulator for running tests on ART. The
simulator enables us to run the ART run-tests without the need for a target
device. This helps to speed up the development/debug/test cycle. The full AOSP
source tree, as well as the partial master-art AOSP source tree, are supported.

## Quick User Guide
1. Set lunch target and setup environment:

    ```bash
    source build/envsetup.sh; lunch armv8-eng
    ```

2. Build ART target and host:

    ```bash
    art/tools/buildbot-build.sh --target
    art/tools/buildbot-build.sh --host
    ```

3. Run Tests:

    To enable the simulator we use the `--simulator` flag. The simulator can
    be used directly with the dalvikvm or the ART test scripts.

    To run a single test on simulator, use the command:
    ```bash
    art/test/run-test --host --simulator --64 <TEST_NAME>
    ```

    To run all ART run-tests on simulator, use the `art/test.py` script with the
    following command:
    ```bash
    ./art/test.py --simulator --run-test --optimizing
    ```

4. Enable simulator tracing

    Simulator provides tracing feature which is useful in debugging. Setting
    runtime option `-verbose:simulator` will enable instruction trace and register
    updates.
    For example,
    ```bash
    ./art/test/run-test --host --runtime-option -verbose:simulator --optimizing \
      --never-clean --simulator --64 640-checker-simd
    ```

5. Debug

    Another useful usecase of the simulator is debugging using the `--gdb` flag.
    ```bash
    ./art/test/run-test --gdb --host --simulator --64 527-checker-array-access-split
    ```
    If developing a compiler optimization which affects the test case
    `527-checker-array-access-split`, you can use the simulator to run and
    generate the control flow graph with:
    ```bash
    ./art/test/run-test --host --dex2oat-jobs 1 -Xcompiler-option --dump-cfg=oat.cfg \
      --never-clean --simulator --64 527-checker-array-access-split
    ```
