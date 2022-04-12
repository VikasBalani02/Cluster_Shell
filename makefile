all:
	gcc clustershell_server.c -lpthread -o server
	gcc clustershell_client.c -o client
server:
	gcc clustershell_server.c -lpthread -o server
client:
	gcc clustershell_client.c -o client