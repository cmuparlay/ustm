#!/bin/bash

if [ "$#" -ne "2" ]; then
	echo "USAGE: $0 <TITLE> <SUBTITLE>"
    exit 1
fi

title=$1
subtitle=$2

ftemplate="template_exp.txt"

flog="_log_gen.txt"
rm $flog 2>/dev/null

navrows="\n    <a href='index.htm'>home<\/a>"

sections=$(ls *.png | rev | cut -d"-" -f3- | rev | sort | uniq)
for sec in $sections ; do

    # get uniq/sorted rows and adjust the sort by shortest length first
    rows=$(ls ${sec}*.png | rev | cut -d"-" -f1 | cut -d"." -f2 | rev | sort | uniq | tr "\n" " ")
    rows=$(for x in $rows ; do echo -e "${#x}\t$x" ; done | sort -n | cut -f2 | tr "\n" " ")

    # get uniq/sorted columns and adjust the sort by shortest length first
    cols=$(ls ${sec}*.png | rev | cut -d"-" -f2 | rev | sort | uniq | tr "\n" " ")
    cols=$(for x in $cols ; do echo -e "${#x}\t$x" ; done | sort -n | cut -f2 | tr "\n" " ")

    echo "sec=\"$sec\" rows=\"$rows\" cols=\"$cols\"" | tee -a $flog

    ## create section
    f=$sec.htm
    f_escaped=${f//\//\\/}
    cp $ftemplate $f

    sec_pretty=$(echo $sec | tr "-" " ")
    navrows=$"${navrows}\n    <a href='${f_escaped}'>${sec_pretty}<\/a>"
    echo "adding \"\n    <a href='${f_escaped}'>${sec_pretty}<\/a>\" to {navrows}" >> $flog

    ## build table header
    thcols=""
    for col in $cols ; do
        thcols="${thcols}<th>$col<\/th>"
    done
    cmd="sed -i \"s/{thcols}/${thcols}/g\" $f >> $flog"
    echo "$cmd" >> $flog
    eval "$cmd"
    echo "    sed replaced {thcols} in $f" | tee -a $flog

    ## build table rows
    trrows=""
    preloadtags=""
    for row in $rows ; do
        trrows=$"${trrows}\n<tr>"
        trrows=$"${trrows}\n    <td>$row<\/td>"

        for col in $cols ; do
            prefix="${sec}-${col}-${row}"
            prefix_escaped=${prefix//\//\\/}

            if [ -e "${prefix}.gif" ]; then
				hover_html="onmouseover=\\\"this.src='${prefix_escaped}.gif'\\\" onmouseout=\\\"this.src='${prefix_escaped}.png'\\\""
				preloadtags="${preloadtags}<link rel=\\\"preload\\\" as=\\\"image\\\" href=\\\"${prefix_escaped}.gif\\\">"
            elif [ -e "${prefix}.mp4" ]; then
				hover_html="onmouseover=\\\"this.src='${prefix_escaped}.mp4'\\\" onmouseout=\\\"this.src='${prefix_escaped}.png'\\\""
				preloadtags="${preloadtags}<link rel=\\\"preload\\\" as=\\\"image\\\" href=\\\"${prefix_escaped}.mp4\\\">"
            else
                hover_html=""
            fi

            fdatalog="datalog-$(echo ${prefix} | cut -d'-' -f2-)"
            if [ -e "${prefix}.txt" ]; then
				## create htm file for this raw log file
                link_html="${prefix}.htm"
				cp template_rawlog.txt ${prefix}.htm
				cat ${prefix}.txt >> ${prefix}.htm
				echo "</pre></body></html>" >> ${prefix}.htm
            elif [ -e "${fdatalog}.txt" ]; then
				## create htm file for this raw log file
                link_html="${fdatalog}.htm"
				cp template_rawlog.txt ${fdatalog}.htm
				cat ${fdatalog}.txt >> ${fdatalog}.htm
				echo "</pre></body></html>" >> ${fdatalog}.htm
            else
                link_html="#"
            fi

            trrows=$"${trrows}\n    <td><a href='${link_html}'><img src='${prefix_escaped}.png' border='0' ${hover_html}><\/a><\/td>"
        done
        trrows=$"${trrows}\n<\/tr>"
    done
    cmd="sed -i \"s/{trrows}/${trrows}/g\" $f >> $flog"
    echo "$cmd" >> $flog
    eval "$cmd"
    echo "    sed replaced {trrows} in $f" | tee -a $flog

    cmd="sed -i \"s/{preloadtags}/${preloadtags}/g\" $f >> $flog"
    echo "$cmd" >> $flog
    eval "$cmd"
    echo "    sed replaced {preloadtags} in $f" | tee -a $flog
done

## prepare index.htm

echo "sec=\"index\"" | tee -a $flog

cp template_index.txt index.htm >> $flog 2>&1

cmd="sed -i \"s/{title}/${title}/g\" index.htm >> $flog"
echo "$cmd" >> $flog
eval "$cmd"
echo "    sed replaced {title} in index.htm" | tee -a $flog

cmd="sed -i \"s/{subtitle}/${subtitle}/g\" index.htm >> $flog"
echo "$cmd" >> $flog
eval "$cmd"
echo "    sed replaced {subtitle} in index.htm" | tee -a $flog

## add navigation bar to all pages

echo "sec=\"navigation\"" | tee -a $flog
echo "navrows=\"${navrows}\"" >> $flog
for f in *.htm ; do
	cmd="sed -i \"s/{navrows}/${navrows}/g\" $f >> $flog"
    echo "$cmd" >> $flog
    eval "$cmd"
    echo "    sed replaced {navrows} in $f" | tee -a $flog
done
