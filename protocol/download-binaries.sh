#!/bin/bash

# This script downloads pre-built Whist protocol libraries from AWS S3, on all OSes

# Exit on errors and missing environment variables
set -Eeuo pipefail

###############################
# Detect Parameters
###############################

OS="$1"
shift
DEST_DIR="$1"
shift
if [[ $OS == "Darwin" ]]; then
  MACOS_ARCH="${1:-x86_64}"
  shift
else
  MACOS_ARCH="x86_64"
fi
POSITION_INDEPENDENT_CODE="${1:-OFF}"
shift
CLIENT_DIR="$DEST_DIR/client/build64"
SERVER_DIR="$DEST_DIR/server/build64"
CACHE_DIR="$DEST_DIR/download-binaries-cache"
mkdir -p "$CLIENT_DIR"
mkdir -p "$SERVER_DIR"
mkdir -p "$CACHE_DIR"

CACHE_FILE="$CACHE_DIR/libcache"
touch "$CACHE_FILE"
# NOTE: Should be listed _first_ in an || statement, so that it updates the timestamp without getting short-circuited
function has_updated {
  # Memoize AWS_LIST, which includes filenames and timestamps
  if [[ -z "${AWS_LIST+x}" ]]; then
    AWS_LIST="$(aws s3 ls s3://whist-protocol-dependencies)"
    export AWS_LIST
  fi
  TIMESTAMP_LINE=$(echo "$AWS_LIST" | grep " $1" | awk '{print $4, $1, $2}')
  if grep "$TIMESTAMP_LINE" "$CACHE_FILE" &>/dev/null; then
    return 1 # Return false since the lib hasn't updated
  else
    echo -n "---- "
    # Delete old timestamp line for that lib, if it exists
    NEW_LIBCACHE=$(sed "/^$1 /d" "$CACHE_FILE")
    echo "$NEW_LIBCACHE" > "$CACHE_FILE"
    # Append new timestamp line to libcache
    echo "$TIMESTAMP_LINE" >> "$CACHE_FILE"
    # Print found library
    echo "$TIMESTAMP_LINE" | awk '{print $1}'
    # Return true, since the lib has indeed updated
    return 0
  fi
}

###############################
# Download SDL2 libraries/headers
###############################

# Select SDL lib dir and SDL lib targz name based on OS and hardware architecture (macOS)
SDL_LIB_DIR="$DEST_DIR/lib/64/SDL2/$OS"
SDL_HEADERS_DIR="$DEST_DIR/include/SDL2"
if [[ "$POSITION_INDEPENDENT_CODE" = "ON" ]]; then
  SDL_LIB_SUFFIX="-pic"
else
  SDL_LIB_SUFFIX=""
fi

if [[ "$OS" =~ "Windows" ]]; then
  SDL_LIB="whist-windows-sdl2-static-lib${SDL_LIB_SUFFIX}.tar.gz"
elif [[ "$OS" == "Darwin" ]]; then
  if [[ "$MACOS_ARCH" == "arm64" ]]; then
    SDL_LIB="whist-macos-arm64-sdl2-static-lib${SDL_LIB_SUFFIX}.tar.gz"
  else
    SDL_LIB="whist-macos-x64-sdl2-static-lib${SDL_LIB_SUFFIX}.tar.gz"
  fi
elif [[ "$OS" == "Linux" ]]; then
  SDL_LIB="whist-linux-sdl2-static-lib${SDL_LIB_SUFFIX}.tar.gz"
fi

# Check if SDL_LIB has updated, and if so, create the dir and copy the libs/headers into the source dir
if has_updated "$SDL_LIB"; then
  rm -rf "$SDL_LIB_DIR"
  mkdir -p "$SDL_LIB_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$SDL_LIB" - | tar -xz -C "$SDL_LIB_DIR"
  if [[ -d "$SDL_LIB_DIR/include" ]]; then
    rm -rf "$SDL_HEADERS_DIR"
    mkdir -p "$SDL_HEADERS_DIR"
    mv "$SDL_LIB_DIR/include"/* "$SDL_HEADERS_DIR"
    rmdir "$SDL_LIB_DIR/include"
  fi
fi

###############################
# Download Sentry headers
###############################

# If the include/sentry directory doesn't exist, make it and fill it
# Or, if the lib has updated, refill the directory
LIB="whist-sentry-headers.tar.gz"
SENTRY_DIR="$DEST_DIR/include/sentry"
if has_updated "$LIB" || [[ ! -d "$SENTRY_DIR" ]]; then
  rm -rf "$SENTRY_DIR"
  mkdir -p "$SENTRY_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$LIB" - | tar -xz -C "$SENTRY_DIR"

  # Pull all SDL2 include files up a level and delete encapsulating folder
  mv "$SENTRY_DIR/include"/* "$SENTRY_DIR"
  rmdir "$SENTRY_DIR/include"
fi

###############################
# Download Sentry libraries
###############################

# Select Sentry lib dir and Sentry lib targz name based on OS and hardware architecture (macOS)
SENTRY_LIB_DIR="$DEST_DIR/lib/64/sentry/$OS"
if [[ "$OS" =~ "Windows" ]]; then
  SENTRY_LIB="whist-windows-sentry-shared-lib.tar.gz"
elif [[ "$OS" == "Darwin" ]]; then
  if [[ "$MACOS_ARCH" == "arm64" ]]; then
    SENTRY_LIB="whist-macos-arm64-sentry-shared-lib.tar.gz"
  else
    SENTRY_LIB="whist-macos-x64-sentry-shared-lib.tar.gz"
  fi
elif [[ "$OS" == "Linux" ]]; then
  SENTRY_LIB="whist-linux-sentry-shared-lib.tar.gz"
fi

# Check if SENTRY_LIB has updated, and if so, create the dir and copy the libs into the source dir
if has_updated "$SENTRY_LIB"; then
  rm -rf "$SENTRY_LIB_DIR"
  mkdir -p "$SENTRY_LIB_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$SENTRY_LIB" - | tar -xz -C "$SENTRY_LIB_DIR"
fi

###############################
# Download mimalloc headers
###############################

# If the include/mimalloc directory doesn't exist, make it and fill it
# Or, if the lib has updated, refill the directory
LIB="whist-mimalloc-headers.tar.gz"
MIMALLOC_DIR="$DEST_DIR/include/mimalloc"
if has_updated "$LIB" || [[ ! -d "$MIMALLOC_DIR" ]]; then
  rm -rf "$MIMALLOC_DIR"
  mkdir -p "$MIMALLOC_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$LIB" - | tar -xz -C "$MIMALLOC_DIR"

  # Pull all SDL2 include files up a level and delete encapsulating folder
  mv "$MIMALLOC_DIR/include"/* "$MIMALLOC_DIR"
  rmdir "$MIMALLOC_DIR/include"
fi

###############################
# Download mimalloc libraries
###############################

if [[ "$OS" == "Darwin" ]]; then
  # Select mimalloc lib dir and mimalloc lib targz name based on OS and hardware architecture (macOS)
  MIMALLOC_LIB_DIR="$DEST_DIR/lib/64/mimalloc/$OS"
  if [[ "$MACOS_ARCH" == "arm64" ]]; then
    MIMALLOC_LIB="whist-macos-arm64-mimalloc-static-lib.tar.gz"
  else
    MIMALLOC_LIB="whist-macos-x64-mimalloc-static-lib.tar.gz"
  fi

  # Check if MIMALLOC_LIB has updated, and if so, create the dir and copy the libs into the source dir
  if has_updated "$MIMALLOC_LIB"; then
    rm -rf "$MIMALLOC_LIB_DIR"
    mkdir -p "$MIMALLOC_LIB_DIR"
    aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$MIMALLOC_LIB" - | tar -xz -C "$MIMALLOC_LIB_DIR"
  fi
fi
###############################
# Download OpenSSL headers
###############################

# If the include/openssl directory doesn't exist, make it and fill it
# Or, if the lib has updated, refill the directory
LIB="whist-libcrypto-headers.tar.gz"
OPENSSL_DIR="$DEST_DIR/include/openssl"
if has_updated "$LIB" || [[ ! -d "$OPENSSL_DIR" ]]; then
  rm -rf "$OPENSSL_DIR"
  mkdir -p "$OPENSSL_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$LIB" - | tar -xz -C "$OPENSSL_DIR"

  # Pull all OpenSSL include files up a level and delete encapsulating folder
  mv "$OPENSSL_DIR/include"/* "$OPENSSL_DIR"
  rmdir "$OPENSSL_DIR/include"
fi

###############################
# Download OpenSSL libraries
###############################

# Select OpenSSL lib dir and OpenSSL lib targz name based on OS and hardware architecture (macOS)
OPENSSL_LIB_DIR="$DEST_DIR/lib/64/openssl/$OS"
if [[ "$OS" =~ "Windows" ]]; then
  OPENSSL_LIB="whist-windows-libcrypto-static-lib.tar.gz"
elif [[ "$OS" == "Darwin" ]]; then
  if [[ "$MACOS_ARCH" == "arm64" ]]; then
    OPENSSL_LIB="whist-macos-arm64-libcrypto-static-lib.tar.gz"
  else
    OPENSSL_LIB="whist-macos-x64-libcrypto-static-lib.tar.gz"
  fi
elif [[ "$OS" == "Linux" ]]; then
  OPENSSL_LIB="whist-linux-libcrypto-static-lib.tar.gz"
fi

# Check if OPENSSL_LIB has updated, and if so, create the dir and copy the libs into the source dir
if has_updated "$OPENSSL_LIB"; then
  rm -rf "$OPENSSL_LIB_DIR"
  mkdir -p "$OPENSSL_LIB_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$OPENSSL_LIB" - | tar -xz -C "$OPENSSL_LIB_DIR"
fi

###############################
# Download FFmpeg headers
###############################

# If the include/ffmpeg directory doesn't exist, make it and fill it
# Or, if the lib has updated, refill the directory
LIB="whist-ffmpeg-headers.tar.gz"
FFMPEG_DIR="$DEST_DIR/include/ffmpeg"
if has_updated "$LIB" || [[ ! -d "$FFMPEG_DIR" ]]; then
  rm -rf "$FFMPEG_DIR"
  mkdir -p "$FFMPEG_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$LIB" - | tar -xz -C "$FFMPEG_DIR"

  mv "$FFMPEG_DIR/include/"/* "$FFMPEG_DIR"
  rmdir "$FFMPEG_DIR/include"
fi

###############################
# Download FFmpeg libraries
###############################

# Select FFmpeg lib dir and FFmpeg lib targz name based on OS and hardware architecture (macOS)
FFMPEG_LIB_DIR="$DEST_DIR/lib/64/ffmpeg"
if [[ "$OS" =~ "Windows" ]]; then
  FFMPEG_LIB="whist-windows-ffmpeg-shared-lib.tar.gz"
elif [[ "$OS" == "Darwin" ]]; then
  if [[ "$MACOS_ARCH" == "arm64" ]]; then
    FFMPEG_LIB="whist-macos-arm64-ffmpeg-shared-lib.tar.gz"
  else
    FFMPEG_LIB="whist-macos-x64-ffmpeg-shared-lib.tar.gz"
  fi
elif [[ "$OS" == "Linux" ]]; then
  FFMPEG_LIB="whist-linux-ffmpeg-shared-lib.tar.gz"
fi

# Check if FFMPEG_LIB has updated, and if so, create the dir and copy the libs into the source dir
if has_updated "$FFMPEG_LIB"; then
  rm -rf "$FFMPEG_LIB_DIR"
  mkdir -p "$FFMPEG_LIB_DIR"
  aws s3 cp --only-show-errors "s3://whist-protocol-dependencies/$FFMPEG_LIB" - | tar -xz -C "$FFMPEG_LIB_DIR"
fi

###############################
echo "-- Downloading Whist Protocol binaries from AWS S3 - Completed"
