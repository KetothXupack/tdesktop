#!/bin/bash
sudo mkdir /sys/fs/cgroup/systemd
sudo mount -t cgroup -o none,name=systemd cgroup /sys/fs/cgroup/systemd
docker build -t telegram/build .
docker-compose up --force-recreate -d
docker exec -it $(docker ps -f "ancestor=telegram/build:latest" --format "{{.ID}}") bash
