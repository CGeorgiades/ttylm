BUILD_DIR=.
SOURCE_DIR=${BUILD_DIR}/..

SRCS=ttylm.c
OBJS=${SRCS:.c=.o}

all: ttylm

ttylm: ${}
	$(CC) $(OBJS) -o $@

%.o:%.c
	$(CC) $^ -c -o $@


install: all
	cp ./ttylm /bin/
	chmod +x /bin/ttylm

install_sh:
	cp ./ttylm.sh /bin/ttylm
	chmod +x /bin/ttylm
