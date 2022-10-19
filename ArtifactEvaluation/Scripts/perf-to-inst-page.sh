# input :  perf.data file
# name  : a string to filter from perf report (usually process name)
# How to run: sh perf-to-inst-page.sh clang propeller
# Look for clang-propeller-*.png

input=$1
name=$2
suffix=$3

HEAT_PNG="$2-$3-histo.png"
TIMELINE_PNG="$2-$3-heatmap.png"
TMP_FILE=$(mktemp)
perf report -D -i $input >& ${TMP_FILE}

# Look for mmap of binary. E.g.:
# PERF_RECORD_MMAP2 396635/396635: [0x5567604d6000(0x20a0e000) @ 0 f8:0f 11 0]: r-xp /usr/local/google/home/tejohnson/extra/adgroup_server/Experiment/citc_cron_adgroup2_20190326-0000/adgroup-server.llvm_thinlto_stable
# Not sure if correct to always use first number...
offset=$(grep PERF_RECORD_MMAP2 ${TMP_FILE} | grep $name | head -1 | sed "s,.*\[\(0x55[0-9a-f]*\)(.*,\1,")
#offset=0
#offset=0x555557223000
#offset=0x5567604d6000
#offset=0x558b23a10000
#offset=0x55c6710ca000
#offset=0x55f647b11000
#offset=0x5601f1321000
#offset=0x5596878f5000
#offset=0x55e9d85cf000
#offset=0x55fe358bf000
for i in 1 4096 2097152; do
  grep -A 6 PERF_RECORD_SAMPLE ${TMP_FILE} | grep -A 1 -B 5 "thread: $name" | \
awk "BEGIN { count=0; } /PERF_RECORD_SAMPLE/ {addr = strtonum(\$7)-$offset; \
     if (addr < 1000000000) count++; \
     if (addr < 1000000000) print \$7,count,int((addr-88080384)/$i)*$i}" >  out-$i.txt
done

echo number of samples $(wc -l out-4096.txt)
echo number of uniq 4KB pages $(awk '{print $3}' out-4096.txt | sort -n | uniq | wc -l)
echo number of uniq 2MB pages $(awk '{print $3}' out-2097152.txt | sort -n | uniq | wc -l)

# generate instruction page histogram
awk '{print $3}' out-1.txt | sort -n | uniq -c > inst-1-histo.txt
awk '{print $3}' out-4096.txt | sort -n | uniq -c > inst-4kb-histo.txt
awk '{print $3}' out-2097152.txt | sort -n | uniq -c > inst-2Mb-histo.txt

# generate inst heat map
echo "
set terminal png size 600,450
set xlabel \"Instruction Virtual Address (MB)\"
set ylabel \"Sample Occurance\"
set grid

set output \"${HEAT_PNG}\"
set title \"Instruction Heat Map\"

plot 'inst-4kb-histo.txt' using (\$2/1024/1024):1 with impulses notitle
" | gnuplot

# generate instruction page access timeline
num=$(awk 'END {print NR+1}' out-4096.txt)

# set title \"instruction page accessd timeline\"
echo "
set terminal png size 600,450 giant font "Helvetica" 18
set rmargin 0.7
set xlabel \"time (sec)\"
set ylabel \"Instruction Virtual Address (MB)\"

set output \"${TIMELINE_PNG}\"

plot 'out-4096.txt' using (\$0/$num*10):(\$3/1024/1024) with dots notitle
" | gnuplot
