#!/bin/bash
export GTK_IM_MODULE=ibus
export XMODIFIERS=@im=ibus
export QT_IM_MODULE=ibus

# Exit on subcommand errors
set -Eeuo pipefail

# Disable any triggers for switching keyboards on the mandelbox
# gsettings set org.freedesktop.ibus.general preload-engines "['xkb:us::eng', 'anthy', 'pinyin', 'hangul', 'Unikey']"
gsettings set org.freedesktop.ibus.general.hotkey triggers "[]"
gsettings set org.freedesktop.ibus.engine.hangul initial-input-mode 'hangul'
ibus-daemon -drx
