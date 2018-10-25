How to build
------------

```
# docker-compose up --force-recreate -d --build
# docker exec -it $(docker ps -f "ancestor=telegram/build:latest" --format "{{.ID}}") bash
(docker) # cd /srv/telegram
(docker) # .travis/build.sh # (run it twice, lol)
(docker) # exit
# cd ../out/Release
# ./Telegram
```

How to add peers to "soft-pinned" category
-----------------------------------------

Place peer id (*) list in text file under `~/.config/telegram_peers.conf`. Example:

```
9599166736 // such important channel
9731106532 // much awesome user
```

_(*) You can find peer id in group/channel info or user/bot profile._
