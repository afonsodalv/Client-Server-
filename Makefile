
export MSG_FICH="\"messages_file.txt\""

all: manager feed

manager.o: manager.c common.h
	@echo "Compile manager.c"
	gcc -DMSG_FICH=$(MSG_FICH) -c manager.c -o manager.o

feed.o: feed.c common.h
	@echo "Compile feed.c"
	gcc -DMSG_FICH=$(MSG_FICH) -c feed.c -o feed.o

manager: manager.o
	gcc manager.o -o manager

feed: feed.o
	gcc feed.o -o feed

clean:
	rm -f *.o *.txt utils feed manager
