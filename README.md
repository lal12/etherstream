# libetherstream
libetherstream is a header only C++14 library, which offers a network stream over ethernet. The packets are addressed using the MAC address. So you can connect to a device with changing IP.

It was originally developed, too support development and debugging on embedded linux devices, while working on the network interfaces, or changing its IPs. Other use cases are possible.

## OS Support
It currently only supports Linux, but a port to other UNIX-like OSes shouldn't be that hard. A windows port would be possible, but probably requires a little bit more work.

## Example projects
This repository also contains simple example implementations:
- File transfer client (esftc)
- File transfer server (esftd)
- Shell client (esftc)
- Shell server (esftd)

## Usage
TODO
