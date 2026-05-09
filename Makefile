CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter

# sa.cc es solo template; se incluye desde main.cc, NO se compila aparte.
SRCS = codi/main.cc \
       codi/algo/ruta/estado_ruta.cc \
       codi/algo/ruta/heuristica/h_ruta.cc \
       codi/algo/ruta/operadores/op_ruta.cc \
       codi/algo/ruta/vecino/vecino.cc \
       codi/algo/ruta/greedy/greedy.cc \
       codi/io/exportar.cc

OBJS = $(SRCS:.cc=.o)

TARGET = damm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
