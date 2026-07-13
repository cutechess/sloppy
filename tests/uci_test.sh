#!/bin/bash
# Protocol regression tests for Sloppy.
#
# Covers the UCI protocol (handshake, position setup, search limits,
# options, error handling) and guards the existing Xboard protocol and
# console mode against regressions.
#
# Usage: tests/uci_test.sh [path-to-engine]

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ENGINE=${1:-$SCRIPT_DIR/../src/sloppy}
TESTTMP=$(mktemp -d)
cleanup() {
	# Engine left running if the script is interrupted mid-test.
	if [ -n "${ENGINE_PID:-}" ]; then
		kill "$ENGINE_PID" 2>/dev/null
	fi
	rm -f "$TESTTMP/out" "$TESTTMP/in" "$TESTTMP/gamelog.txt" \
	      "$TESTTMP/games.pgn"
	rmdir "$TESTTMP/config" "$TESTTMP/data" "$TESTTMP" 2>/dev/null
}
trap cleanup EXIT
# Route fatal signals through the EXIT trap so cleanup always runs.
trap 'exit 130' INT
trap 'exit 143' TERM

# Keep the engine away from the user's real config, book and bitbases.
export XDG_CONFIG_HOME="$TESTTMP/config"
export XDG_DATA_HOME="$TESTTMP/data"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"

if [ ! -x "$ENGINE" ]; then
	echo "Engine not found or not executable: $ENGINE"
	exit 2
fi

nfailed=0
npassed=0

# Feed $2 to the engine and store its output in $TESTTMP/out.
# Kill the engine if it doesn't finish within $1 seconds.
run_engine() {
	local timeout_secs="$1"
	local input="$2"
	local pid
	local ticks=0

	printf '%b' "$input" | (cd "$TESTTMP" && exec "$ENGINE") \
		> "$TESTTMP/out" 2>/dev/null &
	pid=$!
	while kill -0 "$pid" 2>/dev/null; do
		ticks=$((ticks + 1))
		if [ "$ticks" -gt $((timeout_secs * 10)) ]; then
			kill "$pid" 2>/dev/null
			echo "ENGINE-TIMEOUT" >> "$TESTTMP/out"
			break
		fi
		sleep 0.1
	done
	wait "$pid" 2>/dev/null
}

# Assert that the last engine output matches the extended regex $2.
check() {
	if grep -qE "$2" "$TESTTMP/out"; then
		echo "PASS: $1"
		npassed=$((npassed + 1))
	else
		echo "FAIL: $1 (expected pattern: $2)"
		nfailed=$((nfailed + 1))
	fi
}

# Assert that the last engine output does NOT match the extended regex $2.
check_absent() {
	if grep -qE "$2" "$TESTTMP/out"; then
		echo "FAIL: $1 (forbidden pattern found: $2)"
		nfailed=$((nfailed + 1))
	else
		echo "PASS: $1"
		npassed=$((npassed + 1))
	fi
}

# Milliseconds since the epoch. Prefers date's nanosecond format
# (GNU coreutils and newer BSDs); falls back to perl where date
# passes the format character through unexpanded.
now_ms() {
	local ns
	ns=$(date +%s%N 2>/dev/null)
	case $ns in
	''|*[!0-9]*)
		perl -MTime::HiRes=time -e 'printf "%d", time * 1000'
		;;
	*)
		printf '%s' $((ns / 1000000))
		;;
	esac
}

BESTMOVE_RE='^bestmove [a-h][1-8][a-h][1-8][qrbn]?$'

echo "=== UCI handshake ==="
run_engine 10 'uci\nisready\nquit\n'
check "id name" '^id name Sloppy'
check "id author" '^id author'
check "uciok" '^uciok$'
check "Hash option" '^option name Hash type spin default [0-9]+ min 8 max 1024$'
check "Clear Hash option" '^option name Clear Hash type button$'
check "OwnBook option" '^option name OwnBook type check default (true|false)$'
check "EgbbPath option" '^option name EgbbPath type string'
check "EgbbLoadType option" '^option name EgbbLoadType type combo'
check "EgbbCache option" '^option name EgbbCache type spin'
check "readyok" '^readyok$'
check_absent "engine exits on quit" 'ENGINE-TIMEOUT'

echo "=== position startpos + go depth ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos moves e2e4 e7e5\ngo depth 8\n'
check "reaches depth 8" '^info depth 8 '
check "info has score and pv" '^info depth [0-9]+ score cp -?[0-9]+ time [0-9]+ nodes [0-9]+.* pv [a-h][1-8][a-h][1-8]'
check "bestmove format" "$BESTMOVE_RE"
check_absent "no premature termination" 'ENGINE-TIMEOUT'

echo "=== position fen ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nposition fen r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3\ngo depth 6\n'
check "bestmove from fen" "$BESTMOVE_RE"
check_absent "fen accepted" 'Invalid FEN'

echo "=== castling and promotion round-trip ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos moves e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 e1g1 a7a6\ngo depth 4\n'
check "bestmove after castling" "$BESTMOVE_RE"
check_absent "castling accepted" 'Illegal move'
run_engine 30 'uci\nsetoption name OwnBook value false\nposition fen 8/P6k/8/8/8/8/7K/8 w - - 0 1 moves a7a8q\ngo depth 4\n'
check "bestmove after promotion" "$BESTMOVE_RE"
check_absent "promotion accepted" 'Illegal move'

echo "=== illegal move rejection ==="
run_engine 10 'uci\nposition startpos moves e2e5\nquit\n'
check "illegal move reported" '^info string Illegal move: e2e5$'

echo "=== mate score ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nposition fen k7/8/KQ6/8/8/8/8/8 w - - 0 1\ngo depth 6\n'
check "mate 1 reported" '^info depth [0-9]+ score mate 1 '
# There are several mates in one; all of them are queen moves from b6.
check "mating move played" '^bestmove b6[a-h][1-8]$'

echo "=== checkmated and stalemated positions ==="
run_engine 10 'uci\nposition fen k7/1Q6/K7/8/8/8/8/8 b - - 0 1\ngo depth 3\nquit\n'
check "nullmove on checkmate" '^bestmove 0000$'
run_engine 10 'uci\nposition fen k7/8/1Q6/8/8/8/8/K7 b - - 0 1\ngo depth 3\nquit\n'
check "nullmove on stalemate" '^bestmove 0000$'

echo "=== go movetime ==="
# The whole run must take at least roughly the movetime: an engine that
# misreads the budget as zero answers nearly instantly. Wall time is
# used because the timing of the last *reported* iteration is not
# deterministic.
movetime_start=$(now_ms)
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos\ngo movetime 800\n'
movetime_elapsed=$(($(now_ms) - movetime_start))
check "bestmove within movetime" "$BESTMOVE_RE"
if [ "$movetime_elapsed" -ge 400 ]; then
	echo "PASS: search used its time budget (${movetime_elapsed} ms)"
	npassed=$((npassed + 1))
else
	echo "FAIL: search returned too fast (${movetime_elapsed} ms for movetime 800)"
	nfailed=$((nfailed + 1))
fi
check_absent "movetime terminates" 'ENGINE-TIMEOUT'

echo "=== go with clocks ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos\ngo wtime 10000 btime 10000 winc 100 binc 100\n'
check "bestmove on clock" "$BESTMOVE_RE"
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos moves e2e4\ngo wtime 8000 btime 9000 winc 0 binc 0 movestogo 30\n'
check "bestmove with movestogo" "$BESTMOVE_RE"

echo "=== go nodes ==="
run_engine 20 'uci\nsetoption name OwnBook value false\nposition startpos\ngo nodes 20000\n'
check "node-limited search terminates" "$BESTMOVE_RE"
check_absent "node limit respected" 'ENGINE-TIMEOUT'

echo "=== go infinite + stop ==="
( printf 'uci\nsetoption name OwnBook value false\nposition startpos\ngo infinite\n'
  sleep 1
  printf 'stop\nquit\n' ) | (cd "$TESTTMP" && exec "$ENGINE") > "$TESTTMP/out" 2>/dev/null &
infinite_pid=$!
ticks=0
while kill -0 "$infinite_pid" 2>/dev/null; do
	ticks=$((ticks + 1))
	if [ "$ticks" -gt 150 ]; then
		kill "$infinite_pid" 2>/dev/null
		echo "ENGINE-TIMEOUT" >> "$TESTTMP/out"
		break
	fi
	sleep 0.1
done
wait "$infinite_pid" 2>/dev/null
check "infinite search produced info" '^info depth '
check "stop produced bestmove" "$BESTMOVE_RE"
nbestmoves=$(grep -cE '^bestmove ' "$TESTTMP/out")
if [ "$nbestmoves" -eq 1 ]; then
	echo "PASS: exactly one bestmove"
	npassed=$((npassed + 1))
else
	echo "FAIL: exactly one bestmove (got $nbestmoves)"
	nfailed=$((nfailed + 1))
fi
check_absent "infinite terminates on stop" 'ENGINE-TIMEOUT'

echo "=== setoption ==="
run_engine 10 'uci\nsetoption name Hash value 64\nisready\nquit\n'
check "hash accepted" '^readyok$'
check_absent "hash size valid" 'info string Hash size'
run_engine 10 'uci\nsetoption name Hash value 4\nquit\n'
check "hash size limit enforced" '^info string Hash size must be between 8 and 1024 MB$'
run_engine 10 'uci\nsetoption name hash value 64\nisready\nquit\n'
check_absent "option names case-insensitive" 'Unknown option'
run_engine 10 'uci\nsetoption name Clear Hash\nisready\nquit\n'
check "clear hash button" '^readyok$'
run_engine 10 'uci\nsetoption name NoSuchOption value 1\nquit\n'
check "unknown option reported" '^info string Unknown option: NoSuchOption$'

echo "=== ucinewgame ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nucinewgame\nposition startpos\ngo depth 4\n'
check "search after ucinewgame" "$BESTMOVE_RE"

echo "=== Xboard protocol regression ==="
run_engine 30 'xboard\nprotover 2\nnew\nst 1\nping 7\ngo\n'
check "feature line" '^feature myname="Sloppy'
check "features done" 'done=1'
check "pong" '^pong 7$'
check "xboard move" '^move [a-h][1-8][a-h][1-8]'
check_absent "no uci output in xboard mode" '^bestmove '

echo "=== console mode regression ==="
run_engine 30 'perft 3\n'
# The console prompt precedes the result on the same line,
# so no start-of-line anchor.
check "perft node count" 'Perft\(3\): 8902 nodes\.$'
run_engine 10 'help\nquit\n'
check "help lists uci" '^uci - switches to UCI mode$'
check "help lists xboard" '^xboard - switches to Xboard/Winboard mode$'

# ---------------------------------------------------------------------
# Staged-input harness: an engine fed through a fifo so that commands can
# be sent while it is running and the output inspected mid-flight.
# ---------------------------------------------------------------------

ENGINE_PID=

start_engine() {
	rm -f "$TESTTMP/in" "$TESTTMP/out"
	mkfifo "$TESTTMP/in"
	(cd "$TESTTMP" && exec "$ENGINE") < "$TESTTMP/in" \
		> "$TESTTMP/out" 2>/dev/null &
	ENGINE_PID=$!
	exec 3> "$TESTTMP/in"
}

send() {
	printf '%b' "$1" >&3
}

# Wait until the output matches the extended regex $1 (timeout: $2 seconds).
# Returns nonzero on timeout.
wait_for_output() {
	local ticks=0
	while ! grep -qE "$1" "$TESTTMP/out"; do
		ticks=$((ticks + 1))
		if [ "$ticks" -gt $(($2 * 10)) ]; then
			return 1
		fi
		sleep 0.1
	done
	return 0
}

stop_engine() {
	local ticks=0
	send 'quit\n'
	exec 3>&-
	while kill -0 "$ENGINE_PID" 2>/dev/null; do
		ticks=$((ticks + 1))
		if [ "$ticks" -gt 100 ]; then
			kill "$ENGINE_PID" 2>/dev/null
			echo "ENGINE-TIMEOUT" >> "$TESTTMP/out"
			break
		fi
		sleep 0.1
	done
	wait "$ENGINE_PID" 2>/dev/null
	ENGINE_PID=
}

count_bestmoves() {
	grep -cE '^bestmove ' "$TESTTMP/out"
}

# ---------------------------------------------------------------------
# Regression tests. Each of these guards a specific fixed bug or a
# deliberate behavior decision, and failed before its fix was made.
# ---------------------------------------------------------------------

echo "=== regression: long game position line ==="
# GUIs resend the whole move list every move, so the line grows without
# bound; the engine must survive lines much longer than a short buffer.
long_moves=""
i=0
while [ "$i" -lt 15 ]; do
	long_moves="$long_moves b1c3 b8c6 c3b1 c6b8"
	i=$((i + 1))
done
run_engine 30 "uci\nsetoption name OwnBook value false\nposition startpos moves$long_moves\ngo depth 2\n"
check "long position line answered" "$BESTMOVE_RE"
check_absent "long position line not truncated" 'ENGINE-TIMEOUT'

echo "=== regression: setoption during search still yields bestmove ==="
start_engine
send 'uci\nsetoption name OwnBook value false\nposition startpos\ngo movetime 3000\n'
sleep 1
send 'setoption name Hash value 32\n'
wait_for_output '^bestmove ' 15
check "bestmove despite mid-search setoption" "$BESTMOVE_RE"
stop_engine

echo "=== regression: node-limit stop in the first iteration ==="
run_engine 20 'uci\nsetoption name OwnBook value false\nposition fen qqqqqqqq/8/3k4/8/8/4K3/8/QQQQQQQQ w - - 0 1\ngo nodes 1\n'
check "iteration-1 stop yields a legal bestmove" "$BESTMOVE_RE"
check_absent "no crash on iteration-1 stop" 'ENGINE-TIMEOUT'

echo "=== regression: Clear Hash must not leak ==="
# The baseline is taken after the first press, when the table's pages
# are certain to be resident; a leak of one full table per press then
# shows up as unbounded growth over the remaining presses.
start_engine
send 'uci\nsetoption name Hash value 64\nsetoption name Clear Hash\nisready\n'
wait_for_output '^readyok$' 10
sleep 1
rss_before=$(ps -o rss= -p "$ENGINE_PID" | tr -d ' ')
i=0
while [ "$i" -lt 20 ]; do
	send 'setoption name Clear Hash\n'
	i=$((i + 1))
done
send 'isready\n'
sleep 2
rss_after=$(ps -o rss= -p "$ENGINE_PID" | tr -d ' ')
# Allow slack far below the size of a single leaked table.
if [ -n "$rss_before" ] && [ -n "$rss_after" ] \
	&& [ $((rss_after - rss_before)) -lt 32768 ]; then
	echo "PASS: Clear Hash does not leak (rss $rss_before -> $rss_after KB)"
	npassed=$((npassed + 1))
else
	echo "FAIL: Clear Hash leaks (rss $rss_before -> $rss_after KB)"
	nfailed=$((nfailed + 1))
fi
stop_engine

echo "=== regression: zero clocks and tiny movetime move promptly ==="
run_engine 10 'uci\nsetoption name OwnBook value false\nposition startpos\ngo wtime 0 btime 0\n'
check "bestmove on zero clocks" "$BESTMOVE_RE"
check_absent "zero clocks don't search forever" 'ENGINE-TIMEOUT'
run_engine 10 'uci\nsetoption name OwnBook value false\nposition startpos\ngo movetime 1\n'
check "bestmove on movetime 1" "$BESTMOVE_RE"
check_absent "movetime 1 doesn't search forever" 'ENGINE-TIMEOUT'

echo "=== regression: commands after self-finished infinite search ==="
# An infinite search on a mate-in-1 finishes almost instantly and the
# engine parks waiting for the GUI; a new position + go sent without a
# stop must still be honored.
start_engine
send 'uci\nsetoption name OwnBook value false\nposition fen k7/8/KQ6/8/8/8/8/8 w - - 0 1\ngo infinite\n'
sleep 1
send 'position startpos\ngo depth 2\n'
wait_for_output '^bestmove ' 15
check_absent "parked search does not answer with stale position" '^bestmove b6'
check "new go after parked infinite is answered" "$BESTMOVE_RE"
nbm=$(count_bestmoves)
if [ "$nbm" -eq 1 ]; then
	echo "PASS: exactly one bestmove after parked infinite"
	npassed=$((npassed + 1))
else
	echo "FAIL: exactly one bestmove after parked infinite (got $nbm)"
	nfailed=$((nfailed + 1))
fi
stop_engine

echo "=== regression: go infinite on mated position waits for stop ==="
start_engine
send 'uci\nposition fen k7/1Q6/K7/8/8/8/8/8 b - - 0 1\ngo infinite\n'
sleep 1
if grep -qE '^bestmove' "$TESTTMP/out"; then
	echo "FAIL: bestmove sent before stop in infinite mode"
	nfailed=$((nfailed + 1))
else
	echo "PASS: no bestmove before stop in infinite mode"
	npassed=$((npassed + 1))
fi
send 'stop\n'
wait_for_output '^bestmove 0000$' 10
check "stop on mated infinite yields bestmove 0000" '^bestmove 0000$'
stop_engine

echo "=== regression: repeated go without position ==="
# UCI allows 'go' without a new 'position'; the engine must not have
# applied its previous bestmove to its own board.
start_engine
send 'uci\nsetoption name OwnBook value false\nposition startpos\ngo depth 4\n'
wait_for_output '^bestmove ' 15
send 'go depth 4\n'
ticks=0
while [ "$(count_bestmoves)" -lt 2 ] && [ "$ticks" -lt 150 ]; do
	ticks=$((ticks + 1))
	sleep 0.1
done
nbm=$(count_bestmoves)
ndistinct=$(grep -E '^bestmove ' "$TESTTMP/out" | sort -u | wc -l | tr -d ' ')
if [ "$nbm" -eq 2 ] && [ "$ndistinct" -eq 1 ]; then
	echo "PASS: repeated go searches the same position"
	npassed=$((npassed + 1))
else
	echo "FAIL: repeated go searches the same position (got $nbm bestmoves, $ndistinct distinct)"
	nfailed=$((nfailed + 1))
fi
stop_engine

echo "=== regression: go ponder holds bestmove until ponderhit ==="
start_engine
send 'uci\nsetoption name OwnBook value false\nposition startpos\ngo ponder depth 3\n'
sleep 1
if grep -qE '^bestmove' "$TESTTMP/out"; then
	echo "FAIL: bestmove sent during ponder before ponderhit/stop"
	nfailed=$((nfailed + 1))
else
	echo "PASS: no bestmove during ponder before ponderhit/stop"
	npassed=$((npassed + 1))
fi
send 'ponderhit\n'
wait_for_output '^bestmove ' 10
check "ponderhit releases the bestmove" "$BESTMOVE_RE"
check "ponder degradation is announced" '^info string Pondering is not supported'
stop_engine

echo "=== regression: illegal move leaves previous position intact ==="
run_engine 20 'uci\nsetoption name OwnBook value false\nposition fen k7/8/KQ6/8/8/8/8/8 w - - 0 1\nposition startpos moves e2e4 e7e9\ngo depth 2\n'
check "illegal move reported in list" '^info string Illegal move: e7e9$'
check "search still on previous position" '^bestmove b6[a-h][1-8]$'

echo "=== regression: go depth 1 emits an info line ==="
run_engine 20 'uci\nsetoption name OwnBook value false\nposition startpos\ngo depth 1\n'
check "info line for depth 1" '^info depth 1 score '
check "bestmove for depth 1" "$BESTMOVE_RE"

echo "=== regression: node limit is respected closely ==="
run_engine 20 'uci\nsetoption name OwnBook value false\ndebug on\nposition startpos\ngo nodes 100\n'
main_nodes=$(grep -E 'Main nodes searched: [0-9]+' "$TESTTMP/out" | grep -oE '[0-9]+$')
qs_nodes=$(grep -E 'Quiescence nodes searched: [0-9]+' "$TESTTMP/out" | grep -oE '[0-9]+$')
if [ -n "$main_nodes" ] && [ -n "$qs_nodes" ] \
	&& [ $((main_nodes + qs_nodes)) -le 300 ]; then
	echo "PASS: node limit respected ($main_nodes + $qs_nodes nodes for a 100-node budget)"
	npassed=$((npassed + 1))
else
	echo "FAIL: node limit overshoot (${main_nodes:-?} + ${qs_nodes:-?} nodes for a 100-node budget)"
	nfailed=$((nfailed + 1))
fi
check "bestmove within node limit" "$BESTMOVE_RE"

echo "=== regression: invalid go parameter value is safe ==="
run_engine 15 'uci\nsetoption name OwnBook value false\nposition startpos\ngo nodes abc\n'
check "invalid value reported" '^info string Invalid nodes value: abc$'
check "engine still answers promptly" "$BESTMOVE_RE"
check_absent "no unbounded search on garbage" 'ENGINE-TIMEOUT'

echo "=== regression: searchmoves restricts the root moves ==="
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos\ngo searchmoves a2a3 depth 6\n'
check "single searchmove is played" '^bestmove a2a3$'
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos\ngo searchmoves e2e4 d2d4 depth 6\n'
check "bestmove within searchmoves set" '^bestmove (e2e4|d2d4)$'

echo "=== regression: ucinewgame keeps the hash table ==="
# Deliberate decision: like the Xboard "new" command, ucinewgame does
# not clear the hash table, so the engine behaves the same in both
# protocols. A GUI that wants a cold start can press Clear Hash.
# A repeated search of the same position must therefore stay much
# cheaper than the first one even across a ucinewgame.
start_engine
send 'uci\nsetoption name OwnBook value false\nposition startpos\ngo depth 9\n'
wait_for_output '^bestmove ' 30
first_nodes=$(grep -E '^info depth 9 ' "$TESTTMP/out" | sed -n '1p' | grep -oE 'nodes [0-9]+' | grep -oE '[0-9]+')
send 'ucinewgame\nposition startpos\ngo depth 9\n'
ticks=0
while [ "$(count_bestmoves)" -lt 2 ] && [ "$ticks" -lt 300 ]; do
	ticks=$((ticks + 1)); sleep 0.1
done
repeat_nodes=$(grep -E '^info depth 9 ' "$TESTTMP/out" | sed -n '2p' | grep -oE 'nodes [0-9]+' | grep -oE '[0-9]+')
if [ -n "$first_nodes" ] && [ -n "$repeat_nodes" ] \
	&& [ "$repeat_nodes" -lt "$first_nodes" ]; then
	echo "PASS: ucinewgame keeps the hash (cold $first_nodes > repeat $repeat_nodes nodes)"
	npassed=$((npassed + 1))
else
	echo "FAIL: ucinewgame drops the hash (cold ${first_nodes:-?}, repeat ${repeat_nodes:-?} nodes)"
	nfailed=$((nfailed + 1))
fi
stop_engine

echo "=== regression: debug output is protocol-safe in UCI ==="
run_engine 20 'uci\nsetoption name OwnBook value false\ndebug on\nposition startpos\ngo depth 5\n'
check "debug stats as info string" '^info string Main nodes searched: [0-9]+$'
check_absent "no raw debug lines" '^Main nodes searched:'

echo "=== regression: Egbb5Men option ==="
run_engine 10 'uci\nsetoption name Egbb5Men value true\nisready\nquit\n'
check "Egbb5Men advertised" '^option name Egbb5Men type check default (true|false)$'
check_absent "Egbb5Men accepted" 'Unknown option'
check "engine ready after Egbb5Men" '^readyok$'

echo "=== regression: xboard memory command still works ==="
run_engine 30 'xboard\nprotover 2\nnew\nmemory 64\nst 1\ngo\n'
check "xboard move after memory" '^move [a-h][1-8][a-h][1-8]'
run_engine 10 'xboard\nprotover 2\nmemory 4\nquit\n'
check "xboard memory limit message" '^Hash size must be between 8 and 1024 MB\.$'

echo "=== regression: go mate is consumed and validated ==="
run_engine 15 'uci\nsetoption name OwnBook value false\nposition startpos\ngo mate\n'
check "bare mate reports missing value" '^info string The mate parameter needs a value$'
check "bare mate still answers promptly" "$BESTMOVE_RE"
check_absent "bare mate doesn't search forever" 'ENGINE-TIMEOUT'
run_engine 30 'uci\nsetoption name OwnBook value false\nposition startpos\ngo mate 3 depth 4\n'
check "mate N reported unsupported" '^info string The mate parameter is not supported$'
check "depth after mate N is honored" '^info depth 4 '
check "bestmove after mate N" "$BESTMOVE_RE"

echo "=== regression: out-of-range go values are clamped safely ==="
run_engine 10 'uci\nsetoption name OwnBook value false\nposition startpos\ngo wtime -99999999999999 btime -99999999999999\n'
check "bestmove on huge negative clocks" "$BESTMOVE_RE"
check_absent "huge negative clocks don't hang" 'ENGINE-TIMEOUT'
run_engine 15 'uci\nsetoption name OwnBook value false\nposition startpos\ngo wtime 99999999999999 btime 99999999999999 movestogo 1 depth 3\n'
check "bestmove on huge positive clocks" "$BESTMOVE_RE"
check_absent "huge positive clocks don't hang" 'ENGINE-TIMEOUT'

echo "=== regression: debug score agrees with the info lines ==="
# Black to move with a clear material edge: the side-to-move score is
# strongly positive, the white-relative score strongly negative, so a
# sign mix-up cannot hide.
run_engine 20 'uci\nsetoption name OwnBook value false\ndebug on\nposition fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RN2KBNR b KQkq - 0 1\ngo depth 4\n'
cp_score=$(grep -E '^info depth 4 score cp' "$TESTTMP/out" | tail -1 \
	| sed -E 's/.*score cp (-?[0-9]+).*/\1/')
dbg_score=$(sed -n 's/^info string Score: \(-\{0,1\}[0-9]*\)$/\1/p' \
	"$TESTTMP/out" | tail -1)
if [ -n "$cp_score" ] && [ "$cp_score" = "$dbg_score" ]; then
	echo "PASS: debug score matches info score ($cp_score)"
	npassed=$((npassed + 1))
else
	echo "FAIL: debug score mismatch (info cp '$cp_score' vs debug '$dbg_score')"
	nfailed=$((nfailed + 1))
fi

echo "=== regression: repeated uci handshake has no stray blank line ==="
run_engine 10 'uci\nuci\nquit\n'
# The line right after the first "uciok" must be the second handshake's
# "id name" line, not a leftover blank line.
after_uciok=$(sed -n '/^uciok$/{n;p;q;}' "$TESTTMP/out")
if [ "$(grep -c '^uciok$' "$TESTTMP/out")" -eq 2 ] \
	&& printf '%s' "$after_uciok" | grep -q '^id name Sloppy'; then
	echo "PASS: repeated uci repeats the handshake without a blank line"
	npassed=$((npassed + 1))
else
	echo "FAIL: repeated uci handshake (line after uciok: '$after_uciok')"
	nfailed=$((nfailed + 1))
fi

echo "=== regression: ucinewgame leaves gamelog.txt alone ==="
# UCI mode never writes the game log, so it must not delete one either.
# The file is planted after the handshake, because engine startup still
# deletes it once, before any protocol has been chosen.
# The Xboard "new" command keeps its original delete behavior.
start_engine
send 'uci\n'
wait_for_output '^uciok$' 10
printf 'unrelated content\n' > "$TESTTMP/gamelog.txt"
send 'ucinewgame\nisready\n'
wait_for_output '^readyok$' 10
if [ -f "$TESTTMP/gamelog.txt" ]; then
	echo "PASS: uci mode leaves gamelog.txt alone"
	npassed=$((npassed + 1))
else
	echo "FAIL: uci mode deleted gamelog.txt"
	nfailed=$((nfailed + 1))
fi
stop_engine
printf 'unrelated content\n' > "$TESTTMP/gamelog.txt"
run_engine 10 'xboard\nnew\nquit\n'
if [ ! -f "$TESTTMP/gamelog.txt" ]; then
	echo "PASS: xboard new still deletes gamelog.txt"
	npassed=$((npassed + 1))
else
	echo "FAIL: xboard new no longer deletes gamelog.txt"
	nfailed=$((nfailed + 1))
	rm -f "$TESTTMP/gamelog.txt"
fi

echo "=== regression: register is answered ==="
run_engine 10 'uci\nregister later\nisready\nquit\n'
check "registration checking" '^registration checking$'
check "registration ok" '^registration ok$'
check "ready after register" '^readyok$'

echo ""
echo "Passed: $npassed, Failed: $nfailed"
[ "$nfailed" -eq 0 ]
