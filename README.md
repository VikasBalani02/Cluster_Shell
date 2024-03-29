# Cluster_Shell

Allows remote execution of commands on linux machines. The clustershell server application can be run on multiple devices. The clustershell client can issue commands to be executed on local machine or on server nodes. Features like broadcasting commands and inter-node piping are also provided.

### Issuing Commands in ClientShell
1. <server_node_alias>.<command> : The syntax for issuing a command to a server node.<br /> 
Example: To execute a 'ls' command on a server node with alias 'n1' the command to cluster shell must be n1.ls 
2. n*.<command> : To run a command on all active nodes you can use the broadcasting feature. 
3. nodes : to view all active nodes
4. clustertop : to view the cpu and memory usage on all active nodes
5. <server_node_1>.<command1> | <server_node_2.>.<command2> | ... : Multiple piping<br /> 
Example: n1.ls | n2.grep "f" | n3.wc : Run ls on node1 , grep on node 2 and word count on node 3. Final output is displayed on client interface
6. Any command issued without the <server_node_alias> is executed on local machine.

### Usage:
**Server**
```
make server
./server
```

**Client**
```
make client
./client config
```

**Config File**

The config file must contain the name - IP pair of the server nodes. The name is an alias that the client assigns to each server node.<br />
Row Format:<br />
< server_node_alias > , < IP > <br />
Refer to config.txt for an example

### Design Details
Please refer to Design Document.pdf

