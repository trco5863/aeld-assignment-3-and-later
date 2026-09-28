#!/bin/sh
filesdir=$1
searchstr=$2

if [ -z "$filesdir" ] || [ -z "$searchstr" ]; then
	echo "missing search parameter"
	exit 1
fi	

if [ ! -d "$filesdir" ]; then
	echo "can't find directory"
	exit 1
fi

x=$(find "$filesdir" -type f | wc -l)
y=$(grep -r "$searchstr" "$filesdir" | wc -l)

echo "The number of files are $x and the number of matching lines are $y"

exit 0
