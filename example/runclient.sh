N="$1"
echo "Running $N times"
for i in $(seq $N); do 
    echo -n "GET /callback HTTP/1.1 /r/n" | nc localhost 8080 & 
done


