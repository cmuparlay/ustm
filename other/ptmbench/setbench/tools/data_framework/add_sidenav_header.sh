#!/bin/bash

if [ "$#" -ne "1" ]; then
    echo "USAGE: $(basename $0) HTML_ROOT"
    echo "HTML_ROOT should contain one or more folders, which each contain index.html."
    echo "The folder name will determine the name of the link and the href."
    exit 1
fi

path=$1

## construct new sidenav header

new_sidenav_header_html="<h3>categories<\\/h3>"
for f in $(find ${path} -wholename "./*/index.html" | sort) ; do
    name=$(basename $(dirname $f))
    href="..\\/$name\\/index.html"
    echo name=$name href=$href
    new_sidenav_header_html="${new_sidenav_header_html}<a href='${href}'>${name}<\\/a> "
done

echo "$new_sidenav_header_html" > ${path}/temp_sidenav_header.html
cat ${path}/temp_sidenav_header.html

## check if there's an _index.html template... if so, add the sidenav_header.

if [ -e "${path}/_index.html" ]; then
    cp "${path}/_index.html" "${path}/index.html"
    new_index_html="$(cat ${path}/temp_sidenav_header.html)"
    sed -i "s/<!--sidenav_header-->/${new_index_html}/g" "${path}/index.html"
    sed -i 's/\.\.\///g' "${path}/index.html"
fi

## replace sidenav-header token in every html file with sidenav header

for f in $(find ${path} -wholename "./*/*.html") ; do
    # echo $f
    name=$(basename $(dirname $f))
    new_html="${new_sidenav_header_html}<h3>${name}<\\/h3>"
    # echo "new_sidenav_header_html=${new_html}"
    echo "sed -i \"s/<!--sidenav_header-->/${new_html}/g\" $f"
    sed -i "s/<!--sidenav_header-->/${new_html}/g" $f
done
