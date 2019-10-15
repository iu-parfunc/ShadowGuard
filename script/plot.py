
import csv
import os
import statistics
import matplotlib.pyplot as plt
import numpy as np

from collections import defaultdict
from collections import namedtuple

class BenchRawResult(object):
    def __init__(self, name):
        self.name = name

        self.base = []
        self.peak = []

        self.overhead_min = None
        self.overhead_median = None 
        self.overhead_max = None 

    def __repr__(self):
        desc = ""
        desc += "Benchmark : {}\n".format(self.name)
        desc += "  base (iter1, iter2, iter3) : {0:.3f} {1:.3f} {2:.3f}\n".\
                format(self.base[0], self.base[1], self.base[2])
        desc += "  peak (iter1, iter2, iter3) : {0:.3f} {1:.3f} {2:.3f}\n".\
                format(self.peak[0], self.peak[1], self.peak[2])
        desc += "  overhead (min, mean, max) : {0:.1f} {1:.1f} {2:.1f}\n".\
                format(self.overhead_min, self.overhead_median, self.overhead_max)
        return desc
        
def extract_data_csv(results):
    def _extract_experiment_label(results):
        ''' 
        We extract the label from runcpu command line in the csv file

        e.g: "runcpu command:","runcpu --configfile gcc-test.cfg \ 
                   --label STACK_PROTECTOR --noreportable --nopower \
                   --runmode speed --tune base:peak --size test 625.x264_s"
        '''
        with open(results) as fp:
            for row in fp:
                if "runcpu command:" in row:
                    toks = row.split(' ')
                    for i in range(len(toks)):
                        if toks[i] == '--label':
                            return toks[i + 1]
        return ''
 
    csv_lines = []
    in_data = False
    with open(results) as fp:
        for row in fp:
            if "Full Results Table" in row:
                in_data = True
                continue
            if "Selected Results Table" in row:
                break;
            if in_data and not (row == "" or row == "\n"):
                csv_lines.append(row)

    label = _extract_experiment_label(results)
    data_csv = label + '_results.csv'
    with open(data_csv, 'w') as fp:
        for line in csv_lines:
            fp.write(line)

    return (label, data_csv)

def read_data_csv(data):
    results = defaultdict(lambda: None)
    with open(data) as csvfile:
        reader = csv.DictReader(csvfile, delimiter=',')
        for row in reader:
            if not row['Est. Base Run Time']:
                continue

            bench = row['Benchmark']
            if not results[bench]:
                bench_name = bench.split('.')[1]
                results[bench] = BenchRawResult(bench_name)

            res = results[bench]
            res.base.append(float(row['Est. Base Run Time']))
            res.peak.append(float(row['Est. Peak Run Time']))
    return results

def calculate_experiment_stats(results):
    def _overhead(base, peak):
        overheads = [(x[1] - x[0]) * 100 / x[0] for x in zip(base, peak)]
        return (min(overheads), statistics.median(overheads), max(overheads))

    for bench, res  in results.items():
        res.overhead_min, res.overhead_median, res.overhead_max  = \
                _overhead(res.base, res.peak) 
    return results

def calculate_multi_experiment_stats(results_dir):
    multi_experiment_stats = defaultdict(list)
    for root, dirs, files in os.walk(results_dir):
        for f in files:
            if f.endswith('.csv'):
                label, data_csv = extract_data_csv(os.path.join(root, f))
                stats = calculate_experiment_stats(read_data_csv(data_csv))

                for bench, bench_result in stats.items():
                    multi_experiment_stats[label].append(bench_result)
    return multi_experiment_stats

def sort_benchs_by_order(benchs, order):
    assert len(benchs) == len(order), "Benchmark number mismatch"

    ptr = 0
    sorted_benchs = []
    while ptr < len(benchs):
        for bench in benchs:
            if bench.name == order[ptr]:
                sorted_benchs.append(bench)
                ptr += 1

                if ptr == len(benchs):
                    break
    return sorted_benchs

SeriesData = namedtuple('SeriesData', ['overheads', 'errors'])

benchmarks = ['mcf_s', 'x264_s', 'deepsjeng_s', 'xz_s', 'lbm_s', \
              'perlbench_s', 'gcc_s', 'omnetpp_s', 'xalancbmk_s', \
              'leela_s', 'exchange2_s', 'bwaves_s', 'cactuBSSN_s',\
              'wrf_s', 'pop2_s', 'imagick_s', 'nab_s', 'fotonik3d_s',\
              'roms_s']

def get_bar_chart_data(results_dir):

    multi_experiment_stats = calculate_multi_experiment_stats(results_dir)

    plot_data = defaultdict(lambda: None)
    for label, benchs in multi_experiment_stats.items():
        results = sort_benchs_by_order(benchs, benchmarks)

        overheads = [] 
        errors = [[], []]
        for result in results:
            overheads.append(result.overhead_median)
            errors[0].append(result.overhead_median - result.overhead_min)
            errors[1].append(result.overhead_max - result.overhead_median)
        plot_data[label] = SeriesData(overheads, errors)

    return plot_data

TableData = namedtuple('TableData', ['headers', 'data'])

def get_table_data(plot_data, benchmarks):
    arr = np.full((len(plot_data), len(benchmarks)), np.inf)

    row = 0
    headers = []
    for label, data in plot_data.items():
        for col, overhead in enumerate(data.overheads):
            arr[(row, col)] = round(overhead, 1)
        row += 1
        headers.append(label)

    return TableData(headers, arr.T)

if __name__ == "__main__":
    results_dir = "/home/buddhika/Builds/result"

    # Grok the data.
    bar_chart_data = get_bar_chart_data(results_dir)
    headers, table_data = get_table_data(bar_chart_data, benchmarks)

    # Plot the table.
    plt.table(cellText=table_data, colLabels=headers, rowLabels=benchmarks, loc='center')
    # Removing ticks and spines in the table figure.
    plt.tick_params(axis='x', which='both', bottom=False, top=False, labelbottom=False)
    plt.tick_params(axis='y', which='both', right=False, left=False, labelleft=False)
    for pos in ['right','top','bottom','left']:
        plt.gca().spines[pos].set_visible(False)
    plt.savefig('result_table.pdf', bbox_inches='tight', pad_inches=0.05)

    # Plot the bar chart.
    plt.clf()

    bar_width = 0.25
    pos = np.arange(len(benchmarks))
    for label, data in bar_chart_data.items():
        plt.bar(pos, data.overheads, width=bar_width, edgecolor='white', label=label, yerr=data.errors)
        pos = [x + bar_width for x in pos]
    plt.ylabel("Overhead (%)", fontweight='bold')
    plt.xticks([r + bar_width for r in range(len(benchmarks))], benchmarks, rotation='vertical') 
    # Tweak spacing to prevent clipping of tick-labels
    plt.subplots_adjust(bottom=0.30)
    plt.legend()
    plt.savefig('result_bar_chart.pdf')
