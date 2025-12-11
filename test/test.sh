#!C:\Users\patri\OneDrive\Documents\Projects\Linuxify\cmds\lish.exe
# Test script for Linuxify Shell (lish) Interpreter

# Test 1: Variables
echo "=== Test 1: Variables ==="
NAME="World"
echo "Hello, $NAME!"

# Test 2: Comments
# This is a comment and should be ignored
echo "=== Test 2: Comments work ==="

# Test 3: If/Else
echo "=== Test 3: If/Else ==="
if [ -f "test.sh" ]; then
    echo "test.sh exists!"
else
    echo "test.sh not found"
fi

# Test 4: For Loop
echo "=== Test 4: For Loop ==="
for i in 1 2 3; do
    echo "Number: $i"
done

# Test 5: Numeric comparison
echo "=== Test 5: Numeric Test ==="
X=5
if [ $X -gt 3 ]; then
    echo "$X is greater than 3"
fi

# Test 6: String test
echo "=== Test 6: String Test ==="
GREETING="hello"
if [ $GREETING = "hello" ]; then
    echo "Greeting matches!"
fi

echo ""
echo "All tests completed!"
