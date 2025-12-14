# Cron test script - appends to a log file
import datetime

now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
with open("cron_output.log", "a") as f:
    f.write(f"[{now}] Hello World from Linuxify Cron!\n")
    print(f"[{now}] Wrote to cron_output.log")  # This won't be visible, but file will be
