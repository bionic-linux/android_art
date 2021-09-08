#!/usr/bin/bash

set -e

if [ -t 1 ]; then
  # Color sequences if terminal is a tty.
  red='\033[0;31m'
  green='\033[0;32m'
  yellow='\033[0;33m'
  magenta='\033[0;35m'
  nc='\033[0m'
fi

function help {
	cat << EOF
Usage: ./convert-to-base64.sh \
          -d8 $ANDROID_HOME/build-tools/*/d8 \
          -android-jar $ANDROID_HOME/platforms/android-*/android.jar

This script automates regeneration of CLASS_BYTES and DEX_BYTES variables in Main.java
EOF

	exit 0
}

while [[ $# -gt 0 ]]; do
	key="$1"

	case $key in
		-d8)
			d8="$2"
			shift
			shift
			;;
		-android-jar)
			android_jar="$2"
			shift
			shift
			;;
		-h|--help)
			help
			;;
	esac
done

if [ -z $d8 ]; then
	echo -e "${red}No path to d8 executable is specified${nc}"
	echo
	help
fi

if [ -z $android_jar ]; then
	echo -e "${red}No path to android.jar specified${nc}"
	echo
	help
fi

if [ ! -f "src-optional/java/util/OptionalLong.java" ]; then
	echo -e "${red}src-optional/OptionalLong.java does not exist${nc}"
	exit 1
fi

echo -e "${green}Compiling OptionalLong.java...${nc}"
javac  -source 8 -target 8 src-optional/java/util/OptionalLong.java 1>/dev/null 2>/dev/null
$d8 --lib $android_jar --release --output . src-optional/java/util/*.class
echo -e "${green}Done\n${nc}"


echo CLASS_BYTES to be pasted in src/Main.java are below:
echo -e "${yellow}8<------------------------------------------------------------------------------${nc}"
cat src-optional/java/util/OptionalLong.java | base64 | sed "s/\(.*\)/\"\1\" \+/g"
echo -e "${yellow}8<------------------------------------------------------------------------------${nc}"
echo
echo

echo DEX_BYTES to be pasted in src/Main.java are below:
echo -e "${yellow}8<------------------------------------------------------------------------------${nc}"
cat classes.dex | base64 | sed "s/\(.*\)/\"\1\" \+/g"
echo -e "${yellow}8<------------------------------------------------------------------------------${nc}"
echo
echo

echo -e "${green}Cleaning up...${nc}"
rm -f src-optional/java/util/OptionalLong.class
rm -f classes.dex
echo -e "${green}Done${nc}"