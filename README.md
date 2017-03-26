scanport
=================

For a given TCP port, try connecting to every IP address on the LAN and tell
which ones succeed.  Attempts connections in parallel.

Arguments are: TIMEOUT SUBNET PORT.  TIMEOUT is seconds (floating point), the
maximum amount of time to wait for each connection.

Examples:

  time ./scanport 0.5 10.60.3.0/24 80

  for i in $(seq 1 32); do scanport 0.5 10.60.$i.0/24 80; done

To build:

  g++ -Wall -Werror -std=c++11 -s -O3 scanport.cpp -lpthread -o scanport
