# Cron test script - appends to a log file with absolute path
import datetime
import os

# Use absolute path (same directory as this script)
script_dir = os.path.dirname(os.path.abspath(__file__))
log_path = os.path.join(script_dir, "cron_output.log")

now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
with open(log_path, "a") as f:
    f.write(f"[{now}] Hello World from Linuxify Cron!\n")
