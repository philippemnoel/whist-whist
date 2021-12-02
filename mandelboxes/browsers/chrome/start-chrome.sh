#!/bin/bash

# Enable Sentry bash error handler, this will catch errors if `set -e` is set in a Bash script
eval "$(sentry-cli bash-hook)"

# Exit on subcommand errors
set -Eeuo pipefail

# Under certain (bad) circumstances, SingletonLock might be saved into the user's config. This is an issue,
# as this prevents Chrome from running forevermore! Hence, we should remove this file when we launch the
# browser for the first time each session. You can think of this as effectively moving the locking mechansim
# out of the backed-up Chrome config folder and into a location that will not persist when the instance dies.
GOOGLE_CHROME_SINGLETON_LOCK=/home/fractal/.config/google-chrome/SingletonLock
WHIST_CHROME_SINGLETON_LOCK=/home/fractal/.config/WhistChromeSingletonLock

if [[ ! -f $WHIST_CHROME_SINGLETON_LOCK ]]; then
    touch $WHIST_CHROME_SINGLETON_LOCK
    rm -f $GOOGLE_CHROME_SINGLETON_LOCK
fi

features="VaapiVideoDecoder,Vulkan,CanvasOopRasterization,OverlayScrollbar"
flags=(
  --use-gl=desktop
  --flag-switches-begin
  --enable-gpu-rasterization
  --enable-zero-copy
  --double-buffer-compositing
  --disable-smooth-scrolling
  --disable-font-subpixel-positioning
  --force-color-profile=display-p3-d65
  --disable-gpu-process-crash-limit
  --disable-notifications #This is tech debt, remove when notification redirection is implemented
  --no-default-browser-check
)

if [[ $DARK_MODE == true ]]; then
    features="$features,WebUIDarkMode"
    flags+=(--force-dark-mode)
fi

if [[ $RESTORE_LAST_SESSION == true ]]; then
    flags+=(--restore-last-session)
fi

flags+=(--enable-features=$features)
flags+=(--flag-switches-end)

# Passing the initial url from json transport as a parameter to the google-chrome command. If the url is not
# empty, Chrome will open the url as an additional tab at start time. The other tabs will be restored depending
# on the user settings.
flags+=($INITIAL_URL)

# Start Chrome
# flag-switches{begin,end} are no-ops but it's nice convention to use them to surround chrome://flags features
exec google-chrome "${flags[@]}"
