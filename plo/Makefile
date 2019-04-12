# Vanilla compiler (without any bb patches), can be any modern compilers.
RELEASE_LLVM_BIN=/usr/local/google/home/shenhan/llvm/install-8.0/bin

DDIR         := $(shell readlink -f .)
LLVM_PROJECT := $(shell cd $(DDIR)/.. && pwd)
LLVM_SOURCE  := $(shell find $(LLVM_PROJECT)/llvm $(LLVM_PROJECT)/clang $(LLVM_PROJECT)/lld \
	'(' -ipath "*/.git"      -o      \
	    -ipath "*/test"      -o      \
	    -ipath "*/tests"     -o      \
            -ipath "*/unittests" -o      \
            -ipath "*/gtest"     -o      \
            -ipath "*/googletest" ')' -type d  -prune -o \
	-type f '(' -iname "*.cpp" -o -iname "*.cc" -o -iname "*.c" -o -iname "*.h" ')' -print)

J_NUMBER := $(shell grep -Ee "^core id" /proc/cpuinfo | wc -l)
J_NUMBER := $(shell if [[ "$(J_NUMBER)" -lt "16" ]] ; then \
                        echo $$(($(J_NUMBER) / 8 * 7)) ; \
                    else echo $$(($(J_NUMBER) - 5)); fi)

# $1 is compiler name, "stage1", "instrbb", etc;
# $2 is bin directory that is used to build the compiler, must be absolute path.
# $3 is any other cmake flags (optional)
# $4 is llvm enabled projects
# $5 is target
define build_compiler
	if [[ ! -e "$(1)/build/CMakeCache.txt" ]]; then \
	  mkdir -p $(1)/build ;                         \
	  cd $(1)/build && cmake -G Ninja               \
            -DCMAKE_INSTALL_PREFIX=$(DDIR)/$(1)/install \
	    -DCMAKE_BUILD_TYPE=Release                  \
            -DLLVM_OPTIMIZED_TABLEGEN=On                \
            -DLLVM_TARGETS_TO_BUILD="X86"               \
            -DLLVM_USE_LINKER=lld                       \
	    -DCMAKE_C_COMPILER=$(2)/clang               \
	    -DCMAKE_CXX_COMPILER=$(2)/clang++           \
	    -DCMAKE_ASM_COMPILER=$(2)/clang             \
	    $(3)                                        \
            -DLLVM_ENABLE_PROJECTS=$(4)                 \
            $(LLVM_PROJECT)/llvm;                       \
	fi
	ninja -j$(J_NUMBER) -C $(1)/build $(5)
	touch $@
endef

# $1 is either "labels" or "all".
lld_no_opt := -fuse-ld=lld -Wl,-no-flip-bb-jumps -Wl,-no-optimize-bb-jumps
lld_opt := -fuse-ld=lld -Wl,-flip-bb-jumps -Wl,-optimize-bb-jumps
bbsection_flags = -DCMAKE_C_FLAGS=-fbasicblock-sections=$(1) -DCMAKE_CXX_FLAGS=-fbasicblock-sections=$(1) \
	          -DCMAKE_EXE_LINKER_FLAGS="$(2)" \
		  -DCMAKE_SHARED_LINKER_FLAGS="$(2)" \
	          -DCMAKE_MODULE_LINKER_FLAGS="$(2)"

# Stage1 compiler is the compiler that contains "bb section" and "plo" patches.
stage1/install/bin/clang: $(LLVM_SOURCE)
	$(call build_compiler,stage1,$(RELEASE_LLVM_BIN),,"clang;lld;compiler-rt",install)

bbinstr/install/bin/clang: | stage1-compiler
	$(call build_compiler,bbinstr,$(DDIR)/stage1/install/bin,$(call bbsection_flags,labels,$(lld_no_opt)),"clang;compiler-rt;lld",install)

bbsec/install/bin/clang: | stage1-compiler
	$(call build_compiler,bbsec,$(DDIR)/stage1/install/bin,$(call bbsection_flags,all,$(lld_no_opt)),"clang",install-clang)

stage1-compiler bbinstr-compiler bbsec-compiler: %-compiler: %/install/bin/clang
	@rm -f $@
	ln -sf $< $@
	touch $@

clang.perfdata: run-commands.sh bbinstr-compiler
	perf record -o $@ -e "cycles:u" -j any,u -- ./$< $(DDIR)/bbinstr/install/bin/clang

clang.profile clang.symfile: clang.perfdata
	./plotool.sh $(DDIR)/bbinstr/install/bin/clang $<

run-plo.sh: bbsec-compiler clang.symfile clang.profile
	cd bbsec/build ; ninja -t commands clang | grep -Ee "-o bin/clang-[0-9a-f]+" \
	  | sed -E 's!clang\+\+ !clang++ -Wl,-plo -Wl,-symfile=$(DDIR)/clang.symfile -Wl,-profile=$(DDIR)/clang.profile !' > $(DDIR)/$@
	chmod +x $@

run-plo: run-plo.sh stage1-compiler
	{ cd bbsec/build && $(DDIR)/$< ; } 2>&1 | tee $@

run-commands.sh:
	rsync -a -f "+ */" -f "+ *.inc" -f "+ *.h" -f "+ *.def" -f "- *" \
            stage1/build/ test-build/
	ninja -C stage1/build -t commands clang \
            | grep -E "^$(RELEASE_LLVM_BIN)/clang\+?\+? " \
            | grep -Fe " -c " \
            | sed -Ee 's!^$(RELEASE_LLVM_BIN)/clang\+\+ !$${CCP} -x c++ !' \
                   -e 's!^$(RELEASE_LLVM_BIN)/clang !$${CCP} !' \
                   -e 's!^!cd $(DDIR)/test-build \&\& !' >> commands
	echo "export CCP=\$$1" > $@
	echo "head -n 333 commands | xargs -P50 -L1 -d \"\\n\" bash -x -c" \
                >> $@
	chmod +x $@

sample.o : %.o: %.c
	./stage1-compiler -fbasicblock-sections=all -gline-tables-only -c -O2 $< -o $@

sample.out: sample.c
	./stage1-compiler -fbasicblock-sections=labels -O2 -o $@ $<

sample.perfdata: sample.out
	perf record -o $@ -e "cycles:u" -j any,u -- ./$<

sample.out.symfile sample.out.profile: sample.out sample.perfdata
	./plotool.sh $^

sample.out.cfg: sample.out.symfile sample.out.profile sample.o $(lld)
	stage1/install/bin/ld.lld -plo \
	  -symfile sample.out.symfile \
	  -profile sample.out.profile \
          sample.o 2>&1 | tee $@

clean:
	for F in bbinstr bbsec ; do \
	  rm -fr $${F} ; rm -f $${F}-compiler ; \
	done
	rm -f commands run-commands.sh
	rm -f clang.perfdata* clang.profile clang.symfile
	rm -f sample.o sample.out sample.perfdata sample.out.symfile sample.out.profile sample.out.cfg
	rm -fr test-build
	rm -f *.autolog
	rm -f run-plo.sh run-plo

clean-all: clean
	rm -fr stage1 ; rm -fr stage1-compiler
