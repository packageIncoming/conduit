pkill -9 conduit memcheck valgrind 2>/dev/null; sleep 1
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./conduit 4321 docroot/ > /tmp/v.log 2>&1 &
VPID=$!

# Wait for server to actually be listening (up to 15s)
for i in $(seq 1 30); do
  if curl -s --max-time 1 http://127.0.0.1:4321/ > /dev/null 2>&1; then
    echo "server ready after ${i} attempts"
    break
  fi
  sleep 0.5
done

# Real test traffic — print status codes so we know each landed
echo "200 expected: $(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:4321/)"
echo "404 expected: $(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:4321/nonexistent)"
echo "405 expected: $(curl -s -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:4321/)"

# Slowloris
(exec 3<>/dev/tcp/127.0.0.1/4321; sleep 35; exec 3<&-) &
sleep 40

kill -INT $VPID
wait $VPID 2>/dev/null
echo "=== valgrind log ==="
tail -100 /tmp/v.log