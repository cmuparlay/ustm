#!/bin/bash

if [ "$#" -ne "4" ]; then
    echo "USAGE: $(basename $0) <REMOTES_STRING> <ABSPATH_TO_COPY> <OUTPUT_RELPATH> <COMMANDS_STRING>"
    echo "Behaviour:"
    echo " 0. <REMOTES_STRING> defines a set of remotes, e.g., \"jax zyra cloud\""
    echo " 1. local:<ABSPATH_TO_COPY>/out_multi_system is destroyed"
    echo " 2. remotes:<ABSPATH_TO_COPY>_\$(remote) is destroyed"
    echo " 3. local:<ABSPATH_TO_COPY> is copied to remotes:<ABSPATH_TO_COPY>_\$(remote)"
    echo " 4. <COMMANDS_STRING> is run by GNU parallel at remotes:<ABSPATH_TO_COPY>_\$(remote)"
    echo " 5. results are copied from remotes:<ABSPATH_TO_COPY>_\$(remote)/<OUTPUT_RELPATH>"
    echo "    to local:<ABSPATH_TO_COPY>/out_multi_system/. (via rsync -r)"
    echo
    echo "Note: COMMANDS_STRING MUST be escaped appropriately for execution in environment:"
    echo '      parallel " ssh -tt {} \" bash -lc \\\" $COMMANDS_STRING \\\" \" " ::: remote'
    echo
    echo "Note: {1} will give \$(remote) in <OUTPUT_RELPATH> or <COMMANDS_STRING>"
    exit 1
fi

remotes=$1
path="$2"
output_relpath="$3"
remotecmds="$4"
# example remotecmds: "cd anim_lockxfers_acquires ; pwd ; ./run.sh"

numRemotes=`echo $remotes | wc -w`
colors=`echo "1 2 3 6 4 5 1 2 3 6 4 5" | cut -d" " -f1-$numRemotes` # enough colors for 12 remotes
out_multi_system_path="${path}/out_multi_system"
echo "run_multi_system.sh: remotes=\"$remotes\" path=\"$path\" output_relpath=\"$output_relpath\" remotecmds=\"$remotecmds\" numRemotes=\"$numRemotes\" colors=\"$colors\" out_multi_system_path=\"$out_multi_system_path\""

echo run_multi_system.sh: removing local:$out_multi_system_path
rm -r $out_multi_system_path
mkdir $out_multi_system_path
parallel --line-buffer --link --tagstring '\033[30;3{2}m[{1}]' " ssh -tt {1} \" bash -lc \\\" echo run_multi_system.sh: removing {1}:${path}_{1} ; rm -rf ${path}_{1} \\\" \" ; echo run_multi_system.sh: copying local:${path} to {1}:${path}_{1} ; rsync -r ${path}/* {1}:${path}_{1} ; ssh -tt {1} \" bash -lc \\\" cd tools && git pull ; echo run_multi_system.sh: entering {1}:${path}_{1} ; cd ${path}_{1} ; echo run_multi_system.sh: running remote commands on {1} ; ${remotecmds} \\\" \" ; echo run_multi_system.sh: copying {1}:${path}_{1}/${output_relpath} to local:${out_multi_system_path}/. ; rsync -r {1}:${path}_{1}/${output_relpath} ${out_multi_system_path}/. " ::: $remotes ::: $colors | tee -a log_run_multi_system.txt
