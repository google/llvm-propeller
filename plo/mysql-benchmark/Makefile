SHELL := /bin/bash -o pipefail
DDIR  := $(shell pwd)

#### The following need to be changed.
MYSQL_SOURCE         ?= $(DDIR)/../mysql-server
BOLT_BIN_DIR         ?= $(HOME)/copt2/bolt/build-bolt.fb/bin
CREATE_LLVM_PROF     ?= $(HOME)/copt2/autofdo-forked/create_llvm_prof
COMPILER_INSTALL_BIN ?= /usr/local/google/home/shenhan/copt2/llvm-propeller-6/plo/stage1/install/bin
REPORT_SIZE          ?= $(HOME)/copt2/llvm-propeller-6/plo/stage1/install/bin/report-size
####

ITERATIONS ?= 25
COMMA = ,

ifeq ($(J_NUMBER),)
CORES := $(shell grep ^cpu\\scores /proc/cpuinfo | uniq |  awk '{print $$4}')
THREADS  := $(shell grep -Ee "^core id" /proc/cpuinfo | wc -l)
THREAD_PER_CORE := $(shell echo $$(($(THREADS) / $(CORES))))
# leave some cores on the machine for other jobs.
USED_CORES := $(shell \
	if [[ "$(CORES)" -lt "3" ]] ; then \
	  echo 1 ; \
	elif [[ "$(CORES)" -lt "9" ]] ; then \
	  echo $$(($(CORES) * 3 / 4)) ; \
	else echo $$(($(CORES) * 7 / 8)); \
	fi )
J_NUMBER := $(shell echo $$(( $(USED_CORES) * $(THREAD_PER_CORE))))
endif

gen_compiler_flags     = -DCMAKE_C_FLAGS=$(1) -DCMAKE_CXX_FLAGS=$(1)
gen_linker_flags       = -DCMAKE_EXE_LINKER_FLAGS=$(1) -DCMAKE_SHARED_LINKER_FLAGS=$(1) -DCMAKE_MODULE_LINKER_FLAGS=$(1)
common_compiler_flags := -fuse-ld=lld -DDBUG_OFF -ffunction-sections -fdata-sections -O3 -DNDEBUG -Qunused-arguments -funique-internal-linkage-names
common_linker_flags   := -Wl,-z,keep-text-section-prefix
thin_lto_linker_flags  = -flto=thin -Wl,--thinlto-cache-dir=$(DDIR)/$(shell echo $@ | cut -d/ -f1)/thin-lto-cache-dir

# $1 are compiler cluster.
# $2 are ld flags.
gen_build_flags = $(call gen_compiler_flags,"$(1) $(common_compiler_flags)") $(call gen_linker_flags,"$(2) $(common_linker_flags)")

define build_mysql
	$(eval __comp_dir=$(DDIR)/$(shell echo $@ | sed -Ee 's!([^/]+)/.*!\1!'))
	if [[ -z "$(__comp_dir)" ]]; then echo "Invalid dir name" ; exit 1; fi
	echo "Building in directory: $(__comp_dir) ... " ;
	if [[ ! -e "$(__comp_dir)/build/CMakeCache.txt" ]]; then          \
	    mkdir -p $(__comp_dir)/build ;                                \
	    cd $(__comp_dir)/build && cmake --debug-trycompile -G Ninja   \
		-DCMAKE_INSTALL_PREFIX=$(__comp_dir)/install              \
		-DCMAKE_LINKER="lld"                                      \
		-DDOWNLOAD_BOOST=1                                        \
		-DWITH_BOOST=$(DDIR)/boost                                \
		-DCMAKE_BUILD_TYPE=Release                                \
		-DCMAKE_C_COMPILER="$(COMPILER_INSTALL_BIN)/clang"        \
		-DCMAKE_CXX_COMPILER="$(COMPILER_INSTALL_BIN)/clang++"    \
		-DCMAKE_ASM_COMPILER="$(COMPILER_INSTALL_BIN)/clang"      \
		$(1)                                                      \
		$(MYSQL_SOURCE); \
	fi
	sed -i -Ee "s! =thin! -flto=thin!g" $(__comp_dir)/build/build.ninja
	ninja install -j$(J_NUMBER) -C $(__comp_dir)/build $(3) 2>&1 | tee $(DDIR)/$(shell basename $(__comp_dir)).autolog || exit 1
	touch $@
endef

define setup_mysql
	$(eval __comp_dir=$(DDIR)/$(shell echo $@ | sed -Ee 's!([^/]+)/.*!\1!'))
	if [[ -z "$(__comp_dir)" ]]; then echo "Invalid dir name" ; exit 1; fi
	echo "Setup in directory: $(__comp_dir) ... " ;
	mkdir -p $(__comp_dir)/install/mysql-files && \
	echo "[mysqld]" > $(__comp_dir)/my.cnf && \
	echo "default-authentication-plugin=mysql_native_password" >> $(__comp_dir)/my.cnf && \
	$(__comp_dir)/install/bin/mysqld --defaults-file=$(__comp_dir)/my.cnf --initialize-insecure --user=${USER}
endef

# $(1) - The name of the test from /usr/share/sysbench/*.lua, eg oltp_read_only
# $(2) - The number of iterations
# $(3) - The table size to use
# $(4) - The number of events to use
# $(5) - Additional args to pass to the run phase.
define run_loadtest
	@mkdir -p runfiles
	sysbench $(1) --table-size=$(3) --num-threads=1 --rand-type=uniform --rand-seed=1 --db-driver=mysql \
		--mysql-db=sysbench --tables=1 --mysql-socket=/tmp/mysql.sock --mysql-user=root prepare
	@{ if [[ "$(3)" -ge "10000" ]]; then \
		sysbench $(1) --table-size=$(3) --tables=1 --num-threads=1 --rand-type=uniform --rand-seed=1 --db-driver=mysql \
			--mysql-db=sysbench --mysql-socket=/tmp/mysql.sock --mysql-user=root prewarm; \
	fi; }
	@echo "Running test: $(1) $(2)x"
	@{ for i in {1..$(2)}; do \
		sysbench $(1) --table-size=$(3) --tables=1 --num-threads=1 --rand-type=uniform --rand-seed=1 --db-driver=mysql \
			--events=$(4) --time=0 --rate=0 $(5) \
			--mysql-db=sysbench --mysql-socket=/tmp/mysql.sock --mysql-user=root run &> $(DDIR)/runfiles/$@.$(1).$$i.sysbench; \
	done; }
	sysbench $(1) --table-size=$(3) --tables=1 --num-threads=1 --rand-type=uniform --rand-seed=1 --db-driver=mysql \
		--mysql-db=sysbench --mysql-socket=/tmp/mysql.sock --mysql-user=root cleanup
endef

export define run_loadtests
cat << EOF | sed -Ee 's!^\s+@!!' -e 's!^\s+!!' -e 's!\.\.sysbench!\.sysbench_perfonly!'
#/bin/bash
echo "Start server stage-pgo"
{ ./\$$1 & }
echo "Waiting 10s for server to start"
sleep 10
echo "Running training load"
./mysql -u root -e "DROP DATABASE IF EXISTS sysbench; CREATE DATABASE sysbench;"
$(call run_loadtest,oltp_read_write,1,2000,500)
$(call run_loadtest,oltp_update_index,1,2000,500)
$(call run_loadtest,oltp_delete,1,2000,500)
$(call run_loadtest,select_random_ranges,1,2000,500)
echo "Shutdown server"
./mysqladmin -u root shutdown
EOF
endef

run_loadtests.sh:
	@eval "$$run_loadtests" > $@
	chmod +x $@

# The plain version is used for all commands such as admin and sending queries.
# Only the mysqld binary from the others are used.
plain/install/bin/mysqld:
	$(call build_mysql,$(call gen_build_flags))
	$(call setup_mysql)

mysql mysqladmin: %: plain/install/bin/mysqld
	ln -sf plain/install/bin/$* $@
	touch $@

# The remaining rules are for the different flavours of mysqld
stage-pgo/install/bin/mysqld: mysql mysqladmin
	$(call build_mysql,-DFPROFILE_GENERATE=ON $(call gen_build_flags))
	$(call setup_mysql)

stage-pgo-training: stage-pgo-mysqld
	@echo "Start server stage-pgo"
	@{ ./stage-pgo-mysqld &> $<.log & }
	@echo "Waiting 10s for server to start"
	@sleep 10
	@echo "Running training load"
	./mysql -u root -e "DROP DATABASE IF EXISTS sysbench; CREATE DATABASE sysbench;"
        # Keep the table size < 10000 to avoid prewarm during fdo training.
	$(call run_loadtest,oltp_read_write,1,2000,500)
	$(call run_loadtest,oltp_update_index,1,2000,500)
	$(call run_loadtest,oltp_delete,1,2000,500)
	$(call run_loadtest,select_random_ranges,1,2000,500)
	@echo "Shutdown server"
	./mysqladmin -u root shutdown
	touch $@

stage-pgo/profile/default.profdata: stage-pgo-training
	@echo "Combining profdata"
	mkdir -p $(shell dirname $@)
	$(COMPILER_INSTALL_BIN)/llvm-profdata merge -o $@ stage-pgo/profile-data

pgo-vanilla/install/bin/mysqld: mysql mysqladmin stage-pgo/profile/default.profdata
	$(call build_mysql,-DFPROFILE_USE=ON -DFPROFILE_DIR=$(DDIR)/stage-pgo/profile $(call gen_build_flags))
	$(call setup_mysql)

pgolto-vanilla/install/bin/mysqld: mysql mysqladmin stage-pgo/profile/default.profdata
	$(call build_mysql,-DFPROFILE_USE=ON -DFPROFILE_DIR=$(DDIR)/stage-pgo/profile $(call gen_build_flags,-flto=thin,$(thin_lto_linker_flags)))
	$(call setup_mysql)

pgolto-bolt/install/bin/mysqld: mysql mysqladmin stage-pgo/profile/default.profdata
	$(call build_mysql,-DFPROFILE_USE=ON -DFPROFILE_DIR=$(DDIR)/stage-pgo/profile $(call gen_build_flags,-flto=thin,-Wl$(COMMA)-q $(thin_lto_linker_flags)))
	$(call setup_mysql)
	if ! readelf -WS $@ | grep -qE '\.rela\.text\s+RELA' ; then echo Missing reloc section ; rm $@ ; exit 1; fi

pgolto-labels/install/bin/mysqld: mysql mysqladmin stage-pgo/profile/default.profdata
	$(call build_mysql,-DFPROFILE_USE=ON -DFPROFILE_DIR=$(DDIR)/stage-pgo/profile $(call gen_build_flags,-flto=thin -fbasic-block-sections=labels,-Wl$(COMMA)--no-call-graph-profile-sort -Wl$(COMMA)--lto-basic-block-sections=labels $(thin_lto_linker_flags)))
	$(call setup_mysql)

pgolto-labels/perf.data pgolto-bolt/perf.data: %/perf.data: %-mysqld run_loadtests.sh
	perf record -e cycles:u -j any,u -o $@ -- ./run_loadtests.sh $<

pgolto-labels/propeller.cluster pgolto-labels/propeller.symorder &: pgolto-labels/perf.data pgolto-labels-mysqld
	$(CREATE_LLVM_PROF) --binary=$(shell readlink -f $(lastword $^)) --profile=$< --format=propeller --out=pgolto-labels/propeller.cluster --sym_order_out=pgolto-labels/propeller.symorder --logtostderr || \
		{ rm -f pgolto-labels/propeller.cluster pgolto-labels/propeller.symorder ; exit 1 ; }

pgolto-bbinfo/install/bin/mysqld: mysql mysqladmin stage-pgo/profile/default.profdata
	$(call build_mysql,-DFPROFILE_USE=ON -DFPROFILE_DIR=$(DDIR)/stage-pgo/profile $(call gen_build_flags,-flto=thin -mllvm --bb-info-section,-mllvm --bb-info-section -Wl$(COMMA)-mllvm$(COMMA)--bb-info-section -Wl$(COMMA)--no-call-graph-profile-sort $(thin_lto_linker_flags)))
	$(call setup_mysql)

pgolto-propeller/install/bin/mysqld: mysql mysqladmin stage-pgo/profile/default.profdata pgolto-labels/propeller.cluster pgolto-labels/propeller.symorder
	$(call build_mysql,-DFPROFILE_USE=ON -DFPROFILE_DIR=$(DDIR)/stage-pgo/profile $(call gen_build_flags,-funique-internal-linkage-names -flto=thin -fbasic-block-sections=list=$(DDIR)/pgolto-labels/propeller.cluster,-Wl$(COMMA)--no-call-graph-profile-sort -Wl$(COMMA)--lto-basic-block-sections=$(DDIR)/pgolto-labels/propeller.cluster $(thin_lto_linker_flags) -Wl$(COMMA)--symbol-ordering-file=$(DDIR)/pgolto-labels/propeller.symorder))
	$(call setup_mysql)

pgolto-bolt/perf.fdata: pgolto-bolt/perf.data pgolto-bolt-mysqld
	$(BOLT_BIN_DIR)/perf2bolt -p $< -o $@ `readlink -f pgolto-bolt-mysqld`

pgolto-bolt/install/bin/mysqld-bolted: pgolto-bolt-mysqld pgolto-bolt/perf.fdata
	$(BOLT_BIN_DIR)/llvm-bolt $(shell readlink -f $<) -o $(shell readlink -f $<)-bolted -data=$(lastword $^) -relocs -reorder-blocks=cache+ -reorder-functions=hfsort -split-functions=2 -split-all-cold -split-eh -dyno-stats

pgolto-bolt/install/bin/mysqld-bolted-2: pgolto-bolt-mysqld pgolto-bolt/perf.fdata
	$(BOLT_BIN_DIR)/llvm-bolt $(shell readlink -f $<) -o $(shell readlink -f $<)-bolted-2 -data=$(lastword $^) -relocs -reorder-blocks=cache+ -reorder-functions=hfsort -split-functions=2 -split-all-cold -split-eh -dyno-stats \
		-reg-reassign -use-aggr-reg-reassign -peepholes=all -frame-opt=hot -frame-opt-rm-stores -group-stubs \
		-icf -align-macro-fusion=hot -jump-tables=split -indirect-call-promotion=all -group-stubs -eliminate-unreachable 

pgolto-bolted-mysqld: pgolto-bolt/install/bin/mysqld-bolted
	ln -sf $< $@
	touch $@

pgolto-bolted-2-mysqld: pgolto-bolt/install/bin/mysqld-bolted-2
	ln -sf $< $@

plain-mysqld stage-pgo-mysqld pgo-vanilla-mysqld pgolto-vanilla-mysqld pgolto-bolt-mysqld pgolto-labels-mysqld pgolto-bbinfo-mysqld pgolto-propeller-mysqld: %-mysqld: %/install/bin/mysqld
	ln -sf $< $@
	touch $@

benchmark-plain benchmark-pgo-vanilla benchmark-pgolto-vanilla benchmark-pgolto-bolted benchmark-pgolto-bolted-2 benchmark-pgolto-propeller: benchmark-%: %-mysqld mysql mysqladmin
	@echo "Start server $*"
	@{ ./$< &> $<.log & }
	@echo "Waiting 10s for server to start"
	@sleep 10
	@echo "Running benchmark"
	./mysql -u root -e "DROP DATABASE IF EXISTS sysbench; CREATE DATABASE sysbench;"
	$(call run_loadtest,oltp_read_only,${ITERATIONS},500000,30000,--range_selects=off --skip_trx)
	@echo "Shutdown server"
	./mysqladmin -u root shutdown
	touch $@

# $1 is flavor (plain, pgo, pgo-vanilla, pgo-bolt, etc)
# $2 is benchmark name
define generate_summary
	rm -f benchmark-$(1).$(2).sysbench.summary
	for i in {1..${ITERATIONS}}; do \
		sed -nEe 's!^\s+transactions:\s+.*\((.*) per sec\.\)$$$$!\1!p' runfiles/benchmark-$(1).$(2).$$$${i}.sysbench >> benchmark-$(1).$(2).sysbench.summary ; \
	done
endef

t-test: ../t-test.cc
	pushd .. ; make $@; popd

define a_v_b
$(1)-vs-$(2): benchmark-$(1) benchmark-$(2) t-test
	$(call generate_summary,$(1),oltp_read_only)
	$(call generate_summary,$(2),oltp_read_only)
	../t-test benchmark-{$(1),$(2)}.oltp_read_only.sysbench.summary 2>&1 | tee $$@
endef

$(eval $(call a_v_b,plain,pgo-vanilla))

$(eval $(call a_v_b,pgo-vanilla,pgolto-vanilla))

$(eval $(call a_v_b,plain,pgolto-vanilla))

$(eval $(call a_v_b,pgolto-vanilla,pgolto-bolted))

$(eval $(call a_v_b,pgolto-vanilla,pgolto-bolted-2))

$(eval $(call a_v_b,pgolto-bolted,pgolto-bolted-2))

$(eval $(call a_v_b,pgolto-vanilla,pgolto-propeller))

pgolto-vanilla-mysqld.size pgolto-propeller-mysqld.size: %-mysqld.size: %-mysqld
	{ echo "Executable: " ; \
	  $(REPORT_SIZE) $< ; \
	  $(REPORT_SIZE) $*/thin-lto-cache-dir/llvmcache-* ; } 2>&1 | tee $(DDIR)/$@

.PHONY: clean clean-all

clean:
	# links
	rm -f mysql mysqladmin *-mysqld
	# dirs
	rm -rf plain stage-pgo pgo-vanilla pgolto-vanilla pgolto-bolt pgolto-labels pgolto-bbinfo pgolto-propeller
	# logs
	rm -f *.autolog *.log *.sysbench *.sysbench.summary
	# action timestap
	rm -fr stage-pgo-training benchmark-{plain,pgo-vanilla,pgolto-vanilla,pgolto-bolt,pgolto-bolted,pgolto-bolted-2,pgolto-propeller}
	# others
	rm -f t-test *.autolog
	rm -fr runfiles
	rm -fr pgolto-vanilla-vs-* pgolto-bolted-vs-* plain-vs-* pgo-vanilla-vs-*
	rm -f run_loadtests.sh
	rm -f pgolto-vanilla-mysqld.size pgolto-bolted-mysqld.size

clean-propeller:
	rm -fr pgolto-labels* pgolto-propeller* benchmark-pgolto-propeller*
	if [[ -d "runfiles" ]]; then rm -f runfiles/benchmark-pgolto-propeller* ; fi

clean-all: clean clean-propeller
	rm -rf boost
