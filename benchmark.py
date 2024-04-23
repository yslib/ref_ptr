
import matplotlib.pyplot as plt

import json
# Open the JSON file
with open('./build/res.json') as f:
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