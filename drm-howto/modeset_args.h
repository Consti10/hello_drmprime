//
// Created by consti10 on 16.02.22.
//

#ifndef HELLO_DRMPRIME_MODESET_ARGS_H
#define HELLO_DRMPRIME_MODESET_ARGS_H

#include "stdio.h"

//args pretty much stay the same
struct modeset_options{
    const char *card="/dev/dri/card0";
    bool drawFramesOnKeyboardClick;
};

static int modesetParseArguments(int argc, char **argv,modeset_options& options){
    //modeset_options options;
    options.card="/dev/dri/card0";
    int opt;
    while ((opt = getopt(argc, argv, "d:l")) != -1) {
        switch (opt) {
            case 'd':
                options.card=optarg;
                break;
            case 'l':
                options.drawFramesOnKeyboardClick=true;
                break;
            default: /* '?' */
            show_usage:
                fprintf(stderr,"Usage: -d device/card to open -l enable LED\n");
                return -1;
        }
    }
    fprintf(stderr,"using card '%s'\n",options.card);
    fprintf(stderr,"Enable led and redraw on keyboard click:%s\n",options.drawFramesOnKeyboardClick ? "Y":"N");
    return 0;
}

#endif //HELLO_DRMPRIME_MODESET_ARGS_H
