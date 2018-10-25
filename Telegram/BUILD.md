How to build
------------

```
docker-compose up --force-recreate -d --build
docker exec -it $(docker ps -f "ancestor=telegram/build:latest" --format "{{.ID}}") bash
% cd /srv/telegram
% .travis/build.sh # (run it twice, lol)
```