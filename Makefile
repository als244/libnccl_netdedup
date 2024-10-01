CC = gcc
CFLAGS_DEBUG = -g -fPIC
CFLAGS = -O3 -fPIC -march=native

BUILD_CFLAGS = ${CFLAGS_DEBUG}
LIB_BUILD_CFLAGS = -shared ${BUILD_CFLAGS}

NCCL_PLUGIN = libnccl-netdedup.so

NCCL_PLUGIN_OBJS = fingerprint_cache.o fingerprint_table.o fingerprint.o

EXT_LIB_LINKS = -ldl -pthread -lcrypto -lrt

all: $(NCCL_PLUGIN)

libnccl-netdedup.so: plugin_netdedup.c ${NCCL_PLUGIN_OBJS}
	${CC} ${LIB_BUILD_CFLAGS} $^ -o $@ ${EXT_LIB_LINKS}

fingerprint_cache.o: fingerprint_cache.c
	${CC} ${BUILD_CFLAGS} -c $^

fingerprint_table.o: fingerprint_table.c
	${CC} ${BUILD_CFLAGS} -c $^

fingerprint.o: fingerprint.c
	${CC} ${BUILD_CFLAGS} -c $^


clean:
	rm -f ${NCCL_PLUGIN} ${NCCL_PLUGIN_OBJS}
