# =========================
# Toolchain
# =========================
CXX := clang++
LD  := clang++

# =========================
# Targets
# =========================
TARGET_FUZZ     := fuzz_test
TARGET_SNAPSHOT := snapshot_test
TARGET_PERF     := perf_test

# =========================
# Sources
# =========================
SRC_ENGINE := \
	engine.cpp \
	orders.cpp \
	order_book.cpp \
	snapshot.cpp \
	snapshot_lz4.cpp \
	snapshot_delta.cpp \
	perf.cpp

SRC_FUZZ := \
	fuzz.cpp

SRC_SNAPSHOT := \
	snapshot_test.cpp

SRC_PERF := \
	perf_main.cpp

# =========================
# Objects
# =========================
OBJ_ENGINE   := $(SRC_ENGINE:.cpp=.o)
OBJ_FUZZ     := $(SRC_FUZZ:.cpp=.o)
OBJ_SNAPSHOT := $(SRC_SNAPSHOT:.cpp=.o)
OBJ_PERF     := $(SRC_PERF:.cpp=.o)

# =========================
# Includes / Libs
# =========================
INCLUDES := -I/opt/homebrew/include
LIBS     := -L/opt/homebrew/lib -llz4

# =========================
# Sanitizers (debug)
# =========================
SANITIZERS := -fsanitize=address,undefined

# =========================
# Compiler flags
# =========================
CXXFLAGS_COMMON := \
	-std=c++20 \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wconversion \
	-Wsign-conversion \
	-fno-exceptions \
	-fno-rtti \
	$(INCLUDES)

CXXFLAGS_DEBUG := \
	-O0 \
	-g \
	$(SANITIZERS)

CXXFLAGS_RELEASE := \
	-O3 \
	-march=native

LDFLAGS_DEBUG   := $(SANITIZERS)
LDFLAGS_RELEASE := $(LIBS)

# =========================
# Build mode
# =========================
BUILD ?= release

ifeq ($(BUILD),debug)
	CXXFLAGS := $(CXXFLAGS_COMMON) $(CXXFLAGS_DEBUG)
	LDFLAGS  := $(LIBS) $(LDFLAGS_DEBUG)
else
	CXXFLAGS := $(CXXFLAGS_COMMON) $(CXXFLAGS_RELEASE)
	LDFLAGS  := $(LDFLAGS_RELEASE)
endif

# =========================
# Rules
# =========================
.PHONY: all clean fuzz snapshot perf debug release

all: fuzz snapshot perf

debug:
	$(MAKE) BUILD=debug

release:
	$(MAKE) BUILD=release

# -------------------------
# Compile rule
# -------------------------
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -------------------------
# Fuzz test
# -------------------------
fuzz: $(OBJ_ENGINE) $(OBJ_FUZZ)
	$(LD) $^ $(LDFLAGS) -o $(TARGET_FUZZ)

# -------------------------
# Snapshot test
# -------------------------
snapshot: $(OBJ_ENGINE) $(OBJ_SNAPSHOT)
	$(LD) $^ $(LDFLAGS) -o $(TARGET_SNAPSHOT)

# -------------------------
# Perf test
# -------------------------
perf: $(OBJ_ENGINE) $(OBJ_PERF)
	$(LD) $^ $(LDFLAGS) -o $(TARGET_PERF)

# -------------------------
# Clean
# -------------------------
clean:
	rm -f *.o \
	      $(TARGET_FUZZ) \
	      $(TARGET_SNAPSHOT) \
	      $(TARGET_PERF)