#! /bin/bash

tunName=$1
ip=$2

ip link set $tunName up
ip addr add $ip/24 dev $tunName 
