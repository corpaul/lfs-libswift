#!/usr/bin/env python

import sys
import os

def parse_res_usage(orig, parsed=sys.stdout):
    print "Parsing resource usage file"

    try:
        sc_clk_tck = float(os.sysconf(os.sysconf_names['SC_CLK_TCK']))
    except AttributeError:
        sc_clk_tck = 100.0

    for line in orig.readlines():
        parts = line.split(" ")

        timestamp = parts[0]
        utime = parts[15]
        stime = parts[16]

        try:
            time_diff = float(timestamp) - float(prev_timestamp)
            utime_diff = float(utime) - float(prev_utime)
            stime_diff = float(stime) - float(prev_stime)

            pcpu = ((utime_diff + stime_diff) / sc_clk_tck) * (1 / time_diff)
            
            print >>parsed, time_diff, pcpu
        except:
            time_diff = 0.0
            utime_diff = 0.0
            stime_diff = 0.0

        prev_timestamp = timestamp
        prev_utime = utime
        prev_stime = stime

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage:", sys.argv[0], "<logs_dir>"

    logs_dir = sys.argv[1]

    res_usage = os.path.join(logs_dir, "resource_usage.log")
    with open(res_usage, "r") as o, open(res_usage + ".parsed", "w") as p:
        parse_res_usage(o, p)
