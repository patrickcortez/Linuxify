echo "Checking Admin Status..."
if [ "$IS_ADMIN" == "1" ]; then
    echo "Running as Administrator (#)"
else
    echo "Running as User ($)"
fi
echo "IS_ADMIN value: $IS_ADMIN"
