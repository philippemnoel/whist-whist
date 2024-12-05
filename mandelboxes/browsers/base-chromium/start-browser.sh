#!/bin/bash

# Enable Sentry bash error handler, this will catch errors if `set -e` is set in a Bash script
# This is called via `./run-as-whist-user.sh`, which passes sentry environment in.
case "$SENTRY_ENVIRONMENT" in
  dev|staging|prod)
    export SENTRY_ENVIRONMENT=${SENTRY_ENV}
    eval "$(sentry-cli bash-hook)"
    ;;
  *)
    echo "Sentry environment not set, skipping Sentry error handler"
    ;;
esac

# Exit on subcommand errors
set -Eeuo pipefail

# Start socketio server in the background
/opt/teleport/socketio-server &

BROWSER_APPLICATION=$(cat /home/whist/.browser_application)
BROWSER_APPLICATION=${BROWSER_APPLICATION:-chrome}
if [[ "$BROWSER_APPLICATION" == "brave" ]]; then
  USER_DATA_DIR="${1:-$HOME/.config/BraveSoftware/Brave-Browser}"
elif [[ "$BROWSER_APPLICATION" == "chrome" ]]; then
  USER_DATA_DIR="${1:-$HOME/.config/google-chrome}"
elif [[ "$BROWSER_APPLICATION" == "chromium" ]]; then
  USER_DATA_DIR="${1:-$HOME/.config/chromium}"
else
  echo "Browser name not set or invalid. Using the default option, Chrome."
  USER_DATA_DIR="${1:-$HOME/.config/google-chrome}"
fi

# Under certain (bad) circumstances, SingletonLock might be saved into the user's config. This is an issue,
# as this prevents Chrome from running forevermore! Hence, we should remove this file before we launch the
# browser for the first time each session.
BROWSER_SINGLETON_LOCK=$USER_DATA_DIR/SingletonLock
WHIST_CLEARED_SINGLETON_LOCK=/home/whist/.config/WhistClearedSingletonLock

if [[ ! -f $WHIST_CLEARED_SINGLETON_LOCK ]]; then
  touch $WHIST_CLEARED_SINGLETON_LOCK
  rm -f "$BROWSER_SINGLETON_LOCK"
fi

DEFAULT_PROFILE=$USER_DATA_DIR/Default
PREFERENCES=$DEFAULT_PROFILE/Preferences
PREFERENCES_UPDATE=$DEFAULT_PROFILE/Preferences.update

# Clear any cached service workers, which may break extension
# updating by propagating the old version.
rm -rf "$DEFAULT_PROFILE/Service Worker"

# Initialize empty preferences file if one doesn't exist
if [[ ! -f $PREFERENCES ]]; then
  mkdir -p "$DEFAULT_PROFILE"
fi

if [[ ! -s $PREFERENCES ]]; then
  echo {} > "$PREFERENCES"
fi

echo {} > "$PREFERENCES_UPDATE"

function add_preferences_jq() {
  jq -c "$1" < "$PREFERENCES_UPDATE" > "$PREFERENCES_UPDATE.new"
  mv "$PREFERENCES_UPDATE.new" "$PREFERENCES_UPDATE"
}

function commit_preferences_jq() {
  jq -cs '.[0] * .[1]' "$PREFERENCES" "$PREFERENCES_UPDATE" > "$PREFERENCES.new"
  rm "$PREFERENCES_UPDATE"
  mv "$PREFERENCES.new" "$PREFERENCES"
}

# Notes on Chromium flags:
#
# The following flags are currently unsupported on Linux, but desirable. Once they are
# supported, we should add support for them:
# "--enable-native-gpu-memory-buffers" --> https://bugs.chromium.org/p/chromium/issues/detail?id=1031269
# "--use-passthrough-command-decoder" --> https://bugs.chromium.org/p/chromium/issues/detail?id=976283
#
# When inspecting chrome://gpu, you'll see that Optimus is disabled. This is intentional, as it
# is not well supported on Linux, and is a feature intended to increase battery life by doing
# process switching, which is undesirable for Whist.
#
# Some of these flags are inspired from the following links:
# https://www.reddit.com/r/Stadia/comments/jwh0sl/greatly_reduce_inputlag_when_playing_in_chrome/
# https://nira.com/chrome-flags/
# https://bbs.archlinux.org/viewtopic.php?id=244031&p=25
#
# We intentionally disable/don't set the following flags, as they were either causing issues, or
# simply not improving performance:
# "--enable-zero-copy" --> causes visual glitches when using Nvidia Capture
# "--disable-frame-rate-limit" --> significantly degrades frame rate on YouTube
# "--disable-features=UseChromeOSDirectVideoDecoder" --> had no impact
# "--ignore-gpu-blocklist" --> had no impact
# "--disable-software-rasterizer" --> had no impact
# "--ignore-gpu-blocklist" --> had no impact
# "--disable-gpu-vsync" --> had no impact
# "--enable-drdc" --> had no impact
# "--enable-raw-draw" --> not well supported on Linux yet
# "--enable-quic" --> had no impact
# "--enable-lazy-image-loading" --> had no impact
#
features="VaapiVideoDecoder,VaapiVideoEncoder,Vulkan,CanvasOopRasterization,OverlayScrollbar,ParallelDownloading"
antifeatures="ChromeWhatsNewUI"
flags=(
  "--use-gl=desktop"
  # flag-switches{begin,end} are no-ops but it's nice convention to use them to surround chrome://flags features
  "--flag-switches-begin"
  "--enable-show-autofill-signatures"
  "--enable-vp9-kSVC-decode-acceleration"
  "--enable-accelerated-video-decode"
  "--enable-accelerated-mjpeg-decode"
  "--enable-accelerated-2d-canvas"
  "--enable-gpu-rasterization"
  "--enable-gpu-compositing"
  "--double-buffer-compositing"
  "--disable-font-subpixel-positioning"
  "--disable-gpu-process-crash-limit"
  "--no-default-browser-check"
  "--ozone-platform-hint=x11"
  "--password-store=basic" # This disables the kwalletd backend, which we don't support
)

features="$features,WebUIDarkMode"
flags+=("--force-dark-mode")

flags+=("--enable-features=$features")
flags+=("--disable-features=$antifeatures")
flags+=("--flag-switches-end")

# Start the server-side extension if the client requests it
if [[ "$LOAD_EXTENSION" == true ]]; then
  flags+=(  "--load-extension=/opt/teleport/chrome-extension")
fi

# Start the browser in Kiosk mode (full-screen). This flag is used when the client is a
# local Chromium browser integrating Whist to avoid duplicating the URL bar in the cloud tabs, and should
# not be set when the client is a fully-streamed browser rendered via SDL.
if [[ "$KIOSK_MODE" == true ]]; then
  flags+=("--kiosk")
fi

# TODO : Somehow using this argument is not working in E2E CI. Hence hardcording it to 0
# ENABLE_GPU_COMMAND_STREAMING=$1
ENABLE_GPU_COMMAND_STREAMING=0
echo "ENABLE_GPU_COMMAND_STREAMING is set to $ENABLE_GPU_COMMAND_STREAMING"

if [ "$ENABLE_GPU_COMMAND_STREAMING" == 1 ]; then
  flags+=("--enable-logging") # This is just required for debugging.
  # TODO : We are disabling sandbox temporarily for streaming gpu commands via a unix socket.
  # sandbox has to be re-enabled by following this
  # https://chromium.googlesource.com/chromium/src.git/+/HEAD/docs/linux/sandbox_ipc.md
  flags+=("--no-sandbox")
  flags+=("--enable-gpu-command-streaming")
fi

# OS-specific provisions
# If no CLIENT_OS is passed (i.e. we're testing locally), assume macOS
# and disable smooth scrolling.
if [[ "$CLIENT_OS" == "darwin" || "$CLIENT_OS" == "" ]]; then
  # Edit the browser Preferences Config file to use the default Mac fonts
  add_preferences_jq '.webkit.webprefs.fonts |= . + {"fixed": {"Zyyy": "Courier"}, "sansserif": {"Zyyy": "Helvetica"}, "serif": {"Zyyy": "Times"}, "standard": {"Zyyy": "Times"}}'
  # Disable smooth scrolling, which we handle via uinput instead
  flags+=("--disable-smooth-scrolling")
elif [[ "$CLIENT_OS" == "linux" ]]; then
  # Disable smooth scrolling, which we handle via uinput instead
  flags+=("--disable-smooth-scrolling")
else
  # Edit the browser Preferences Config file to use the default Windows/Ubuntu fonts
  add_preferences_jq '.webkit.webprefs.fonts |= . + {"fixed": {"Zyyy": "Consolas"}, "sansserif": {"Zyyy": "Arial"}, "serif": {"Zyyy": "Times New Roman"}, "standard": {"Zyyy": "Times New Roman"}}'
fi

commit_preferences_jq

# Load D-Bus configurations; necessary for launching the browser
# The -10 comes from the display ID
dbus_env_file="/home/whist/.dbus/session-bus/$(cat /etc/machine-id)-10"
# shellcheck source=/dev/null
. "$dbus_env_file"
export DBUS_SESSION_BUS_ADDRESS
echo "loaded d-bus address in start-browser.sh: $DBUS_SESSION_BUS_ADDRESS"

# Start lowercase-files script
# This allows chromium themes to work more consistently
# TODO: Re-enable this once we've found a way for this to enable browser themes
# without breaking extension importing
# /usr/bin/lowercase-chromium-files "google-chrome" &

case "$BROWSER_APPLICATION" in
  brave)
    BROWSER_EXE="brave-browser"
    ;;
  chrome)
    BROWSER_EXE="google-chrome"
    ;;
  chromium)
    BROWSER_EXE="chromium-browser"
    ;;
  *)
    echo "Browser name not set or invalid. Using the default option, Chrome."
    BROWSER_EXE="google-chrome"
    ;;
esac

# Start the browser with the KDE desktop environment
exec env XDG_CURRENT_DESKTOP=KDE XDG_SESSION_TYPE=x11 "$BROWSER_EXE" "${flags[@]}"
