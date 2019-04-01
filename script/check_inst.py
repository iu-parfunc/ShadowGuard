import sys
import argparse
from subprocess import *

def getParameters():
    parser = argparse.ArgumentParser(description='Run objdump to check instrumentation')
    parser.add_argument("--binpath", help ="The path to the rewritten binary", required=True)
    parser.add_argument("--stack_trace", help="The path to a file containing a chain of return addresses.\n"
            "Each line of this file should be a return address")
    args = parser.parse_args()
    return args


def RunObjdump(path):
    cmd = "objdump -d {0}".format(path)
    p = Popen(cmd, shell=True, stdout=PIPE, stderr=PIPE)
    msg, err = p.communicate()
    if err.find("error") > 0:
        print "Error message in running", cmd,":"
	print err
        return False, None
    return True, msg

def ParseObjdumpOutput(o):
    relocation = {}
    prev_line = {}
    dyn_start = o.find(".dyninstInst")
    lines = o[dyn_start:].split("\n")
    last = ""
    for line in lines:
        if line.find(":") == -1: continue
        parts = line.split(":")
        if parts[0][0] != " ": continue
        subparts = parts[0].split()
        if len(subparts) != 1: continue
        addr = subparts[0]
        parts = line.split("\t")
        insn = "\t".join(parts[2:])
        relocation[addr] = insn
        prev_line[addr] = last
        last = insn
    return relocation, prev_line

def CheckSpringBoard(o):
    print "Checking spring boards from original code to relocated code"
    text_start = o.find(".text")
    text_end = o.find(".fini")
    lines = o[text_start: text_end].split("\n")
    for i in range(len(lines)):
        line = lines[i]
        if len(line) == 0: continue
        if line[-1] != ":": continue
        i += 1
        first_insn = lines[i]
        parts = first_insn.split("\t")
        if len(parts) < 3: continue
        subparts = parts[2].split()
        opcode = subparts[0]
        if opcode != "jmpq": 
            print "Not jmpq"
            print line
            print first_insn
            continue
        target = subparts[1]
        if target not in relocation:
            print "Do not find target location"
            print line
            print first_insn
            continue
        relocated = relocation[target]
        if relocated.find("push") == -1:
            print "Target is not push"
            print line
            print first_insn
            print relocated
    return prev_line

def GetReturnAddresses(filename):
    stack = []
    with open(filename, "r") as f:
        for line in f:
            ra = line[:-1].strip()
            if ra.startswith("0x"):
                ra = ra[2:]
            stack.append(ra)
    return stack

def PrintCallStack(ra_list, prev_line):
    print "Printing stack trace"
    for ra in ra_list:
        print ra,
        if ra in prev_line:
            print "\t", prev_line[ra]
        else:
            print "\tCannot find"

args = getParameters()
success, objdumpOutput = RunObjdump(args.binpath)
if success:
    relocation, prev_line = ParseObjdumpOutput(objdumpOutput)
    CheckSpringBoard(objdumpOutput)
    if args.stack_trace is not None:
        ra = GetReturnAddresses(args.stack_trace)
        PrintCallStack(ra, prev_line)

