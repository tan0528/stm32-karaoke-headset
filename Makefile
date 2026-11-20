# ---------------------------------------------------------------------------
# STM32 线控耳机 K 歌系统 —— 构建与测试
#
#   make        构建仿真器与测试程序
#   make test   跑单元 + 集成测试（纯 C，无需 Python）
#   make demo   生成输入信号、跑全链路、出图（需要 numpy/matplotlib）
#   make clean  清理 build/
# ---------------------------------------------------------------------------

ifeq ($(OS),Windows_NT)
EXE   := .exe
SHELL := sh          # Windows 下用 Git Bash / MSYS 的 sh，保证 mkdir -p 可用
else
EXE   :=
endif

CC       ?= cc
PYTHON   ?= python
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
CPPFLAGS += -Ifirmware -Isim
LDLIBS   += -lm

BUILD    := build
SIM      := $(BUILD)/sim$(EXE)
TESTBIN  := $(BUILD)/tests$(EXE)

FW_SRC   := firmware/audio_core.c
SIM_SRC  := sim/sim_engine.c sim/sim_cli.c
TEST_SRC := tests/run_tests.c \
            tests/test_audio_core.c \
            tests/test_volume.c \
            tests/test_key.c \
            tests/test_engine.c

.PHONY: all test demo clean help

all: $(SIM) $(TESTBIN)

help:
	@echo "make        构建 build/sim 与 build/tests"
	@echo "make test   运行测试套件"
	@echo "make demo   跑完整演示链路并出图（需 numpy / matplotlib）"
	@echo "make clean  清理构建产物"

$(BUILD):
	@mkdir -p $(BUILD)

$(SIM): $(SIM_SRC) $(FW_SRC) firmware/audio_core.h firmware/audio_calc.h \
        sim/sim_engine.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SIM_SRC) $(FW_SRC) $(LDLIBS)

$(TESTBIN): $(TEST_SRC) $(FW_SRC) sim/sim_engine.c \
            firmware/audio_core.h firmware/audio_calc.h \
            sim/sim_engine.h tests/test_util.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_SRC) $(FW_SRC) sim/sim_engine.c $(LDLIBS)

test: $(TESTBIN)
	@$(TESTBIN)

demo: $(SIM)
	@$(PYTHON) tools/gen_inputs.py
	@$(PYTHON) tools/analyze.py

clean:
	@rm -rf $(BUILD)
	@rm -rf out
	@echo "cleaned"
