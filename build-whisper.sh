#!/bin/sh

set -e

usage()
{
    echo 'usage: build-whisper [-d|-r|-c]
    where:
        -d to create a debug build default)
        -r to create a release build
        -c to clean the build artifacts'
}

clean()
{
    # Remove all possible artifact directories.
    cd ${WEBRTC_SRC_DIR}/modules/third_party/whisper.cpp
    echo "rm -rf build in modules/third_party/whisper.cpp"
    rm -rf build
    cd ${WEBRTC_SRC_DIR}/modules/third_party/llama.cpp
    echo "rm -rf build in modules/third_party/llama.cpp "
    rm -rf build
    cd ${WEBRTC_SRC_DIR}/modules/third_party/espeak-ng
    echo "rm -rf build in modules/third_party/espeak-ng"
    rm -rf build
    cd ${WEBRTC_SRC_DIR}/modules/third_party/pcaudiolib
    echo "make clean in modules/third_party/pcaudiolib"
    make clean
}

WEBRTC_SRC_DIR="${PWD}/../src"

case "$(uname -s | tr '[:upper:]' '[:lower:]')" in
    linux)
        HOST_PLATFORM="linux"
        ;;
    msys*|mingw*)
        HOST_PLATFORM="windows"
        ;;
    darwin)
        HOST_PLATFORM="mac"
        ;;
    *)
        HOST_PLATFORM="unknown"
        ;;
esac

BUILD_TYPE=debug

while [ "$1" != "" ]; do
    case $1 in
        -d | --debug )
            BUILD_TYPE=debug
            ;;
        -r | --release )
            BUILD_TYPE=release
            ;;
        -c | --clean )
            clean
            exit
            ;;
        -h | --help )
            usage
            exit
            ;;
        * )
            usage
            exit 1
    esac
    shift
done

    echo "HOST_PLATFORM is ${HOST_PLATFORM}, BUILD_TYPE is ${BUILD_TYPE}, WEBRTC_SRC_DIR is ${WEBRTC_SRC_DIR}"

    cd ${WEBRTC_SRC_DIR}/modules/third_party
    if [ ! -f ${WEBRTC_SRC_DIR}/modules/third_party/whisper.cpp/CMakeLists.txt ]; then
    echo "cloning whisper.cpp to ${PWD}"
    git clone https://github.com/ggerganov/whisper.cpp
    fi

    if [ ! -f ${WEBRTC_SRC_DIR}/modules/third_party/llama.cpp/CMakeLists.txt ]; then
    echo "cloning llama.cpp to ${PWD}"
    git clone https://github.com/ggerganov/llama.cpp
    fi

    if [ ! -f ${WEBRTC_SRC_DIR}/modules/third_party/espeak-ng/CMakeLists.txt ]; then
    echo "cloning espeak-ng to ${PWD}"
    git clone https://github.com/espeak-ng/espeak-ng
    fi

    if [ ! -f ${WEBRTC_SRC_DIR}/modules/third_party/pcaudiolib/Makefile.am ]; then
    echo "cloning pcaudiolib to ${PWD}"
    git clone https://github.com/espeak-ng/pcaudiolib
    fi

    cd ${WEBRTC_SRC_DIR}/modules/third_party/whisper.cpp
    
    if [ "${HOST_PLATFORM}" = "linux" ]
    then
        sed -i 's/Wunreachable-code-break/Wunreachable-code/g' ggml/src/CMakeLists.txt 
        sed -i 's/Wunreachable-code-return/Wunreachable-code/g' ggml/src/CMakeLists.txt
    fi

    echo "building whisper.cpp"
    cmake -B build

    if [ "${BUILD_TYPE}" = "release" ]
        cmake --build build --config Release  
    then
        cmake --build build --config Debug  
    fi

    if [ "${HOST_PLATFORM}" = "linux" ]
    then
        echo "installing whisper.cpp"
        cd build; sudo make install; cd ..
    fi

    cd ${WEBRTC_SRC_DIR}/modules/third_party/llama.cpp

    if [ "${HOST_PLATFORM}" = "linux" ]
    then
        sed -i 's/Wunreachable-code-break/Wunreachable-code/g' ggml/src/CMakeLists.txt 
        sed -i 's/Wunreachable-code-return/Wunreachable-code/g' ggml/src/CMakeLists.txt
    fi

    echo "building llama.cpp"
    cmake -B build

    if [ "${BUILD_TYPE}" = "release" ]
        cmake --build build --config Release  
    then
        cmake --build build --config Debug  
    fi

    if [ "${HOST_PLATFORM}" = "linux" ]
    then
        echo "installing llama.cpp"
        cd build; sudo make install; cd ..
    fi

    cd ${WEBRTC_SRC_DIR}/modules/third_party/espeak-ng

    echo "building espeak-ng"

    cmake -B build

    if [ "${BUILD_TYPE}" = "release" ]
        cmake --build build --config Release  
    then
        cmake --build build --config Debug  
    fi

    if [ "${HOST_PLATFORM}" = "linux" ]
    then
        echo "installing espeak-ng"
        sudo make install
    fi

    echo "building pcaudio"

    cd ${WEBRTC_SRC_DIR}/modules/third_party/pcaudiolib

    if [ "${HOST_PLATFORM}" = "mac" ]
    then
        echo "building pcaudiolib"
        ./autogen.sh
        ./configure
        make
        ./libtool --mode=install cp src/libpcaudio.la  ${WEBRTC_SRC_DIR}/modules/third_party/pcaudiolib/src/libpcaudio.dylib
    fi

    if [ ! -f ${WEBRTC_SRC_DIR}/cert.pem ]; then
    echo "creating certtificate in ${PWD}"
        openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 3650 -nodes -subj "/C=XX/ST=StateName/L=CityName/O=CompanyName/OU=CompanySectionName/CN=CommonNameOrHostname"
    fi
