import sys
import time

print("Starting input test...")
print("Try typing something and pressing Enter.")
print("Or try pressing Ctrl+C to exit.")

try:
    if sys.version_info.major < 3:
        i = raw_input("Input: ")
    else:
        i = input("Input: ")
    print(f"You entered: {i}")
except KeyboardInterrupt:
    print("\nCaught KeyboardInterrupt (Ctrl+C)!")
except EOFError:
    print("\nCaught EOFError!")
except Exception as e:
    print(f"\nCaught exception: {e}")

print("Exiting.")
