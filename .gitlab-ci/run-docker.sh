#!/bin/bash

set -e

TAG="registry.gitlab.gnome.org/gnome/libsoup/master:v3"

cd "$(dirname "$0")"
docker build --build-arg HOST_USER_ID="$UID" --tag "${TAG}" \
    --file "Dockerfile" .

if [ "$1" = "--push" ]; then
  docker login registry.gitlab.gnome.org
  docker push $TAG
else
  docker run --rm \
      --volume "$(pwd)/..:/home/user/app" --workdir "/home/user/app" \
      --tty --interactive "${TAG}" bash
fi
