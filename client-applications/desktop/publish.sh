#!/bin/bash

function printhelp {
    echo "Usage: build [OPTION 1] [OPTION 2] ...\n"
    echo "Note: Make sure to run this script in a terminal on Mac."
    echo "  --branch BRANCH                set the Github protocol branch that you"
    echo "                                  want the client app to run"
    echo "  --version VERSION              set the version number of the client app"
    echo "                                  must be greater than the current version"
    echo "                                  in S3 bucket"
    echo "  --bucket BUCKET                set the S3 bucket to upload to (if -publish=true)"
    echo "                                  options are:"
    echo "                                    fractal-windows-application-release [Windows bucket]"
    echo "                                    fractal-mac-application-release [Mac bucket]"
    echo "                                    fractal-linux-application-release [Linux bucket]"
    echo "                                    fractal-windows-application-testing [Internal use Windows testing bucket]"
    echo "                                    fractal-mac-application-testing [Internal use Mac testing bucket]"
    echo "                                    fractal-linux-application-testing [Internal use Linux testing bucket]"
    echo "  --publish PUBLISH              set whether to publish to S3 and auto-update live apps"
    echo "                                  defaults to false, options are true/false"
}

if [[ "$1" == "--help" ]]
then 
     printhelp 
else
     branch=${branch:-dev}
     version=${version:-1.0.0}
     bucket=${bucket:-fractal-mac-application-testing}
     publish=${publish:-false}

     # get named params
     while [ $# -gt 0 ]; do
     if [[ $1 == *"--"* ]]; then
          param="${1/--/}"
          declare $param="$2"
     fi
     shift
     done

     python3 setVersion.gyp $bucket $version
     
     ls
     cd ../../protocol
     cmake . -DCMAKE_BUILD_TYPE=Release
     make FractalClient
     cd ..
     rm -rf protocol-build 
     mkdir protocol-build 
     cd protocol-build
     mkdir desktop
     cd ..
     cp .protocol/desktop/build64/Darwin/FractalClient protocol-build/desktop
     cp -R .protocol/desktop/build64/Darwin/loading protocol-build/desktop
     if [[ "$OSTYPE" == "linux-gnu" ]]; then
     # Linux Ubuntu
     sudo chmod +x protocol-build/desktop/FractalClient
     elif [[ "$OSTYPE" == "darwin"* ]]; then
     # Mac OSX
     # add logo to the FractalClient executable
     sips -i build/icon.png # take an image and make the image its own icon
     DeRez -only icns build/icon.png > tmpicns.rsrc # extract the icon to its own resource file
     Rez -append tmpicns.rsrc -o protocol-build/desktop/FractalClient # append this resource to the file you want to icon-ize
     SetFile -a C protocol-build/desktop/FractalClient # use the resource to set the icon
     rm tmpicns.rsrc # clean up
     # copy over the Unison executable and FFmpeg dylibs
     cp .protocol/lib/64/ffmpeg/Darwin/libavcodec.58.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libavdevice.58.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libavfilter.7.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libavformat.58.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libavutil.56.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libpostproc.55.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libswresample.3.dylib protocol-build/desktop
     cp .protocol/lib/64/ffmpeg/Darwin/libswscale.5.dylib protocol-build/desktop
     cp .protocol/desktop/build64/Darwin/libsentry.dylib protocol-build/desktop
     # codesign the FractalClient executable
     codesign -s "Fractal Computers, Inc." protocol-build/desktop/FractalClient
     fi
     yarn -i

     if [[ "$publish" == "true" ]]
     then
     yarn package-ci 
     else
     yarn package
     fi
fi
