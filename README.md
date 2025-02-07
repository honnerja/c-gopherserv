# C-Gopherserv
Server for the [Gopher](https://en.wikipedia.org/wiki/Gopher_(protocol)) internet protocol, written in C.

Currently a work in progress

Current Features:
  * Uses epoll to efficiently handle multiple connections.
  * Automatically generates directory listings
  * Supports custom titles and informational text for directory entries
  * No dependencies except libc

Planned Features:
  * Caching
  * Support user-defined scripts for file and directory generation
