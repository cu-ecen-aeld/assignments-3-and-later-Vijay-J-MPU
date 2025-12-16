#! /bin/sh


if [ $# -ne 2 ]
then
	echo "Error"
        echo "Usage: $0 <filesdir> <searchstr>"
	exit 1
fi

filesdir=$1
searchstr=$2


if [ ! -d "$filesdir" ]
then
	echo "ERROR : $filesdir does not represent a directory"
	exit 1
fi

filescount=$(find "$filesdir" -type f | wc -l)

matchcount=$(grep -r "$searchstr" "$filesdir" | wc -l)

echo " The number of files are $filescount and the number of matching lines are $matchcount "

exit 0
