This directory contains the definition of a custom kernel module used to expose the priviledged instruction WBINVD to user space. This implementation is from https://github.com/epfl-vlsc/Incll/tree/master/flush. The original authors are listed below:

Eddie Kohler
kohler@seas.harvard.edu

Yandong Mao
ydmao@csail.mit.edu

Robert Morris
rtm@csail.mit.edu

# Compilation

Simply run `make all` in this directory

# Loading the kernal module 

This requires sudo priviledges. You can run `make load` in this directory or run the following: `sudo insmod gf.ko` (install the module) and `sudo chmod 777 /dev/global_flush` (set the correct permissions on the device)