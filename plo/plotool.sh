#!/bin/bash

set -e

# Binary must be built with "-fbasicblock-section-instrument=all".
BINARY="$1"
if [[ -L "$BINARY" ]]; then
    BINARY=$(readlink -f "${BINARY}")
fi
# Perf data must contain lbr information.
PERFDATA="$2"
BASENAME=$(basename $1) # "$(echo ${BINARY} | sed -nEe 's!(.*)\.[^.]+$!\1!p')"
PROFNAME="${BASENAME}.profile"
SYMNAME="${BASENAME}.symfile"

TMPDIR=$(mktemp -d plo.XXXXXX)

echo "Generate ${BASENAME}.symfile in the background ..."
{ nm -S -n ${BINARY} | grep -E " (t|T|w|W) " | \
     awk -e '{if(NF == 4) print $1" "$2" "$3" "$4; if(NF==3) print $1" 0 "$2" "$3}' \
	 > ${SYMNAME}; } &
SYMWID=$!

echo "Processing mmap events (brstack processing in the background) ..." ;
{
    perf script -F pid,brstack -i "$PERFDATA" 1>${TMPDIR}/${BASENAME}.brstack 2>/dev/null ;
    echo "Splitting file ..."
    split -a 2 -n l/40 ${TMPDIR}/${BASENAME}.brstack ${TMPDIR}/${BASENAME}.brstack. ;
} &
WID=$!


echo "Extracing pid ..."
perf script --show-mmap-events -i "$PERFDATA" 2>/dev/null | grep -F PERF_RECORD_MMAP2 > ${TMPDIR}/${BASENAME}.mmap2

if [[ -z `head -n 1 ${TMPDIR}/${BASENAME}.mmap2` ]]; then
    echo "No PERF_RECORD_MMAP2 found in ${PERFDATA}."
    exit 1
fi

# \1 is pid, \2 is address, \3 is size
sed -nEe 's!^.*PERF_RECORD_MMAP2 ([0-9]+)/[0-9]+: \[(0x[0-9a-f]+)\((0x[0-9a-f]+)\).*'"${BINARY}"'$!\1 \2 \3!p' ${TMPDIR}/${BASENAME}.mmap2 > ${TMPDIR}/${BASENAME}.mmap2.1

echo "Sorting pid ..."
cat ${TMPDIR}/${BASENAME}.mmap2.1 | cut -d" " -f 1 | sort -u > ${TMPDIR}/${BASENAME}.pid

echo "Waiting for brstack to finish ..."
wait $WID  # Wait till "pid, brstack finish".

PIDS=`awk -e 'BEGIN { S="" } {S = S "|" $1} END {print "("S")"}' ${TMPDIR}/${BASENAME}.pid`

echo "Filtering brstack files ..."
ls ${TMPDIR}/${BASENAME}.brstack.?? | xargs -L 1 -P 40 sed -i -nEe 's/^'"${PIDS}"'  (0x.*)$/\2/p'
echo "Combing everything ..."
rm -f ${PROFNAME}
{
    for F in ${TMPDIR}/${BASENAME}.brstack.?? ; do
	cat ${F};
    done;
} > ${PROFNAME}
echo "Done writing $PROFNAME".

wait $SYMWID
echo "Done generating ${SYMNAME}."

# rm -f ${BASENAME}.brstack.?? ${BASENAME}{.brstack,.mmap2,.mmap2.1,.pid}
echo rm -fr ${TMPDIR}
rm -fr ${TMPDIR}
