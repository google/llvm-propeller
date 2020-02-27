#!/bin/bash
# usage: ./ab_test.sh compiler1 compiler2 ...

ITERATIONS=5
declare -A times=()
for i in $(eval echo "{1..$(($ITERATIONS+1))}") ; do
  for arg; do
    echo "Running $arg ... iteration $i ..."
    fullpathbinary=`readlink -f $arg`
    TIME=`/usr/bin/time --format "RESULT: USER:%U SYS:%S WALL:%e STATUS:%x" ./run-commands.sh $fullpathbinary 2>&1 | sed -n -Ee 's/^RESULT: USER:(.*) SYS:(.*) WALL:(.*)/(\1+\2)/p' | bc`
    if [ "$i" != "1" ] ; then
      times[$arg]="${times[$arg]}$TIME "
    else
      echo "Throwing away the first iteration result ($TIME)!"
    fi
    echo "${times[$arg]}"
  done
done

for binary in "${!times[@]}"; do
  AVERAGE=$(eval "echo ${times[$binary]} | tr ' ' '\n' | awk '{ total += \$1 } END { print total/$ITERATIONS }'")
  echo "${binary} -> average(${AVERAGE})"
done

