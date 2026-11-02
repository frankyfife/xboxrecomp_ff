/*
 * input_host.h -- the host controller behind the replaced XAPI input.
 *
 * Adapted from doaxbv-re (https://github.com/NoRain211/doaxbv-re,
 * recomp-runtime/input_host_win32.h), GPL-3.0.
 */
#ifndef XBOXRECOMP_INPUT_HOST_H
#define XBOXRECOMP_INPUT_HOST_H

#include <stdbool.h>
#include "input_model.h"

/* Guest ordering of the analog button bytes. */
enum {
    HOST_ANALOG_A = 0, HOST_ANALOG_B, HOST_ANALOG_X, HOST_ANALOG_Y,
    HOST_ANALOG_BLACK, HOST_ANALOG_WHITE, HOST_ANALOG_LTRIG, HOST_ANALOG_RTRIG,
};

/* One sample of the host device bound to this port, as the Xbox gamepad the
 * title reads. Always succeeds: with nothing connected or pressed it is a
 * neutral pad. RECOMP_FAKE_INPUT and RECOMP_INPUT_SEQ presses are added on
 * port 0 only. Ports are 0-based here and 1-based in the UI and the config
 * file, where they are what is written on the console. */
bool recomp_input_host_sample_port(unsigned port, RecompInputGamepad *gamepad);

/* Port 0, for callers that only ever had one pad. */
bool recomp_input_host_sample(RecompInputGamepad *gamepad);

/* The sticks of the last port-0 sample the title was given, scripts
 * (RECOMP_INPUT_SEQ, RECOMP_FAKE_INPUT) included: lx, ly, rx, ry, -32768..
 * 32767. For a title's own diagnostics -- e.g. finding which guest values
 * follow the right stick -- without sampling the pad a second time (which
 * would start a script's clock or eat mouse motion). 0 before the first
 * sample. Safe from any thread; a torn read costs one stale axis. */
int  recomp_input_host_last_sticks(short out[4]);

/* A title's look at controller 1 before the title's own code reads it, and
 * the chance to change it: buttons (XINPUT_GAMEPAD bits), the eight analog
 * buttons and the sticks (lx, ly, rx, ry), scripts included. For a feature
 * that takes the pad over for a while -- TimeSplitters 2's free camera
 * reads it and hands the game a neutral pad. NULL removes it. */
typedef void (*RecompInputPort0Filter)(uint16_t *buttons, uint8_t analog[8], int16_t sticks[4]);
void recomp_input_host_set_port0_filter(RecompInputPort0Filter filter);

#endif
