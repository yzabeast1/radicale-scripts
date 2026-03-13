CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?= -static -static-libgcc -static-libstdc++

ANNIVERSARY_FIXER := build/anniversary-fixer
TASK_ARCHIVER := build/task-archiver
VCF_TO_ICS := build/vcf-to-ics

TARGETS := $(ANNIVERSARY_FIXER) $(TASK_ARCHIVER) $(VCF_TO_ICS)

.PHONY: all clean anniversary-fixer task-archiver vcf-to-ics

all: $(TARGETS)

anniversary-fixer: $(ANNIVERSARY_FIXER)

task-archiver: $(TASK_ARCHIVER)

vcf-to-ics: $(VCF_TO_ICS)

$(ANNIVERSARY_FIXER): anniversary-fixer/main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TASK_ARCHIVER): task-archiver/main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(VCF_TO_ICS): vcf-to-ics/vcf-to-ics.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
