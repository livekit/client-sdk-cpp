#!/usr/bin/env bash
# Copyright 2025 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCKERFILE="${PROJECT_ROOT}/ros/docker/Dockerfile"
IMAGE_NAME="ros2-livekit"
WORKSPACE_DIR="/$HOME/workspaces/"
TAG=""
RUN="1"

usage() {
  cat <<EOF
Usage: $0 -t <tag>

Build and Run the ROS2 LiveKit Docker image.

Options:
  -t <tag>   Tag for the image (required). Image will be named ros2-livekit:<tag>
  -n <no-run>   Do not run the container after building it.

Example:
  $0 -t latest
  $0 -t v1.0.0
  $0 -t v1.0.0 -n
EOF
}

while getopts "t:n:h" opt; do
  case "$opt" in
    t) TAG="$OPTARG" ;;
    n) RUN="0" ;;
    h) usage; exit 0 ;;
    *) usage; exit 1 ;;
  esac
done

if [[ -z "$TAG" ]]; then
  echo "ERROR: -t <tag> is required"
  usage
  exit 1
fi

if [[ ! -f "$DOCKERFILE" ]]; then
  echo "ERROR: Dockerfile not found at $DOCKERFILE"
  exit 1
fi

cd "$PROJECT_ROOT"
docker build -t "${IMAGE_NAME}:${TAG}" -f "$DOCKERFILE" .

if [[ "$RUN" == "1" ]]; then
  docker run -it --network host --volume=${WORKSPACE_DIR}:/workspace ${IMAGE_NAME}:${TAG} /bin/bash
fi