CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter -MMD -MP

# sa.cc es solo template; se incluye desde main.cc / experimentos.cc, NO se compila aparte.
SRCS_COMMON = codi/algo/ruta/estado_ruta.cc \
              codi/algo/ruta/heuristica/h_ruta.cc \
              codi/algo/ruta/operadores/op_ruta.cc \
              codi/algo/ruta/vecino/vecino.cc \
              codi/algo/ruta/greedy/greedy.cc \
              codi/io/exportar.cc

# --- Binario principal (comportamiento original) ---
SRCS_MAIN = codi/main.cc $(SRCS_COMMON)
OBJS_MAIN = $(SRCS_MAIN:.cc=.o)
DEPS_MAIN = $(OBJS_MAIN:.o=.d)
TARGET = damm

# --- Binario de experimentos ---
SRCS_EXP = codi/experimentos.cc $(SRCS_COMMON)
OBJS_EXP = $(SRCS_EXP:.cc=.o)
DEPS_EXP = $(OBJS_EXP:.o=.d)
TARGET_EXP = damm-exp

# =============================================================================
# Targets de compilación
# =============================================================================

all: $(TARGET)

$(TARGET): $(OBJS_MAIN)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TARGET_EXP): $(OBJS_EXP)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(DEPS_MAIN)
-include $(DEPS_EXP)

# =============================================================================
# Targets de ejecución — binario principal
# =============================================================================

# Uso: make run [SCENARIO=pequeno|medio|grande] [SEED=42] [GREEDY=set_cover]
#               [VECINO=completo] [OUTPUT=salida.json]
SCENARIO ?= pequeno
SEED     ?= 42
GREEDY   ?= set_cover
VECINO   ?= completo
OUTPUT   ?= salida.json

run: $(TARGET)
	./$(TARGET) $(SCENARIO) $(SEED) $(GREEDY) $(VECINO) $(OUTPUT)

# =============================================================================
# Targets de experimentos
# =============================================================================

# Ejecuta TODOS los experimentos (1-7) en secuencia.
exp: $(TARGET_EXP)
	./$(TARGET_EXP) all

# Experimentos individuales: make exp1 | exp2 | exp3 | exp4 | exp5 | exp6 | exp7
exp1: $(TARGET_EXP)
	./$(TARGET_EXP) 1

exp2: $(TARGET_EXP)
	./$(TARGET_EXP) 2

exp3: $(TARGET_EXP)
	./$(TARGET_EXP) 3

exp4: $(TARGET_EXP)
	./$(TARGET_EXP) 4

exp5: $(TARGET_EXP)
	./$(TARGET_EXP) 5

exp6: $(TARGET_EXP)
	./$(TARGET_EXP) 6

exp7: $(TARGET_EXP)
	./$(TARGET_EXP) 7

exp8: $(TARGET_EXP)
	./$(TARGET_EXP) 8

# =============================================================================
# Debug (sanitizers) — disponible para ambos binarios
# Uso: make debug | make debug-exp
# =============================================================================

debug: CXXFLAGS = -std=c++17 -O0 -g -Wall -Wno-unused-parameter -MMD -MP -fsanitize=address,undefined
debug: TARGET = damm-debug
debug: $(OBJS_MAIN)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS_MAIN)

debug-exp: CXXFLAGS = -std=c++17 -O0 -g -Wall -Wno-unused-parameter -MMD -MP -fsanitize=address,undefined
debug-exp: TARGET_EXP = damm-exp-debug
debug-exp: $(OBJS_EXP)
	$(CXX) $(CXXFLAGS) -o $(TARGET_EXP) $(OBJS_EXP)

# =============================================================================
# Visualización
# =============================================================================

VIZ_SCRIPT = viz/visualizar.py
VIZ_OUTPUT ?= viz/output/
VIZ_INPUT  ?= $(OUTPUT)   # por defecto usa el mismo JSON que genera `run`

# Genera los PNGs a partir de un JSON ya existente.
# Uso: make viz [VIZ_INPUT=otra_salida.json] [VIZ_OUTPUT=viz/output/]
viz:
	python3 $(VIZ_SCRIPT) $(VIZ_INPUT) --output $(VIZ_OUTPUT)

# Ejecuta el binario principal Y genera los PNGs en un solo paso.
# Uso: make run-viz [SCENARIO=medio] [SEED=99] [GREEDY=set_cover] [VECINO=completo]
run-viz: run
	python3 $(VIZ_SCRIPT) $(OUTPUT) --output $(VIZ_OUTPUT)

# =============================================================================
# Limpieza
# =============================================================================

clean:
	rm -f $(OBJS_MAIN) $(DEPS_MAIN) $(OBJS_EXP) $(DEPS_EXP) \
	      $(TARGET) $(TARGET).exe \
	      $(TARGET_EXP) $(TARGET_EXP).exe \
	      damm-debug damm-exp-debug \
	      experimento*.csv salida.json

clean-viz:
	rm -rf $(VIZ_OUTPUT)

clean-all: clean clean-viz

.PHONY: all run exp exp1 exp2 exp3 exp4 exp5 exp6 exp7 exp8 \
        debug debug-exp viz run-viz clean clean-viz clean-all