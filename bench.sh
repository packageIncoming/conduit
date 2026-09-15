#!/usr/bin/env bash
# bench.sh - sweep wrk and conduit configurations, report a perftest-style table.
set -u
set -o pipefail

DURATION=10; RUNS=3; WARMUP=3
CONN_LIST="1 2 5 10 25 50 100 200"
WRK_T_LIST="auto"
SRV_T_LIST=""; SRV_C_LIST=""
CONDUIT_BIN=""; DOCROOT=""; CSV_OUT=""
NETNS=0
NS_NAME="conduitbench"
SRV_IP="10.201.0.2"; CLI_IP="10.201.0.1"; PREFIX=24
URL=""

usage() {
    local out=/dev/stdout; [ "${1:-0}" = "1" ] && out=/dev/stderr
    cat >"$out" <<'EOF'
Usage: bench.sh [options] <url>

Required:
  <url>                  e.g. http://localhost:8080/index.html
                         With -N the host:port is rewritten to the namespace IP.

Load:
  -C "<list>"            wrk connection counts (default: "1 2 5 10 25 50 100 200")
  -T "<list>"            wrk thread counts     (default: auto = min(conns, nproc))
  -d <sec>               duration per run      (default: 10)
  -n <runs>              repetitions per config (default: 3)
  -w <sec>               warmup per config, discarded (default: 3)

Server sweep (all four together):
  -b <path>              conduit binary
  -r <docroot>           docroot
  -S "<list>"            conduit -t values
  -K "<list>"            conduit -c values

Network:
  -N                     run the server in a network namespace behind a veth
                         pair instead of loopback. Requires root and iproute2.
                         Client stays in the root namespace.

Output:
  -o <file.csv>          raw per-run rows
  -h                     help
EOF
}
die() { printf 'bench.sh: %s\n' "$1" >&2; exit 2; }

while getopts "C:T:d:n:w:b:r:S:K:o:Nh" opt; do
    case "$opt" in
        C) CONN_LIST="$OPTARG" ;;  T) WRK_T_LIST="$OPTARG" ;;
        d) DURATION="$OPTARG" ;;   n) RUNS="$OPTARG" ;;
        w) WARMUP="$OPTARG" ;;     b) CONDUIT_BIN="$OPTARG" ;;
        r) DOCROOT="$OPTARG" ;;    S) SRV_T_LIST="$OPTARG" ;;
        K) SRV_C_LIST="$OPTARG" ;; o) CSV_OUT="$OPTARG" ;;
        N) NETNS=1 ;;              h) usage 0; exit 0 ;;
        *) usage 1; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { printf 'bench.sh: exactly one <url> required\n\n' >&2; usage 1; exit 2; }
URL="$1"

command -v wrk >/dev/null 2>&1 || die "wrk not found in PATH"
case "$DURATION$RUNS$WARMUP" in *[!0-9]*) die "-d, -n, -w must be integers";; esac
[ "$RUNS" -ge 1 ] || die "-n must be >= 1"

SERVER_SWEEP=0
if [ -n "$CONDUIT_BIN$SRV_T_LIST$SRV_C_LIST" ]; then
    { [ -n "$CONDUIT_BIN" ] && [ -n "$DOCROOT" ] && [ -n "$SRV_T_LIST" ] && [ -n "$SRV_C_LIST" ]; } \
        || die "-b, -r, -S and -K must be given together"
    [ -x "$CONDUIT_BIN" ] || die "not executable: $CONDUIT_BIN"
    [ -d "$DOCROOT" ]     || die "not a directory: $DOCROOT"
    SERVER_SWEEP=1
fi
[ "$NETNS" = "1" ] && [ "$SERVER_SWEEP" = "0" ] && die "-N requires the server sweep options (-b -r -S -K)"

PORT=$(printf '%s' "$URL" | sed -n 's#^[a-zA-Z]*://[^:/]*:\([0-9]\{1,\}\).*#\1#p')
[ -z "$PORT" ] && PORT=80
PATH_PART=$(printf '%s' "$URL" | sed -n 's#^[a-zA-Z]*://[^/]*\(/.*\)$#\1#p')
[ -z "$PATH_PART" ] && PATH_PART="/"

NPROC=$(nproc 2>/dev/null || echo 1)
SERVER_PID=""; NS_UP=0

# ---------------------------------------------------------------- namespace --
ns_setup() {
    [ "$(id -u)" = "0" ] || die "-N requires root"
    command -v ip >/dev/null 2>&1 || die "-N requires iproute2 (ip)"
    ip netns del "$NS_NAME" >/dev/null 2>&1
    ip link del "vb-cli"    >/dev/null 2>&1
    ip netns add "$NS_NAME"                                   || die "ip netns add failed"
    ip link add vb-cli type veth peer name vb-srv             || die "veth create failed"
    ip link set vb-srv netns "$NS_NAME"                       || die "veth move failed"
    ip addr add "$CLI_IP/$PREFIX" dev vb-cli
    ip link set vb-cli up
    ip netns exec "$NS_NAME" ip addr add "$SRV_IP/$PREFIX" dev vb-srv
    ip netns exec "$NS_NAME" ip link set vb-srv up
    ip netns exec "$NS_NAME" ip link set lo up
    # veth defaults to large offloads, which makes small-message results
    # unrealistic. Turn them off so the path behaves more like a wire.
    for f in tso gso gro tx rx; do
        ethtool -K vb-cli "$f" off >/dev/null 2>&1
        ip netns exec "$NS_NAME" ethtool -K vb-srv "$f" off >/dev/null 2>&1
    done
    NS_UP=1
    URL="http://$SRV_IP:$PORT$PATH_PART"
}
ns_teardown() {
    [ "$NS_UP" = "1" ] || return 0
    ip netns del "$NS_NAME" >/dev/null 2>&1
    ip link del vb-cli      >/dev/null 2>&1
    NS_UP=0
}

port_free() {   # 0 = free
    if [ "$NETNS" = "1" ]; then
        (exec 3<>/dev/tcp/"$SRV_IP"/"$PORT") 2>/dev/null && return 1 || return 0
    fi
    (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null && return 1 || return 0
}

start_server() {    # $1 = -t, $2 = -c
    # Refuse to start if anything already owns the port. Without this a stale
    # server silently answers the entire sweep and every row is fiction.
    if ! port_free; then
        printf 'bench.sh: port %s is already in use; refusing to run\n' "$PORT" >&2
        return 1
    fi
    if [ "$NETNS" = "1" ]; then
        ip netns exec "$NS_NAME" "$CONDUIT_BIN" -t "$1" -c "$2" "$PORT" "$DOCROOT" >/dev/null 2>&1 &
    else
        "$CONDUIT_BIN" -t "$1" -c "$2" "$PORT" "$DOCROOT" >>/tmp/conduit-srv.log 2>&1 &
    fi
    SERVER_PID=$!
    local i
    for i in $(seq 1 50); do
        kill -0 "$SERVER_PID" 2>/dev/null || { SERVER_PID=""; return 1; }
        port_free || return 0        # port now answers, and it is ours
        sleep 0.1
    done
    return 1
}

stop_server() {
    [ -z "$SERVER_PID" ] && return 0
    kill -TERM "$SERVER_PID" 2>/dev/null
    local i; for i in $(seq 1 30); do kill -0 "$SERVER_PID" 2>/dev/null || break; sleep 0.1; done
    kill -KILL "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    SERVER_PID=""; sleep 0.3
}

cleanup() { stop_server; ns_teardown; }
trap cleanup EXIT INT TERM

[ "$NETNS" = "1" ] && ns_setup

# ------------------------------------------------------------------ parsing --
# wrk suffixes latencies with us/ms/s/m/h. Normalise to us.
# "Latency Distribution" must not match the stats line, hence the [0-9] guard.
# Socket errors and Non-2xx lines are absent when zero.
parse_wrk() {
    awk '
    function to_us(tok,   n, u) {
        n = tok; u = tok
        sub(/[a-zA-Z]+$/, "", n); sub(/^[0-9.]+/, "", u)
        if (u == "us") return n + 0
        if (u == "ms") return n * 1000
        if (u == "s")  return n * 1000000
        if (u == "m")  return n * 60000000
        if (u == "h")  return n * 3600000000
        return n + 0
    }
    /^[ \t]*Latency[ \t]+[0-9]/ { lmax = to_us($4) }
    /^[ \t]*50%/                { p50  = to_us($2) }
    /^[ \t]*90%/                { p90  = to_us($2) }
    /^[ \t]*99%/                { p99  = to_us($2) }
    /^Requests\/sec:/           { rps  = $2 + 0 }
    /Socket errors:/            { s=$0; gsub(/[^0-9]/," ",s); n=split(s,a," ");
                                  err=0; for(i=1;i<=n;i++) err += a[i]+0 }
    /Non-2xx or 3xx responses:/ { non2xx = $NF + 0 }
    END { printf "%.2f %.2f %.2f %.2f %.2f %d %d\n",
                 rps+0,p50+0,p90+0,p99+0,lmax+0,err+0,non2xx+0 }
    '
}
median() { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1}
    END{ if(NR==0)print"0"; else if(NR%2)printf"%.2f\n",v[(NR+1)/2];
         else printf"%.2f\n",(v[NR/2]+v[NR/2+1])/2 }'; }
spread() { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1}
    END{ m=v[int((NR+1)/2)]; if(NR<2||m==0)print"0.0";
         else printf"%.1f",(v[NR]-v[1])/m*100 }'; }

# --------------------------------------------------------------- conditions --
printf '=== Conditions ===\n'
printf '  date          : %s\n' "$(date -Is)"
printf '  host          : %s\n' "$(uname -srm)"
printf '  cpu           : %s\n' "$(awk -F: '/model name/{print $2;exit}' /proc/cpuinfo 2>/dev/null | sed 's/^ *//')"
printf '  nproc         : %s\n' "$NPROC"
printf '  governor      : %s\n' "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
printf '  wrk           : %s\n' "$(wrk --version 2>&1 | head -1)"
printf '  url           : %s\n' "$URL"
printf '  duration/run  : %ss   warmup: %ss   runs/config: %s\n' "$DURATION" "$WARMUP" "$RUNS"
if [ "$NETNS" = "1" ]; then
    printf '  path          : veth pair, server in netns %s (%s), client in root ns (%s)\n' \
           "$NS_NAME" "$SRV_IP" "$CLI_IP"
    printf '  offloads      : tso/gso/gro/tx/rx disabled on both veth ends\n'
else
    case "$URL" in *localhost*|*127.0.0.1*)
        printf '  path          : LOOPBACK. Never reaches a NIC. Use -N for a veth path.\n' ;;
    esac
fi
printf '  CONFOUND      : client and server share this machine and its %s cores\n' "$NPROC"
[ "$SERVER_SWEEP" = "1" ] && printf '  server sweep  : -t {%s}  -c {%s}\n' "$SRV_T_LIST" "$SRV_C_LIST"
printf '\n'

[ -n "$CSV_OUT" ] && echo "srv_t,srv_c,conns,wrk_t,run,rps,p50_us,p90_us,p99_us,max_us,errors,non2xx" > "$CSV_OUT"

HDR=' srv_t  srv_c  conns  wrk_t       req/s    p50[us]    p90[us]    p99[us]    max[us]   err  non2xx  spread%'
RULE=$(printf '%.0s-' $(seq 1 ${#HDR}))
printf '%s\n%s\n%s\n' "$RULE" "$HDR" "$RULE"

for st in ${SRV_T_LIST:-na}; do
for sk in ${SRV_C_LIST:-na}; do

    if [ "$SERVER_SWEEP" = "1" ]; then
        start_server "$st" "$sk" || { printf ' %-5s  %-5s  server failed to start\n' "$st" "$sk"; continue; }
    fi

    for conns in $CONN_LIST; do
    for wt_spec in $WRK_T_LIST; do
        if [ "$wt_spec" = "auto" ]; then wt=$(( conns < NPROC ? conns : NPROC )); else wt="$wt_spec"; fi
        [ "$wt" -gt "$conns" ] && wt="$conns"
        [ "$wt" -lt 1 ] && wt=1

        [ "$WARMUP" -gt 0 ] && wrk -t"$wt" -c"$conns" -d"${WARMUP}s" --latency "$URL" >/dev/null 2>&1

        rps_s=(); p50_s=(); p90_s=(); p99_s=(); max_s=(); err_t=0; non_t=0; failed=0
        for r in $(seq 1 "$RUNS"); do
            out=$(wrk -t"$wt" -c"$conns" -d"${DURATION}s" --latency "$URL" 2>&1)
            printf '%s' "$out" | grep -q '^Requests/sec:' || { failed=1; break; }
            read -r rps p50 p90 p99 lmax err non2 <<<"$(printf '%s' "$out" | parse_wrk)"
            rps_s+=("$rps"); p50_s+=("$p50"); p90_s+=("$p90"); p99_s+=("$p99"); max_s+=("$lmax")
            err_t=$((err_t+err)); non_t=$((non_t+non2))
            [ -n "$CSV_OUT" ] && printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                "$st" "$sk" "$conns" "$wt" "$r" "$rps" "$p50" "$p90" "$p99" "$lmax" "$err" "$non2" >> "$CSV_OUT"
        done

        if [ "$failed" = "1" ]; then
            printf ' %-5s  %-5s  %-5s  %-5s   wrk failed (server down or unreachable)\n' "$st" "$sk" "$conns" "$wt"
            continue
        fi

        printf ' %-5s  %-5s  %-5s  %-5s  %10s %10s %10s %10s %10s  %4d  %6d  %6s\n' \
            "$st" "$sk" "$conns" "$wt" \
            "$(median "${rps_s[@]}")" "$(median "${p50_s[@]}")" "$(median "${p90_s[@]}")" \
            "$(median "${p99_s[@]}")" "$(median "${max_s[@]}")" "$err_t" "$non_t" "$(spread "${rps_s[@]}")"
    done
    done

    [ "$SERVER_SWEEP" = "1" ] && stop_server
done
done

printf '%s\n' "$RULE"
printf 'Medians of %s runs. spread%% = (max-min)/median of req/s; >2%% is noise-dominated.\n' "$RUNS"
printf 'err sums wrk socket errors (connect+read+write+timeout). non2xx counts 503s\n'
printf 'from the capacity cap -- a fast row with a large non2xx is serving errors, not files.\n'
[ -n "$CSV_OUT" ] && printf 'Raw rows: %s\n' "$CSV_OUT"