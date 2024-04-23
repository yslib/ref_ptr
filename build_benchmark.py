def show_plot(filename):
    import matplotlib.pyplot as plt
    import json
    with open(filename) as f:
        data = json.load(f)
        bench = data["benchmarks"]

        x = [i for i in range(1, 21)]   # thread num

        res = {}
        for item in bench:
            family_index = item["name"].split('/')[0]
            if family_index not in res:
                res[family_index] = []
            res[family_index].append(item["real_time"])

        for key, value in res.items():
            plt.plot(x, value, marker='o', linestyle='-')

        plt.xlabel('threads')
        plt.ylabel('Time (lower is better)')
        plt.title('ref_ptr/shared_ptr concurrency CPUs: {}'.format(data["context"]["num_cpus"]))
        # Display the plot
        plt.show()


if __name__ == '__main__':
    # cmake config and build
    import os
    os.system('cmake -S . -B build -DCMAKE_BUILD_TYPE=Release')
    os.system('cmake --build build --config Release')

    param = "--benchmark_out=result.json --benchmark_out_format=json --benchmark_time_unit=s"
    # check if os is windows
    if os.name == 'nt':
        os.system('build\\Release\\concurrency_bench.exe {}'.format(param))
    else:
        os.system('build/concurrency_bench {}'.format(param))

    show_plot('result.json')