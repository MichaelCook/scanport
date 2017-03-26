/*
  For a given TCP port, try connecting to every IP address on the LAN and tell
  which ones succeed.  Attempts connections in parallel.

  Arguments are: TIMEOUT SUBNET PORT.  TIMEOUT is seconds (floating point),
  the maximum amount of time to wait for each connection.

  Examples:

    g++ -Wall -Werror -std=c++11 -s -O3 scanport.cpp -lpthread -o scanport
    time ./scanport 0.5 10.60.3.0/24 80

    for i in $(seq 1 32); do scanport 0.5 10.60.$i.0/24 8090; done
*/

#include <iostream>
#include <stdexcept>
#include <future>
#include <vector>
#include <libgen.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cmath>
#include <cassert>
#include <regex>

namespace
{

char const* program_name;

std::string errStr()
{
  return strerror(errno);
}

std::string try_host(timeval timeout, std::string ipaddr, int port)
{
  int sockfd = -1;
  std::shared_ptr<void> finally{ nullptr,
      // make sure this socket gets closed
      [&sockfd](void*) { close(sockfd); }
  };
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    throw std::runtime_error("socket: " + errStr());

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, ipaddr.c_str(), &sa.sin_addr) <= 0)
    throw std::runtime_error("inet_pton: " + ipaddr + ": " + errStr());

  if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
    throw std::runtime_error("fcntl: " + errStr());

  if (connect(sockfd, (sockaddr*) &sa, sizeof(sa)) == 0)
    // connection succeeded immediately
    return ipaddr;
  if (errno != EINPROGRESS)
    throw std::runtime_error("connect: " + errStr());

  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET(sockfd, &fdset);
  timeval tv = timeout;
  int r = select(sockfd + 1, nullptr, &fdset, nullptr, &tv);
  if (r == -1)
    throw std::runtime_error("select: " + errStr());
  if (r == 0)
    // timeout
    return {};

  int err = 0;
  socklen_t len = sizeof(err);
  r = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len);
  if (r == -1)
    throw std::runtime_error("getsockopt: " + errStr());
  if (err != 0)
    // not connected
    return {};

  return ipaddr;
}

template <typename T>
T string_to(std::string const&);

/* Convert a string representation of a floating point number into a timeval
   object, seconds and microseconds. */
template <>
timeval string_to(std::string const& s)
{
  try
  {
    size_t idx;
    double v = std::stod(s, &idx);
    if (idx == s.size())
    {
      double i;
      double f = modf(v, &i);
      long sec = i;
      if (sec == i)             // check for overflow
      {
        long usec = f * 1e6;
        return{ sec, usec };
      }
    }
  }
  catch (std::exception&)
  {}
  throw std::invalid_argument("Invalid floating point number '" + s + '\'');
}

template <>
uint16_t string_to(std::string const& s)
{
  try
  {
    size_t idx;
    auto v = std::stoul(s, &idx);
    uint16_t r = v;
    if (r == v and idx == s.size())
      return r;
  }
  catch (std::exception&)
  {}
  throw std::invalid_argument("Invalid integer '" + s + '\'');
}

} // namespace

int main(int argc, char** argv)
try
{
  program_name = basename(argv[0]);

  if (argc != 4)
    throw std::runtime_error("wrong usage");
  auto timeout = string_to<timeval>(*++argv);
  std::string subnet{ *++argv };
  auto port = string_to<uint16_t>(*++argv);

  // Currently supports only 24-bit IPv4 subnets, "x.x.x.0/24".
  {
    std::regex subnet_re{R"(^(\d{1,3}\.\d{1,3}\.\d{1,3}\.)0/24$)"};
    std::smatch matched;
    if (not std::regex_match(subnet, matched, subnet_re))
      throw std::runtime_error("Invalid subnet '" + std::string(subnet) + '\'');
    assert(matched.size() == 2);
    subnet = matched[1]; // e.g., "10.60.3."
  }

  using Future = std::future<std::string>;
  std::vector<Future> futures;
  for (int i = 1; i < 255; ++i)
  {
    std::string ipaddr = subnet + std::to_string(i);
    futures.emplace_back(std::async(std::launch::async, try_host, timeout,
                                    std::move(ipaddr), port));
  }
  for (auto& f : futures)
  {
    auto ipaddr = f.get();
    if (not ipaddr.empty())
      std::cout << ipaddr << std::endl;
  }
}
catch (std::exception& exc)
{
  std::clog << program_name << ": " << exc.what() << std::endl;
  exit(EXIT_FAILURE);
}
