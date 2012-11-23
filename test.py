# Automated script to run the pspautotests test suite in PPSSPP.

import sys
import io
import os
import subprocess
import threading


PPSSPP_EXECUTABLES = [ "Windows\\Release\\PPSSPPHeadless.exe", "SDL/build/ppsspp-headless" ]
PPSSPP_EXE = None
TEST_ROOT = "pspautotests/tests/"
teamcity_mode = False
TIMEOUT = 5

class Command(object):
  def __init__(self, cmd):
    self.cmd = cmd
    self.process = None
    self.output = None
    self.timeout = False

  def run(self, timeout):
    def target():
      self.process = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
      self.output, _ = self.process.communicate()

    thread = threading.Thread(target=target)
    thread.start()

    thread.join(timeout)
    if thread.is_alive():
      self.timeout = True
      self.process.terminate()
      thread.join()

# Test names are the C files without the .c extension.
# These have worked and should keep working always - regression tests.
tests_good = [
  "cpu/cpu/cpu",
  "cpu/vfpu/base/vfpu",
  "cpu/vfpu/convert/vfpu_convert",
  "cpu/vfpu/prefixes/vfpu_prefixes",
  "cpu/vfpu/colors/vfpu_colors",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
  "cpu/fpu/fpu",

  "display/display",
  "dmac/dmactest",
  "intr/intr",
  "intr/vblank/vblank",
  "misc/testgp",
  "string/string",
  "gpu/callbacks/ge_callbacks",
  "threads/mbx/mbx",
  "threads/mutex/mutex",
  "threads/mutex/delete/delete",
  "threads/semaphores/semaphores",
  "threads/semaphores/delete/delete",
  "threads/semaphores/poll/poll",
  "threads/semaphores/refer/refer",
  "threads/semaphores/signal/signal",
  "power/power",
  "umd/callbacks/umd",
  "io/directory/directory",
]

tests_next = [
  "malloc/malloc",
# These are the next tests up for fixing. These run by default.
  "threads/fpl/fpl",
  "threads/msgpipe/msgpipe",
  "threads/mutex/create/create",
  "threads/mutex/lock/lock",
  "threads/mutex/priority/priority",
  "threads/mutex/try/try",
  "threads/mutex/unlock/unlock",
  "threads/scheduling/scheduling",
  "threads/semaphores/cancel/cancel",
  "threads/semaphores/create/create",
  "threads/semaphores/priority/priority",
  "threads/semaphores/wait/wait",
  "threads/threads/threads",
  "threads/vpl/vpl",
  "threads/vtimers/vtimer",
  "threads/wakeup/wakeup",
  "audio/sascore/sascore",
  "ctrl/ctrl",
  "gpu/simple/simple",
  "gpu/triangle/triangle",
  "hle/check_not_used_uids",
  "font/fonttest",
  "io/cwd/cwd",
  "io/io/io",
  "io/iodrv/iodrv",
  "modules/loadexec/loader",
  "rtc/rtc",
  "threads/k0/k0",
  "threads/fpl/fpl",
  "threads/msgpipe/msgpipe",
  "threads/mutex/mutex",
  "threads/scheduling/scheduling",
  "threads/threads/threads",
  "threads/vpl/vpl",
  "threads/vtimers/vtimers",
  "threads/wakeup/wakeup",
  "umd/io/umd_io",
  "umd/raw_access/raw_acess",
  "utility/systemparam/systemparam",
  "video/pmf",
  "video/pmf_simple",

  # Currently hang or crash.
  "threads/events/events",
  "audio/atrac/atractest",
]

# These don't even run (or run correctly) on the real PSP
test_broken = [
  "sysmem/sysmem",
  "mstick/mstick",
]


# These are the tests we ignore (not important, or impossible to run)
tests_ignored = [
  "kirk/kirk",
  "me/me",

  "umd/umd", # mostly fixed but output seems broken? (first retval of unregister...)
]



def init():
  global PPSSPP_EXE
  if not os.path.exists("pspautotests"):
    print "Please run git submodule init; git submodule update;"
    sys.exit(1)

  if not os.path.exists(TEST_ROOT + "cpu/cpu/cpu.prx"):
    print "Please install the pspsdk and run make in common/ and in all the tests" 
    sys.exit(1)

  for p in PPSSPP_EXECUTABLES:
    if os.path.exists(p):
      PPSSPP_EXE = p
      break

  if not PPSSPP_EXE:
    print "PPSSPP executable missing, please build one."
    sys.exit(1)

def tcprint(arg):
  global teamcity_mode
  if teamcity_mode:
    print arg

def run_tests(test_list, args):
  global PPSSPP_EXE, TIMEOUT
  tests_passed = []
  tests_failed = []
  
  for test in test_list:
    # Try prx first
    expected_filename = TEST_ROOT + test + ".expected"

    elf_filename = TEST_ROOT + test + ".prx"
    print elf_filename

    if not os.path.exists(elf_filename):
      print "WARNING: no prx, trying elf"
      elf_filename = TEST_ROOT + test + ".elf"

    if not os.path.exists(elf_filename):
      print("ERROR: PRX/ELF file missing, failing test: " + test)
      tests_failed.append(test)
      tcprint("##teamcity[testIgnored name='%s' message='PRX/ELF missing']" % test)
      continue

    if not os.path.exists(expected_filename):
      print("WARNING: expects file missing, failing test: " + test)
      tests_failed.append(test)
      tcprint("##teamcity[testIgnored name='%s' message='Expects file missing']" % test)
      continue

    expected_output = open(expected_filename).read()
    
    tcprint("##teamcity[testStarted name='%s' captureStandardOutput='true']" % test)

    cmdline = [PPSSPP_EXE, elf_filename]
    cmdline.extend(args)

    c = Command(cmdline)
    c.run(TIMEOUT)

    output = c.output.strip()
    
    if c.timeout:
      print output
      print "Test exceded limit of %d seconds." % TIMEOUT
      tcprint("##teamcity[testFailed name='%s' message='Test timeout']" % test)
      tcprint("##teamcity[testFinished name='%s']" % test)
      continue

    if output.startswith("TESTERROR"):
      print "Failed to run test " + elf_filename + "!"
      tests_failed.append(test)
      tcprint("##teamcity[testFailed name='%s' message='Failed to run test']" % test)
      tcprint("##teamcity[testFinished name='%s']" % test)
      continue

    different = False
    expected_lines = expected_output.splitlines()
    output_lines = output.splitlines()
    
    for i in range(0, min(len(output_lines), len(expected_lines))):
      if output_lines[i] != expected_lines[i]:
        print "%i < %s" % (i, output_lines[i])
        print "%i > %s" % (i, expected_lines[i])
        different = True

    if len(output_lines) != len(expected_lines):
      print "*** Different number of lines!"
      different = True

    if not different:
      if '-v' in args:
        print "++++++++++++++ The Equal Output +++++++++++++"
        print "\n".join(output_lines)
        print "+++++++++++++++++++++++++++++++++++++++++++++"
      print "  " + test + " - passed!"
      tests_passed.append(test)
      tcprint("##teamcity[testFinished name='%s']" % test)
    else:
      if '-v' in args:
        print "============== output from failed " + test + " :"
        print output
        print "============== expected output:"
        print expected_output
        print "==============================="
      tests_failed.append(test)
      tcprint("##teamcity[testFailed name='%s' message='Output different from expected file']" % test)
      tcprint("##teamcity[testFinished name='%s']" % test)

  print "%i tests passed, %i tests failed." % (
      len(tests_passed), len(tests_failed))

  if len(tests_failed):
    print "Failed tests:"
    for t in tests_failed:
      print "  " + t
  print "Ran " + PPSSPP_EXE


def main():
  global teamcity_mode
  init()
  tests = []
  args = []
  for arg in sys.argv[1:]:
    if arg == '--teamcity':
      teamcity_mode = True
    elif arg[0] == '-':
      args.append(arg)
    else:
      tests.append(arg)

  if not tests:
    if '-g' in args:
      tests = tests_good
    else:
      tests = tests_next + tests_good

  run_tests(tests, args)

main()
