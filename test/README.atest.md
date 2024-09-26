# Running ART Tests with Atest / Trade Federation

ART Testing has early support for execution in the [Trade
Federation](https://source.android.com/devices/tech/test_infra/tradefed)
("TradeFed") test harness, in particular via the
[Atest](https://source.android.com/compatibility/tests/development/atest)
command line tool.

Atest conveniently takes care of building tests and their dependencies (using
Soong, the Android build system) and executing them using Trade Federation.

See also [README.md](README.md) for a general introduction to ART run-tests and
gtests.

## ART run-tests

### Running ART run-tests on device

ART run-tests are defined in sub-directories of `test/` starting with a number
(e.g. `test/001-HelloWorld`). Each ART run-test is identified in the build
system by a Soong module name following the `art-run-test-`*`<test-directory>`*
format (e.g. `art-run-test-001-HelloWorld`).

You can run a specific ART run-test on device by passing its Soong module name
to Atest:
```bash
atest art-run-test-001-HelloWorld
```

To run all ART run-tests in a single command, the currently recommended way is
to use [test mapping](#test-mapping) (see below).

You can nonetheless run all supported ART run-tests with a single Atest command,
using its support for wildcards:
```bash
atest art-run-test-\*
```

Note: Many ART run-tests are failing with the TradeFed harness as of March 2021,
so the above Atest command will likely report many tests failures. The ART team
is actively working on this issue.

## ART gtests

### Running standalone ART gtests on device

Standalone ART gtests can be used to test the ART APEX presently residing on a
device (either the original one, located in the "system" partition, or an
updated package, present in the "data" partition).

Standalone ART gtests are defined as Soong modules `art_standalone_*_tests`. You
can run them individually with Atest, e.g:

```bash
atest art_standalone_cmdline_tests
```

You can also run all of them with a single Atest command, using its support for
wildcards:

```bash
atest art_standalone_\*_tests
```

The previous commands build the corresponding ART gtests and their dependencies,
dynamically link them against local ART APEX libraries (in the source tree), and
run them on device against the active ART APEX.

### Running ART gtests on host

You first need to build the boot classpath and boot image on host:

```bash
m art-host-tests
```

Then you can use `atest --host` to run host gtests, e.g:

```bash
atest --host art_runtime_tests
```

## Test Mapping

ART Testing supports the execution of tests via [Test
Mapping](https://source.android.com/compatibility/tests/development/test-mapping).
The tests declared in ART's [TEST_MAPPING](../TEST_MAPPING) file are executed
during pre-submit testing (when an ART changelist in Gerrit is verified by
Treehugger) and/or post-submit testing (when a given change is merged in the
Android code base), depending on the "test group" where a test is declared.

### Running tests via Test Mapping with Atest

It is possible to run tests via test mapping locally using Atest.

To run all the tests declared in ART's `TEST_MAPPING` file, use the following
command from the Android source tree top-level directory:
```bash
atest --test-mapping art:all
```
In the previous command, `art` is the (relative) path to the directory
containing the `TEST_MAPPING` file listing the tests to run, while `all` means
that tests declared in all [test
groups](https://source.android.com/compatibility/tests/development/test-mapping#defining_test_groups)
shall be run.

To only run tests executed during pre-submit testing, use:
```bash
atest --test-mapping art:presubmit
```
