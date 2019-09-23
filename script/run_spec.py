import sys
from subprocess import *

##### EDIT #############

# Root directory of SPEC CPU 2017
SPEC_ROOT = "/home/xm13/projects/spec_cpu2017"

# One compiler per configuration file
config_list = [ ("GCC-6.4.0", "gcc-test-liteCFI-single-thread-gcc-6.4.0.cfg"), \
        ("GCC-7.3.0", "gcc-test-liteCFI-single-thread-gcc-7.3.0.cfg"), \
        ("GCC-8.3.0", "gcc-test-liteCFI-single-thread-gcc-8.3.0.cfg") ]

# The list of benchmark to run
tests_list = ["600.perlbench_s", \
        "602.gcc_s", \
        "605.mcf_s", \
        "620.omnetpp_s", \
        "623.xalancbmk_s", \
        "625.x264_s", \
        "631.deepsjeng_s", \
        "641.leela_s", \
        "648.exchange2_s", \
        "657.xz_s"]

###### END OF EDIT ############

def Failed(output):
    if output.find("Error") != -1:
        return True
    return False

def Passed(output):
    if output.find("Success:") != -1:
        return True
    return False

def Run(config, test):
    cmd = "source shrc && "
    cmd += "runcpu --config={0} {1}".format(config, test)
    p = Popen(cmd, stdout=PIPE, stderr=PIPE,  shell=True, cwd=SPEC_ROOT, executable='/bin/bash')
    msg, err = p.communicate()
    if Failed(msg + err):
        return "Failed"
    elif Passed(msg + err):
        return "Passed"
    else:
        return "Unknown"

for compiler, config in config_list:
    for test in tests_list:
        ret = Run(config, test)
        print compiler, test, ret




