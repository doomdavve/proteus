// Browser worker: descriptor validation or isolated objective transitions; no window or audio.
#include "boat_plugin_eval.h"
#include <emscripten.h>
EM_JS(void, expose_heap, (void), { Module.boatPluginHeap = HEAPU8; })
#include "boat_plugin_stage.h"

static unsigned char input[BOAT_PLUGIN_EVAL_REQUEST_MAX];
static unsigned char output[BOAT_PLUGIN_EVAL_REPLY_MAX];
unsigned char *boat_plugin_eval_input(void)
{
    expose_heap();
    return input;
}
unsigned char *boat_plugin_eval_output(void)
{
    expose_heap();
    return output;
}
unsigned boat_plugin_eval_run(unsigned size)
{
    return (unsigned)boat_plugin_eval_execute(input, size, output, sizeof(output));
}

int main(void) { return boat_plugin_stage_worker(); }
