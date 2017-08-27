LIBDIR    := reindexer

CC_FILES  := $(wildcard *.cc) $(wildcard $(LIBDIR)/cmd/reindexer_server/http/*.cc) $(LIBDIR)/pprof/backtrace.cc

OBJ_FILES := $(patsubst %.cc, .build/%.o, $(CC_FILES))

HLCUP_REINDEX := hlcup_reindex

CXXFLAGS  := -I$(LIBDIR) -I$(LIBDIR)/vendor -I$(LIBDIR)/cmd/reindexer_server -std=c++11 -g -O3 -Wall -Wpedantic -Wextra
LDFLAGS   :=  -L$(LIBDIR)/.build -lreindexer -lleveldb -lsnappy -lev -lpthread

all: $(HLCUP_REINDEX)

lib:
$(LIBDIR)/.build/libreindexer.a:
	+make -s -C $(LIBDIR)

.build/%.o: ./%.cc
	@mkdir -p $(dir $@)
	@echo CXX $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(LIBDIR)/.build/libreindexer.a: lib

$(HLCUP_REINDEX): $(OBJ_FILES)  $(LIBDIR)/.build/libreindexer.a
	@echo LD $@
	@$(CXX) $^ $(LDFLAGS) -o $@

clean:
	rm -rf .build .depend

.depend: $(CC_FILES)
	@$(CXX) -MM $(CXXFLAGS) $^ | sed "s/^\(.*\): \(.*\)\.\([cp]*\) /\.build\/\2.o: \2.\3 /" >.depend

-include .depend

install:
	cp $(HLCUP_REINDEX) /usr/bin
