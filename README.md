# Simple C Server with Huffman Encoding

This program was developed for a university assignment.

This program uses sockets to create a server that takes in commands from servers and then acts on them.

The server can both compress and decompress information with a Huffman tree. This compression is performed bit by bit and does not conform to Byte boundaries.

The server uses multiple processes in order to complete different commands at the same time.

This repo only contains code for the server, not the client.
