#!/bin/sh

<<<<<<< HEAD
# This script starts the Fractal protocol server, and assumes that it is
# being run within a Fractal Docker container.

# Set/Retrieve Container parameters

# TODO: symlink the appropriate folders from the downloaded config, if it exists
#       perhaps better is to copy the contents to the appropriate locations in the
#       host service from the get-go, instead of symlinking ahead of tim
#       early symlinks to things that eventually won't exist may lead to broken links
# - actually, cannot copy-paste directly because then can't upload back to S3 from the host
# - perhaps create the
# all configs are at /home/fractal/...
# programs that have a /home/fractal/.nv folder for cache are marked with (nv)
# some programs might have stuff in the .local/share folder ?
# it is also possible that each app needs the config of its dependencies also installed
# brave: .config/BraveSoftware/BraveBrowser
# chrome: .config/google-chrome
# firefox: .mozilla/firefox
# sidekick: .config/sidekick
# ____
# blender: .config/blender (nv)
# blockbench: .config/Blockbench
# darktable: .config/darktable (nv)
# figma: .config/Figma
# gimp: .config/GIMP
# godot: .config/godot (nv)
# inkscape: .config/inkscape
# kdenlive: .config/kdenlive-layoutsrc, .config/kdenliverc (nv)
# krita: .config/kritadisplayrc, .config/kritarc
# lightworks: Lightworks (nv)
# shotcut: .config/Meltytech/Shotcut.conf (nv)
# texturelab: .config/texturelab (nv)
# ____
# discord: .config/discord
# freecad: .config/FreeCAD
# notion: .config/Notion
# rstudio: .rstudio-desktop, .config/rstudio (nv)
# slack: .config/Slack (nv)

=======
>>>>>>> bc868cbfd... after pinpointing and having solved the issue with tarred configs, revert to tarring configs and using aws s3 cp instead of aws s3 sync once again
CONTAINER_ID=$(basename $(cat /proc/1/cpuset))
FRACTAL_MAPPINGS_DIR=/fractal/containerResourceMappings
IDENTIFIER_FILENAME=hostPort_for_my_32262_tcp
PRIVATE_KEY_FILENAME=/usr/share/fractal/private/aes_key
WEBSERVER_URL_FILENAME=/usr/share/fractal/private/webserver_url
SENTRY_ENV_FILENAME=/usr/share/fractal/private/sentry_env

# Define a string-format identifier for this container
IDENTIFIER=$(cat $FRACTAL_MAPPINGS_DIR/$IDENTIFIER_FILENAME)

# Create list of command-line arguments to pass to the Fractal protocol server
OPTIONS=""

# Send in AES private key, if set
if [ -f "$PRIVATE_KEY_FILENAME" ]; then
    export FRACTAL_AES_KEY=$(cat $PRIVATE_KEY_FILENAME)
    OPTIONS="$OPTIONS --private-key=$FRACTAL_AES_KEY"
fi

# Send in Fractal webserver url, if set
if [ -f "$WEBSERVER_URL_FILENAME" ]; then
    export WEBSERVER_URL=$(cat $WEBSERVER_URL_FILENAME)
fi
OPTIONS="$OPTIONS --webserver=$WEBSERVER_URL"

# Send in Sentry environment, if set
if [ -f "$SENTRY_ENV_FILENAME" ]; then
    export SENTRY_ENV=$(cat $SENTRY_ENV_FILENAME)
    OPTIONS="$OPTIONS --environment=$SENTRY_ENV"
fi

# Create a google_drive folder in the user's home
ln -sf /fractal/cloudStorage/$IDENTIFIER/google_drive /home/fractal/

# This tar file, if it exists, has been retrieved from S3 and must be extracted
tarFile=/fractal/userConfigs/$IDENTIFIER/fractal-app-config.tar.gz
if [ -f "$tarFile" ]; then
    tar -xzf $tarFile -C /fractal/userConfigs/$IDENTIFIER/
fi

# While perhaps counterintuitive, "source" is the path in the userConfigs directory
#   and "destination" is the original location of the config file/folder.
#   This is because when creating symlinks, the userConfig path is the source
#   and the original location is the destination
# Iterate through the possible configuration locations and copy
mkdir /home/fractal/.config/Slack # Slack doesn't create a configuration folder until after it has been launched for the first time
for row in $(cat app-config-map.json | jq -rc '.[]'); do
    SOURCE_CONFIG_SUBPATH=$(echo ${row} | jq -r '.source')
    SOURCE_CONFIG_PATH=/fractal/userConfigs/$IDENTIFIER/$SOURCE_CONFIG_SUBPATH
    DEST_CONFIG_PATH=$(echo ${row} | jq -r '.destination')

    # if config path does not exist, then continue
    if [ ! -f "$DEST_CONFIG_PATH" ] && [ ! -d "$DEST_CONFIG_PATH" ]; then
        continue
    fi

    # If no, then copy default configs to the synced app config folder
    if [ ! -f "$tarFile" ]; then
	cp -rT $DEST_CONFIG_PATH $SOURCE_CONFIG_PATH
    fi

    # Remove the original configs and symlink the new ones to the original locations
    rm -rf $DEST_CONFIG_PATH
    ln -sfnT $SOURCE_CONFIG_PATH $DEST_CONFIG_PATH
    chown -R fractal $SOURCE_CONFIG_PATH
done

# Delete broken symlinks from config
find /fractal/userConfigs/$IDENTIFIER/ -xtype l -delete

# To assist the tar tool's "exclude" option, create a dummy tar file if it does not already exist
if [ ! -f "$tarFile" ]; then
    touch $tarFile
fi

# Create a .configready file that forces the display to wait until configs are synced
#     We are also forced to wait until the display has started
touch /fractal/userConfigs/$IDENTIFIER/.configready
until [ ! -f /fractal/userConfigs/$IDENTIFIER/.configready ]
do
    sleep 0.1
done
sleep 1 # a sore attempt to wait for the display to start before launching FractalServer

# Send in identifier
OPTIONS="$OPTIONS --identifier=$IDENTIFIER"

/usr/share/fractal/FractalServer $OPTIONS

# Send logs to webserver
curl \
    --header "Content-Type: application/json" \
    --request POST \
    --data @- \
    $WEBSERVER_URL/logs \
    <<END
{
    "sender": "server",
    "identifier": "$IDENTIFIER",
    "secret_key": "$FRACTAL_AES_KEY",
    "logs": $(jq -Rs . </usr/share/fractal/log.txt)
}
END
