
all: build

build:
	ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk

buildV:
	ndk-build V=1 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk

push:
	adb push libs/armeabi-v7a/su /data/local/tmp/su

run:
	adb push libs/armeabi-v7a/su /data/local/tmp/su
	adb shell "su -c \"/data/local/tmp/su -d 2>&1\""

clean:
	rm -rf obj
	rm -rf libs
