CC=~/llvm/build-release/bin/clang-9
lld=~/copt/plo2/build-release/bin/ld.lld

sample.o : %.o: %.c
	$(CC) -fbasicblock-sections=all -c -O2 $< -o $@

sample.out: sample.c
	$(CC) -fbasicblock-sections-instrument=all -O2 -o $@ $<

sample.perfdata: sample.out
	perf record -o $@ -e "cycles:u" -j any,u -- ./$<

sample.out.symfile sample.out.profile: sample.out sample.perfdata
	./plotool.sh $^

sample.out.cfg: sample.out.symfile sample.out.profile sample.o
	$(lld) -plo \
	       -symfile sample.out.symfile \
	       -profile sample.out.profile \
	       sample.o 2>&1 | tee $@
