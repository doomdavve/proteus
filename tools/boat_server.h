#ifndef BOAT_SERVER_H
#define BOAT_SERVER_H

// The room authority's command line (tools/boat_server.c). The boat_server executable is only
// this; wake_velocity runs it as `wake_velocity --serve ...`, which is how the game hosts a room
// as a child process of itself. ARGV[0] is ignored.
int boat_server_main(int argc, char **argv);
#endif
