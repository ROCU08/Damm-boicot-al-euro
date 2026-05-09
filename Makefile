CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter -MMD -MP

# sa.cc es solo template; se incluye desde main.cc, NO se compila aparte.
SRCS = codi/main.cc \
       codi/algo/ruta/estado_ruta.cc \
       codi/algo/ruta/heuristica/h_ruta.cc \
       codi/algo/ruta/operadores/op_ruta.cc \
       codi/algo/ruta/vecino/vecino.cc \
       codi/algo/ruta/greedy/greedy.cc \
       codi/io/exportar.cc

OBJS = $(SRCS:.cc=.o)
DEPS = $(OBJS:.o=.d)

TARGET = damm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Auto-rastreo de dependencias (.h tocado → recompila los .o que lo incluyen).
-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(TARGET).exe

# Build para depurar: sanitizers + símbolos. Crea binario `damm-debug`.
debug: CXXFLAGS = -std=c++17 -O0 -g -Wall -Wno-unused-parameter -MMD -MP -fsanitize=address,undefined
debug: TARGET = damm-debug
debug: $(TARGET)

.PHONY: all clean debug
