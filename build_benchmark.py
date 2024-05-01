def get_cpu_info():
    import platform
    cpu_info = {}
    cpu_info['processor'] = platform.processor()
    cpu_info['architecture'] = platform.architecture()
    cpu_info['machine'] = platform.machine()
    cpu_info['system'] = platform.system()
    info_str = ""
    for key, value in cpu_info.items():
        info_str += "{}: {} | ".format(key, value)
    return info_str

def get_compiler_info():
    cmake_script = """
    cmake_minimum_required(VERSION 3.0)
    project(CompilerInfo)
    message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "C++ compiler id: ${CMAKE_CXX_COMPILER_ID}")
    """
    temp_filename = 'TempCMakeLists.txt'
    with open(temp_filename, 'w') as f:
        f.write(cmake_script)
    import subprocess
    result = subprocess.run(['cmake', '-S', '.', '-B', 'tmp', '-DCMAKE_PROJECT_INCLUDE=' + temp_filename], stdout=subprocess.PIPE)
    import os
    os.remove(temp_filename)
    return result.stdout.decode('utf-8')

benchmark_item = {
    'real_time': {'marker': '+', 'linestyle': ':'}
}


def show_plot(filenames):
    import matplotlib.pyplot as plt
    import json
    for each in filenames:
        label = list(each.keys())[0]
        filename = each[label]
        with open(filename) as f:
            data = json.load(f)
            bench = data["benchmarks"]

            max_x = -1
            res = {}
            for item in bench:
                family_index = item["name"].split('/')[0]
                if family_index not in res:
                    res[family_index] = {key: [] for key, _ in benchmark_item.items()}

                for item_key, _ in benchmark_item.items():
                    res[family_index][item_key].append(item[item_key])

                if item['per_family_instance_index'] > max_x:
                    max_x = item['per_family_instance_index']

            x = [i for i in range(max_x + 1)] # x-axis, threads number

            # plot benchmark items
            for key, value in res.items():
                for item_key, style in benchmark_item.items():
                    plt.plot(x, value[item_key], marker=style['marker'], linestyle=style['linestyle'], label="[{}] {}:{}".format(label, key, item_key))

            # plot cpu_time acceleration between shared_ptr cpu time and ref_ptr cpu time
            shared_ptr_cpu_time = res['shared_ptr']['real_time']
            ref_ptr_cpu_time = res['ref_ptr']['real_time']
            acceleration = [shared_ptr_cpu_time[i] / ref_ptr_cpu_time[i] for i in range(len(x))]
            plt.plot(x, acceleration, marker='o', linestyle='-', label='[{}] acceleration'.format(label))

            # plot y = 1 baseline

    plt.xlabel('threads')
    plt.ylabel('Time (lower is better)')
    plt.axhline(y=1, color='r', linestyle=':', label='acceleration baseline')
    plt.legend()
    plt.show()


def all_result(path):
    import os
    json_files = []
    for root, dirs, files in os.walk(path):
        for file in files:
            if file.endswith('_result.json'):
                label = file.split('_result.json')[0]
                json_files.append({label: os.path.join(root, file)})
    return json_files


if __name__ == '__main__':
    import sys
    import os
    import platform

    if len(sys.argv) == 2:
        if sys.argv[1] == 'summary':
            show_plot(all_result('bench_result'))
            exit(0)
        else:
            print("Invalid argument: summary|show")
            exit(1)

    # cmake config and build
    os.system('cmake -S . -B build -DCMAKE_BUILD_TYPE=Release')
    os.system('cmake --build build --config Release')

    # import cpuinfo # install by pip install py-cpuinfo
    # label = platform.system() + "_" + cpuinfo.get_cpu_info()["brand_raw"].replace(' ', '_')   # compiler info makes more sense
    label = platform.system()
    output_filename = os.path.join('bench_result', '{}_result.json'.format(label))

    param = "--benchmark_out={} --benchmark_out_format=json --benchmark_time_unit=s".format(output_filename)
    # check if os is windows
    if os.name == 'nt':
        os.system('build\\Release\\concurrency_bench.exe {}'.format(param))
    else:
        os.system('build/concurrency_bench {}'.format(param))

    show_plot(all_result('bench_result'))