.PHONY:all
all:client server
client:client.c
	gcc -pthread -o client client.c -lm
server:server.c
	gcc -pthread -o server server.c -lm
.PHONY:clean
clean:
	rm -f client server myfifo

