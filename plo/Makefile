# Customizable variables.
# Vanilla clang/clang++ bin path.
RELEASE_LLVM_BIN=/usr/bin
PERF2BOLT=/usr/local/google/home/shenhan/copt/bolt/build-release/bin/perf2bolt
BOLT=/usr/local/google/home/shenhan/copt/bolt/build-release/bin/llvm-bolt

#################################
# Important targets:
#  stage1-compiler: the compiler contains all the patches and used to do benchmark.
#  stage2-compiler: instrumented compiler
#  pgo-compiler: thinlto optimized compiler based on pgo perf data, this is the peak optimized compiler we get.
#  perf.data:    perf collected for pgo-compiler, in order to further optimize pgo-compiler
#  bolt-compiler: bolt on top of pgo-compiler using perf.data
#  pgoplo-compiler: plo on top of pgo-compiler using perf.data
#  cspgoplo-compiler: 
#  benchmark-pgo/benchmark-pgoplo/benchmark-bolt: run benchmarks

DDIR := $(shell pwd)
LLVM_PROJECT := $(shell cd $(DDIR)/.. && pwd)
LLVM_SOURCE := $(shell find $(LLVM_PROJECT)/llvm \
                            $(LLVM_PROJECT)/clang \
                            $(LLVM_PROJECT)/lld \
        '(' -ipath "*/.git"      -o      \
            -ipath "*/test"      -o      \
            -ipath "*/tests"     -o      \
            -ipath "*/unittests" -o      \
            -ipath "*/gtest"     -o      \
            -ipath "*/googletest" ')' -type d  -prune -o \
        -type f '(' -iname "*.cpp" -o -iname "*.cc" -o -iname "*.c" \
                   -o -iname "*.h" ')' -print)

J_NUMBER := $(shell grep -Ee "^core id" /proc/cpuinfo | wc -l)
J_NUMBER := $(shell if [[ "$(J_NUMBER)" -lt "16" ]] ; then \
                        echo $$(($(J_NUMBER) / 8 * 7)) ; \
                    else echo $$(($(J_NUMBER) - 5)); fi)

STAGE1_BIN := $(DDIR)/stage1/install/bin
STAGE2_BIN := $(DDIR)/stage2/install/bin
PROFILES_DIR := $(DDIR)/stage2/build/profiles
CSPROFILES_DIR := $(DDIR)/cspgo/build/csprofiles
LLD_NO_OPT := -fuse-ld=lld -Wl,-no-optimize-bb-jumps
LLD_OPT := -fuse-ld=lld -Wl,-optimize-bb-jumps

COMPILERS := stage1-compiler stage2-compiler pgo-compiler pgoplo-compiler bolt-compiler

# $1 are compiler flags.
# $2 are ld flags.
gen_build_flags = -DCMAKE_C_FLAGS=$(1) -DCMAKE_CXX_FLAGS=$(1) \
                  -DCMAKE_EXE_LINKER_FLAGS=$(2) \
                  -DCMAKE_SHARED_LINKER_FLAGS=$(2) \
                  -DCMAKE_MODULE_LINKER_FLAGS=$(2)

# $1 is either "labels", "all" or "none".
# -fexperimental-new-pass-manager -mllvm -qualify-function-names
bb_section_lto_flags = -Wl,--lto-basicblock-sections=$(1)
lld_relocs              := -Wl,-q
bb_section_none_flags   := $(call gen_build_flags,"-fbasicblock-sections=none","$(LLD_NO_OPT)")
bb_section_labels_flags := $(call gen_build_flags,"-fbasicblock-sections=labels","$(lld_relocs) $(LLD_NO_OPT) $(call bb_section_lto_flags,labels)")
bb_section_all_flags    := $(call gen_build_flags,"-fbasicblock-sections=all","$(lld_relocs) $(LLD_NO_OPT) $(call bb_section_lto_flags,all)")

# $1 is compiler name, "stage1", "instrbb", etc;
# $2 is bin directory that is used to build the compiler, must be absolute path.
# $3 is any other cmake flags (optional)
# $4 is llvm enabled projects
# $5 is target
#	    -DLLVM_PARALLEL_LINK_JOBS=$(J_NUMBER)
define build_compiler
	if [[ ! -e "$(1)/build/CMakeCache.txt" ]]; then \
	  mkdir -p $(1)/build ;                         \
	  cd $(1)/build && cmake -G Ninja               \
            -DCMAKE_INSTALL_PREFIX=$(DDIR)/$(1)/install \
	    -DCMAKE_BUILD_TYPE=Release                  \
            -DLLVM_OPTIMIZED_TABLEGEN=On                \
            -DLLVM_TARGETS_TO_BUILD="X86"               \
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

stage1/install/bin/clang: $(LLVM_SOURCE)
	$(call build_compiler,stage1,$(RELEASE_LLVM_BIN),,"clang;compiler-rt;lld",install)

stage1-compiler: %-compiler: %/install/bin/clang
	ln -sf $< $@
	touch $@

stage-pgo/build/bin/clang-9: | stage1-compiler
	$(call build_compiler,stage-pgo,$(DDIR)/stage1/install/bin,-DLLVM_USE_LINKER=lld -DLLVM_BUILD_INSTRUMENTED=IR,"clang;compiler-rt;lld",all)

stage-pgo-compiler stage-cspgo-compiler pgo-compiler cspgo-compiler: %-compiler: %/build/bin/clang-9
	ln -sf $< $@
	touch $@

pgo-plo-compiler cspgo-plo-compiler: %-compiler: %/build/bin/clang-9-plo
	ln -sf $< $@
	touch $@

stage-cspgo.profdata stage-pgo.profdata: %.profdata: %-compiler run-commands.sh  | stage1-compiler
	./run-commands.sh $(shell readlink -f $<)
	$(STAGE1_BIN)/llvm-profdata merge -output=$@ `find $(dir $(shell readlink -f $<))../ -path "*/csprofiles/*.profraw" -o -path "*/profiles/*.profraw"`

pgo/build/bin/clang-9: stage-pgo.profdata
	$(call build_compiler,pgo,$(DDIR)/stage1/install/bin,    -DLLVM_USE_LINKER=lld -DLLVM_PROFDATA_FILE=$(DDIR)/$< -DLLVM_ENABLE_LTO=Thin $(bb_section_labels_flags),"clang;compiler-rt;lld",all)

pgo-plo/build/bin/clang-9: stage-pgo.profdata
	$(call build_compiler,pgo-plo,$(DDIR)/stage1/install/bin,-DLLVM_USE_LINKER=lld -DLLVM_PROFDATA_FILE=$(DDIR)/$< -DLLVM_ENABLE_LTO=Thin $(bb_section_all_flags),"clang;compiler-rt;lld",clang lld)

pgo.perfdata cspgo.perfdata: %.perfdata: %-compiler run-commands.sh
	perf record -o $@ -e cycles:u -j any,u -- ./run-commands.sh $(shell readlink -f $<)

pgo-compiler.symfile cspgo-compiler.symfile: %-compiler.symfile: %-compiler %.perfdata
	./plotool.sh $< $(word 2,$^)
	[[ -n "`grep -F '.bb.' $@`" ]] || { echo "ERROR: no .bb. in $@" ; exit 1; }

pgo-plo/build/bin/clang-9-plo cspgo-plo/build/bin/clang-9-plo: %-plo/build/bin/clang-9-plo: relink-clang-%-plo.sh
	./$<
	[[ -e "$@" ]] || { echo "Failed to link $@" ; exit 1; }

stage-cspgo/build/bin/clang-9: stage-pgo.profdata | stage1-compiler
	$(call build_compiler,stage-cspgo,$(DDIR)/stage1/install/bin,-DLLVM_USE_LINKER=lld -DLLVM_PROFDATA_FILE=$(DDIR)/$< -DLLVM_BUILD_INSTRUMENTED=CSIR -DLLVM_ENABLE_LTO=Thin,"clang;compiler-rt;lld",all)

cspgo/build/bin/clang-9: stage-cspgo.profdata stage-pgo.profdata | stage1-compiler
	$(STAGE1_BIN)/llvm-profdata merge -output=stage-cspgo2.profdata stage-cspgo.profdata stage-pgo.profdata
	$(call build_compiler,cspgo,$(DDIR)/stage1/install/bin,-DLLVM_USE_LINKER=lld -DLLVM_PROFDATA_FILE=$(DDIR)/stage-cspgo2.profdata -DLLVM_ENABLE_LTO=Thin $(bb_section_labels_flags),"clang;compiler-rt;lld",all)

cspgo-plo/build/bin/clang-9: stage-cspgo.profdata stage-pgo.profdata | stage1-compiler
	$(STAGE1_BIN)/llvm-profdata merge -output=stage-cspgo2.profdata stage-cspgo.profdata stage-pgo.profdata
	$(call build_compiler,cspgo-plo,$(DDIR)/stage1/install/bin,-DLLVM_USE_LINKER=lld -DLLVM_PROFDATA_FILE=$(DDIR)/stage-cspgo2.profdata -DLLVM_ENABLE_LTO=Thin $(bb_section_all_flags),"clang;compiler-rt;lld",all)

run-commands.sh: | stage1-compiler
	rsync -a -f "+ */" -f "+ *.inc" -f "+ *.h" -f "+ *.def" -f "- *" \
            stage1/build/ test-build/
	ninja -C stage1/build -t commands clang \
            | grep -E "^$(RELEASE_LLVM_BIN)/clang\+?\+? " \
            | grep -Fe " -c " \
            | sed -Ee 's!^$(RELEASE_LLVM_BIN)/clang\+\+ !$${CCP} -x c++ !' \
                   -e 's!^$(RELEASE_LLVM_BIN)/clang !$${CCP} !' \
                   -e 's!^!cd $(DDIR)/test-build \&\& !' >> commands
	if [[ -z `cat commands` ]]; then \
		echo "Empty commands file, ERROR." ; exit 1 ; \
	fi
	echo "export CCP=\$$1" > run-commands.sh
	echo "head -n 300 commands | xargs -P50 -L1 -d \"\\n\" bash -x -c" \
                >> run-commands.sh
	chmod +x run-commands.sh

relink-clang-pgo-plo.sh relink-clang-cspgo-plo.sh: \
relink-clang-%-plo.sh: %-plo/build/bin/clang-9 %-compiler.symfile
	echo "pushd $(shell dirname $(shell dirname $<))" > $@
	ninja -C "$(shell dirname $(shell dirname $<))" -t commands clang \
          | grep -Fe " -o bin/clang-9 " \
          | sed -e "s! -o bin/clang-9 ! -o bin/clang-9-plo \
                     -Wl,-plo                              \
                     -Wl,-symfile=$(DDIR)/$(word 2,$^) \
                     -Wl,-profile=$(DDIR)/$(subst .symfile,.profile,$(word 2,$^)) \
                     -Wl,--lto-basicblock-sections=all     \
                     -Wl,--no-call-graph-profile-sort !"   \
               -e 's!$(LLD_NO_OPT)!$(LLD_OPT)!g'           \
	       -e 's! -Wl,-q !!g' | tr '\t' ' ' | tr -s ' ' >> $@
	echo 'EV=$$? ; popd ; exit $$EV' >> $@
	@if [[ `wc -l $@ | cut -f1 -d' '` -gt 2 ]] ; then \
	  chmod +x $@ ; \
	  echo "Done generating $@." ; \
	else \
	  echo "Generate $@ failed."; exit 1; \
	fi

benchmark-pgo benchmark-pgo-plo benchmark-cspgo benchmark-cspgo-plo benchmark-stage1: benchmark-%: %-compiler run-commands.sh
	{ time ./run-commands.sh `readlink -f $<` 2>&1 | grep -E "^(real|user|sys)" ; } 2>&1 | tee $@.result

# ######################################  not fully ready, WIP.
# sample.bc: sample.c
# 	./stage1-compiler -emit-llvm -c -O2 $< -o $@

# sample_bblabels.o: sample.bc
# 	./stage1-compiler -c -O2 -fbasicblock-sections=labels $< -o $@
# 	@[[ -n `readelf -Ws $@ | grep -F '.bb.'` ]] \
# 		|| { echo "No bb symbols found in $@, error" ; exit 1; }

# sample.out: sample_bblabels.o
# 	./stage1-compiler -Wl,-build-id -o $@ $<
# 	@[[ -n `readelf -Ws $@ | grep -F '.bb.'` ]] \
# 		|| { echo "No bb symbols found in $@, error" ; exit 1; }

# sample.perfdata: sample.out
# 	perf record -o $@ -e "cycles:u" -j any,u -- ./$<

# sample.out.symfile sample.out.profile: sample.out sample.perfdata
# 	./plotool.sh $^

# sample_bball.o: sample.bc
# 	./stage1-compiler -c -O2 -fbasicblock-sections=all $< -o $@

sample.plo.out sample.out.cfg: sample_bball.o sample.out.symfile sample.out.profile stage1-compiler
	stage1/install/bin/clang -fuse-ld=lld \
	  -Wl,-plo \
	  -Wl,-symfile=sample.out.symfile \
	  -Wl,-profile=sample.out.profile \
	  -Wl,-cfgdump=sample.out.cfg \
	  sample_bball.o -o sample.plo.out 2>&1 | tee sample.plo.out.autolog

# sample.out.layout: sample.out.cfg stage1-compiler
# 	stage1/install/bin/layout -cfg-read $< -layout-dump $@ | tee $@.autolog

.phony: clean clean-all
clean:
	rm -fr stage-{pgo,cspgo} stage-{pgo,cspgo}-compiler
	rm -f  stage-{pgo,cspgo}.profdata stage-cspgo2.profdata
	rm -f  {pgo,cspgo}.perfdata
	rm -fr pgo cspgo
	rm -fr {pgo,cspgo}-plo
	rm -fr {pgo,cspgo}-plo-compiler
	rm -f  {pgo,cspgo}-compiler 
	rm -f  {pgo,cspgo}-compiler.symfile
	rm -f  {pgo,cspgo}-compiler.profile
	rm -f  relink-clang-{cspgo,pgo}-plo.sh
	rm -f  benchmark-{pgo,cspgo}.result benchmark-{pgo,cspgo}-plo.result
	rm -f  commands run-commands.sh
	rm -f  sample.out sample.out.{layout,symfile,profile,cfg} sample_*.o sample.out sample.plo.out sample.bc sample.perfdata

clean-all: clean
	rm -fr stage1 ; rm -fr stage1-compiler
