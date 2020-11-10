#!/usr/bin/env bash

cat -- write-bw-writeOff-send-*server >> write-bw-writeOff-send-server.data
cat -- write-bw-writeOff-read-*server >> write-bw-writeOff-read-server.data

cat -- write-bw-writeRev-send-*server >> write-bw-writeRev-send-server.data
cat -- write-bw-writeRev-read-*server >> write-bw-writeRev-read-server.data

cat -- write-bw-writeImm-send-*server >> write-bw-writeImm-send-server.data
cat -- write-bw-writeImm-read-*server >> write-bw-writeImm-read-server.data


