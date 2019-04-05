
import argparse
import sys

def get_args():
    parser = argparse.ArgumentParser(description='Compress stack traces')
    parser.add_argument("-v", help ="Print compressed stack traces.")
    parser.add_argument("trace", help="Trace file input")
    args = parser.parse_args()
    return args

if __name__ == "__main__":
    args = get_args()

    trace_file = "trace.txt"
    if args.trace:
        trace_file = args.trace

    depth = 0
    lines = [line.rstrip('\n') for line in open(trace_file)]

    cr1 = []
    cr1_depth = 0
    prev = None
    for line in lines:
        if line != prev:
            cr1.append(line.rstrip('\n'))
            cr1_depth += 1
        prev = line
        depth += 1

    cr2 = []
    aset = []
    cr2_depth = 0
    for line in lines:
        if line not in aset:
            cr2.append(line.rstrip('\n'))
            cr2_depth += 1
        aset.append(line)
        if len(aset) > 2:
            del aset[0]

    print("Trace Compression Statistics:")
    print("  Original stack depth : %d" % depth)
    print("  Compressed stack depth (CR1) : %d" % cr1_depth)
    print("  Compressed stack depth (CR2) : %d" % cr2_depth)
    print("  Compression ratio (CR1) : %.2f" %  (100 - (cr1_depth / depth) * 100))
    print("  Compression ratio (CR2) : %.2f" %  (100 - (cr2_depth / depth) * 100))

    if args.v:
        print("  Compressed stack (CR1): ")
        for entry in cr1:
            print("    %s" % entry)

        print("\n  Compressed stack (CR2): ")
        for entry in cr2:
            print("    %s" % entry)
