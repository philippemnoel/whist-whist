#!/bin/bash

# Exit on errors and missing environment variables
set -Eeuo pipefail

function printhelp {
    echo "Usage: build [OPTION 1] [OPTION 2] ...\n"
    echo "Note: Make sure to run this script in a terminal on macOS or Linux Ubuntu."
    echo ""
    echo "  --version VERSION         set the version number of the client app"
    echo "                            must be greater than the current version"
    echo "                            in S3 bucket"
    echo ""
    echo "  --bucket BUCKET           set the S3 bucket to upload to (if -publish=true)"
    echo "                            options are:"
    echo "                              fractal-chromium-macos-dev      [macOS Fractalized Chrome Development Bucket]"
    echo "                              fractal-chromium-macos-staging  [macOS Fractalized Chrome Staging Bucket]"
    echo "                              fractal-chromium-macos-prod     [macOS Fractalized Chrome Production Bucket]"
    echo "                              fractal-chromium-ubuntu-dev     [Linux Ubuntu Fractalized Chrome Development Bucket]"
    echo "                              fractal-chromium-ubuntu-staging [Linux Ubuntu Fractalized Chrome Staging Bucket]"
    echo "                              fractal-chromium-ubuntu-prod    [Linux Ubuntu Fractalized Chrome Production Bucket]"
    echo ""
    echo "  --publish PUBLISH         set whether to publish to S3 and auto-update live apps"
    echo "                            defaults to false, options are true/false"
}

if [[ "$1" == "--help" ]]
then
    printhelp
else
    version=${version:-1.0.0}
    bucket=${bucket:-fractal-chromium-macos-dev}
    publish=${publish:-false}

    # Download binaries
    mkdir binaries
    cd binaries
    if [[ "$OSTYPE" == "linux-gnu" ]]; then
        # Linux Ubuntu
        wget https://fractal-client-application-deps.s3.us-east-1.amazonaws.com/awsping_linux
        sudo chmod +x awsping_linux
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        curl https://fractal-client-application-deps.s3.us-east-1.amazonaws.com/awsping_osx -o awsping_osx
        sudo chmod +x awsping_osx
    fi
    cd ..

    # get named params
    while [ $# -gt 0 ]; do
    if [[ $1 == *"--"* ]]; then
        param="${1/--/}"
        declare $param="$2"
    fi
    shift
    done

    python3 setVersion.gyp $bucket $version

    cd ../../protocol
    cmake . -DCMAKE_BUILD_TYPE=Release
    make FractalClient
    cd ../client-applications/desktop
    rm -rf protocol-build
    mkdir protocol-build
    cd protocol-build
    mkdir desktop
    cd ..
    cp ../../protocol/desktop/build64/Darwin/FractalClient protocol-build/desktop
    cp -R ../../protocol/desktop/build64/Darwin/loading protocol-build/desktop

    # Note: we no longer add the logo to the executable because the logo gets set
    # in the protocol directly via SDL

    if [[ "$OSTYPE" == "linux-gnu" ]]; then
        # Linux Ubuntu
        sudo chmod +x protocol-build/desktop/FractalClient
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        # Mac OSX
        # copy over the FFmpeg dylibs
        cp ../../protocol/lib/64/ffmpeg/Darwin/libavcodec.58.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libavdevice.58.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libavfilter.7.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libavformat.58.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libavutil.56.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libpostproc.55.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libswresample.3.dylib protocol-build/desktop
        cp ../../protocol/lib/64/ffmpeg/Darwin/libswscale.5.dylib protocol-build/desktop
        cp ../../protocol/desktop/build64/Darwin/libsentry.dylib protocol-build/desktop
        # codesign the FractalClient executable
        codesign -s "Fractal Computers, Inc." protocol-build/desktop/FractalClient
    fi

    # Initialize yarn first
    yarn -i

    # Increase Yarn network timeout, to avoid ESOCKETTIMEDOUT on weaker devices (like GitHub Action VMs)
    yarn config set network-timeout 300000

    if [[ "$publish" == "true" ]]
    then
        yarn package-ci
    else
        yarn package
    fi
fi
