# Makefile - IOGL-homework (OpenGL / GLEW / GLFW / GLM)
#
# Chaque fichier *.cpp qui contient un main() est compile en son propre
# executable (ex: Lighting_Spot.cpp -> ./Lighting_Spot). Les fichiers
# communs (Camera, Mesh, ShaderProgram, Texture2D) sont compiles une seule
# fois et lies a chaque executable.
#
# Fonctionne aussi bien sous Linux que sous Windows via MSYS2/MinGW64 (ouvrir
# un "MSYS2 MinGW x64" shell, installer les paquets mingw-w64-x86_64-{glew,
# glfw,glm}, puis lancer "make" ici) - ca evite la couche de virtualisation
# d'affichage de WSLg et tourne directement sur le pilote GPU natif Windows.

BUILDDIR := build

CXX      := g++
CXXSTD   := -std=c++17
WARN     := -Wall

# Debug par defaut : make BUILD=release pour optimiser
BUILD ?= debug
ifeq ($(BUILD),release)
  OPT := -O2 -DNDEBUG
else
  OPT := -O0 -g
endif

ifeq ($(OS),Windows_NT)
  EXE_EXT := .exe
  SYS_LIBS := -lopengl32 -lgdi32
else
  EXE_EXT :=
  SYS_LIBS := -lGL -ldl -lpthread
endif

# glm is header-only and some MSYS2/vcpkg setups don't ship a glm.pc - query
# it separately (and ignore failure) so a missing glm.pc doesn't wipe out the
# glew/glfw3 flags too (pkg-config returns nothing for the whole list if any
# one package in it is missing). Its headers are on the default include path
# anyway once the package/library is installed.
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(shell pkg-config --cflags glew glfw3 2>/dev/null) $(shell pkg-config --cflags glm 2>/dev/null)
LDLIBS   := $(shell pkg-config --libs glew glfw3) $(SYS_LIBS)

# Fichiers source contenant un main() -> un executable chacun
MAIN_SRCS   := $(shell grep -l "int main" *.cpp)
BASENAMES   := $(patsubst %.cpp,%,$(MAIN_SRCS))
TARGETS     := $(addsuffix $(EXE_EXT),$(BASENAMES))

# Fichiers source communs (classes partagees, pas de main())
COMMON_SRCS := $(filter-out $(MAIN_SRCS),$(wildcard *.cpp))
COMMON_OBJS := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(COMMON_SRCS))

ALL_OBJS := $(COMMON_OBJS) $(patsubst %.cpp,$(BUILDDIR)/%.o,$(MAIN_SRCS))
DEPS     := $(ALL_OBJS:.o=.d)

.PHONY: all run clean rebuild list

all: $(TARGETS)

# On Windows, real targets end in .exe (Scene_AllModels.exe). These aliases
# let "make Scene_AllModels" work too, without needing to type the extension.
ifneq ($(EXE_EXT),)
.PHONY: $(BASENAMES)
$(BASENAMES): %: %$(EXE_EXT)
endif

# Scene de demonstration avec tous les modeles
run: Scene_AllModels$(EXE_EXT)
	./Scene_AllModels$(EXE_EXT)

# Chaque cible correspond a un fichier <Nom>.cpp contenant un main()
$(TARGETS): %$(EXE_EXT): $(BUILDDIR)/%.o $(COMMON_OBJS)
	$(CXX) $^ -o $@ $(LDLIBS)

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $@

list:
	@echo "Executables disponibles: $(TARGETS)"

clean:
	rm -rf $(BUILDDIR) $(TARGETS)

rebuild: clean all

-include $(DEPS)
