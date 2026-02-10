
CXX := g++
CXXFLAGS := -fPIC -I../include

OBJS += osvnvme.o
OBJS += osvthread.o
OBJS += leanstore_debug.o

HEADERS += ../include/osv/nvme.hh
HEADERS += ../include/osv/lw_thread.hh
HEADERS += ../include/osv/leanstore_debug.hh
HEADERS += ../drivers/
all: libfakeosv.so

libfakeosv.so: $(OBJS)
	$(CXX) -shared -lpthread -o $@ $(OBJS)

libfakeosv.a: $(OBJS)
	ar rcs $@ $(OBJS)

%.o: %.cc $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $<

clean:
	rm -rf *.o *.a *.so