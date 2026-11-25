CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS := -lm

CORE_SRCS := src/common.c \
             src/corpus.c \
             src/tokenizer.c \
             src/index.c \
             src/persistence.c \
             src/search.c \
             src/benchmark.c \
             src/report.c

APP_SRCS  := $(CORE_SRCS) src/main.c
TEST_SRCS := $(CORE_SRCS) tests/test_suite.c

ifeq ($(OS),Windows_NT)
    EXE := .exe
    RM  := del /Q
else
    EXE :=
    RM  := rm -f
endif

TARGET := csir$(EXE)
TEST   := test_suite$(EXE)

.PHONY: all test bench clean

all: $(TARGET)

$(TARGET): $(APP_SRCS) include/*.h
	$(CC) $(CFLAGS) $(APP_SRCS) -o $(TARGET) $(LDFLAGS)

$(TEST): $(TEST_SRCS) include/*.h
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST) $(LDFLAGS)

test: $(TEST)
	./$(TEST)

bench: $(TARGET)
	./$(TARGET) --benchmark data/sample_corpus --eval data/eval_corpus data/eval_queries.txt data/eval_qrels.txt --report-txt report.txt --report-csv benchmark_report.csv

clean:
	$(RM) $(TARGET) $(TEST) *.o *.idx report.txt report.csv benchmark_report.csv 2>nul || true
