# Script that tries to prove that the quickjs wrapper does not leak memory.
#
# It finds the leak if a Py_DECREF is commented out in module.c.

import gc
import tracemalloc
import unittest

import quickjs
import test_quickjs

def run():
    loader = unittest.TestLoader()
    suite = loader.discover(".")
    runner = unittest.TextTestRunner()
    runner.run(suite)

def main():
    print("Warming up (to discount regex cache etc.)")
    run()

    tracemalloc.start(25)
    gc.collect()
    snapshot1 = tracemalloc.take_snapshot()
    run()
    gc.collect()
    snapshot2 = tracemalloc.take_snapshot()

    top_stats = snapshot2.compare_to(snapshot1, 'traceback')

    print("Objects not released")
    print("====================")
    for stat in top_stats:
        if "tracemalloc.py" in str(stat) or stat.size_diff == 0:
            continue
        print(stat)
        for line in stat.traceback.format():
            print("    ", line)

    print("\nquickjs should not show up above.")

if __name__ == "__main__":
    main()
