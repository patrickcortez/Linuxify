import sys
import time

print(f"Stdin isatty: {sys.stdin.isatty()}")
print(f"Stdout isatty: {sys.stdout.isatty()}")
sys.stdout.flush()

print("Starting input test...", flush=True)
print("Try typing something and pressing Enter.", flush=True)
print("Or try pressing Ctrl+C to exit.", flush=True)

try:
    i = input("Input: ")
    print(f"You entered: {i}")
except KeyboardInterrupt:
    print("\nCaught KeyboardInterrupt (Ctrl+C)!")
except EOFError:
    print("\nCaught EOFError!")
except Exception as e:
    print(f"\nCaught exception: {e}")

print("Exiting.")
