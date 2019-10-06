import sys
import time
from subprocess import *

##### EDIT #############

# Root directory of SPEC CPU 2017
SPEC_ROOT = "/home/xm13/projects/spec_cpu2017"

# One compiler per configuration file
config_list = [ ("GCC-6.4.0", "single-thread-gcc-6.4.0", "gcc-6.4.0-gcc-4.8.5-pcrkwvm"), \
        ("GCC-7.4.0", "single-thread-gcc-7.4.0", "gcc-7.4.0-gcc-6.4.0-ja3xbno"), \
        ("GCC-8.3.0", "single-thread-gcc-8.3.0", "gcc-8.3.0-gcc-7.3.0-qowc552") ]

# The list of benchmark to run
tests_list = ["600.perlbench_s", \
        "602.gcc_s", \
        "603.bwaves_s", \
        "605.mcf_s", \
        "607.cactuBSSN_s", \
        "619.lbm_s", \
        "620.omnetpp_s", \
        "621.wrf_s", \
        "623.xalancbmk_s", \
        "625.x264_s", \
#        "627.cam4_s", \
        "628.pop2_s", \
        "631.deepsjeng_s", \
        "638.imagick_s", \
        "641.leela_s", \
        "644.nab_s", \
        "648.exchange2_s", \
        "649.fotonik3d_s", \
        "654.roms_s", \
        "657.xz_s"]

#test_mode = ["light", "full"]
test_mode = ["full"]

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

def Run(mode, compiler, config, test, module):
    cmd = "source shrc && "
    cmd += "module unload gcc-6.4.0-gcc-4.8.5-pcrkwvm && "
    cmd += "module load {0} && ".format(module)
    cmd += "runcpu --config={0}-{2}.cfg {1}".format(config, test, mode)
    p = Popen(cmd, stdout=PIPE, stderr=PIPE,  shell=True, cwd=SPEC_ROOT, executable='/bin/bash')
    return p, mode, compiler, config, test

def CleanOldDirs():
    cmd = "rm -rf benchspec/C*/*/run; "
    cmd += "rm -rf benchspec/C*/*/build; "
    cmd += "rm -rf benchspec/C*/*/exe "
    p = Popen(cmd, stdout=PIPE, stderr=PIPE,  shell=True, cwd=SPEC_ROOT, executable='/bin/bash')
    msg, err = p.communicate()
    if len(err) > 0:
        print err

def GenerateTestConfigurations():
    tests = []
    for mode in test_mode:
        for compiler, config, module in config_list:
            for bench in tests_list:
                tests.append ( (mode, compiler, config, bench, module) )
    return tests

def WaitForFinish(results, runs):
    while True:
        for i in range(len(runs)):
            p = runs[i][0]
            if p.poll() is not None:
                # This test is done
                out = p.stdout.read() + p.stderr.read()
                if Failed(out):
                    ret = "Fail"
                elif Passed(out):
                    ret = "Pass"
                else:
                    ret = "Unknown"
                mode = runs[i][1]
                compiler = runs[i][2]
                bench = runs[i][4]
                results[ (mode, compiler, bench) ] = ret
                print mode, compiler, bench, ret
                del runs[i]
                return
        # All tests are running
        # Wait for some time and re-check
        time.sleep(30)

def PrintResultsTable(mode):
    print mode 
    line = "| SPEC "
    for compiler, config, _ in config_list:
        line += " | {0}".format(compiler)
    line += " | "
    print line
    line = "| --- | "
    for compiler, config, _ in config_list:
        line += " --- | "
    print line

    for test in tests_list:
        line = "| {0}".format(test)
        for compiler, config, _ in config_list:
            line += " | {0}".format(results[(mode, compiler, test)])
        line += " |"
        print line
        
        
CleanOldDirs()
configs = GenerateTestConfigurations()

results = {}
runs = []
for mode, compiler, cfg_prefix, bench, module in configs:
    runs.append( Run(mode, compiler, cfg_prefix, bench, module) )
    if len(runs) == PARALLEL:
        WaitForFinish(results, runs)

while len(runs) > 0:
    WaitForFinish(results, runs)

#PrintResultsTable("light")
PrintResultsTable("full")


