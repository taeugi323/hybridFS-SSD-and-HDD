/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
#define _XOPEN_SOURCE 500

// maintain bbfs state in here
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <pthread.h>
struct hb_state 
{
    FILE *logfile;
    char *dir_ssd;  // ~/Desktop/hybridFS/example/rootdir -> change it to hybridFS/ssd and hybridFS/hdd
    char *dir_hdd;  // ~/Desktop/hybridFS/ssd : just send to hybridFS/hdd when file size is larger than threshold
    char *dir_workspace;    // mountpoint location
    int mig_threshold;
};

#define HB_DATA ((struct hb_state *) fuse_get_context()->private_data)

struct mutex_node 
{
    char rel_path[PATH_MAX];
    ino_t inode;
    pthread_mutex_t mutex;
    pthread_mutex_t wrt;
    int read_count;
    struct mutex_node *link;
};

#endif
