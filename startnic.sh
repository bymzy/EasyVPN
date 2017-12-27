#! /bin/bash

ip link set tun0 up
ip addr add 10.0.10.1/24 dev tun0
