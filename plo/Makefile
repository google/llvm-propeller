CC=/usr/local/google/home/shenhan/llvm/build-release/bin/clang-9
lld=/usr/local/google/home/shenhan/copt/plo2/build-release/bin/ld.lld
lld_source := $(shell sed -nEe 's!^LLVM_EXTERNAL_LLD_SOURCE_DIR:STRING=(.*)$$!\1!p' $(shell dirname $(lld))/../CMakeCache.txt)

$(lld): $(wildcard $(lld_source)/ELF/*.cpp) $(wildcard $(lld_source)/ELF/*.h)
	ninja -C `dirname $(lld)`/..

Check:
	@If [[ ! -e "$(CC)" ]] || [[ ! -e "$(lld)" ]]; then \
	  echo Please change Makefile to setup CC and lld ; \
	  exit 1; \
	fi

sample.o : %.o: %.c
	$(CC) -fbasicblock-sections=all -c -O2 $< -o $@

sample.out: sample.c
	$(CC) -fbasicblock-sections-instrument=all -O2 -o $@ $<

sample.perfdata: sample.out
	perf record -o $@ -e "cycles:u" -j any,u -- ./$<

sample.out.symfile sample.out.profile: sample.out sample.perfdata
	./plotool.sh $^

sample.out.cfg: sample.out.symfile sample.out.profile sample.o $(lld)
	$(lld) -plo \
	       -symfile sample.out.symfile \
	       -profile sample.out.profile \
	       sample.o 2>&1 | tee $@

.phony: clean
clean:
	rm -f sample.o sample.out sample.perfdata sample.out.symfile sample.out.profile sample.out.cfg
