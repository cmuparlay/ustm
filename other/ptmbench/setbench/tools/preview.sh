#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "USAGE: $(basename $0) FILE_TO_PREVIEW"
	exit 1
fi

scp -p $1 root@rift:/var/www/html/preview.obj
if [ "$?" -eq "0" ]; then
    echo "To preview, open http://localhost (or appropriate ssh tunneled address)"
    echo "    note: apache2 is running on *rift*, so must open tunneling there"
    echo "          (i.e., ssh -L 80:localhost:80 user@rift)"
else
	echo "An error has occurred..."
	exit 1
fi
