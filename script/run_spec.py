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
#        "602.gcc_s", \
        "605.mcf_s", \
        "620.omnetpp_s", \
        "623.xalancbmk_s", \
        "625.x264_s", \
        "631.deepsjeng_s", \
        "641.leela_s", \
        "648.exchange2_s", \
        "657.xz_s"]

PARALLEL = 5 

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
    return p, config, test

def CleanOldDirs():
    cmd = "rm -rf benchspec/C*/*/run; "
    cmd += "rm -rf benchspec/C*/*/build; "
    cmd += "rm -rf benchspec/C*/*/exe "
    p = Popen(cmd, stdout=PIPE, stderr=PIPE,  shell=True, cwd=SPEC_ROOT, executable='/bin/bash')
    msg, err = p.communicate()
    if len(err) > 0:
        print err

def WaitForResults(runs, results):
    for p, config, test in runs:
        msg, err = p.communicate()
        if Failed(msg + err):
            ret = "Fail"
        elif Passed(msg + err):
            ret = "Pass"
        else:
            ret = "Unknown"
        results[compiler][test] = ret
        print compiler, test, ret

results = {}
for compiler, config in config_list:
    CleanOldDirs()
    results[compiler] = {}
    runs = []
    for test in tests_list:
        runs.append( Run(config, test) )
        if len(runs) == PARALLEL:
            WaitForResults(runs, results)
            runs = []
    if len(runs) > 0:
        WaitForResults(runs, results)


line = "| SPEC "
for compiler, config in config_list:
    line += " | {0}".format(compiler)
line += " | "
print line
line = "| --- | "
for compiler, config in config_list:
    line += " --- | "
print line

for test in tests_list:
    line = "| {0}".format(test)
    for compiler, config in config_list:
        line += " | {0}".format(results[compiler][test])
    line += " |"
    print line




