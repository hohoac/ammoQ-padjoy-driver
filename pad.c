/*
 * Pad for Psemu Pro like Emulators
 * This is the plugin
 *
 * Written by Erich Kitzm�ller <ammoq@ammoq.com>
 * Based on padXwin by linuzappz <linuzappz@hotmail.com>
 *
 * Copyright 2002,2003 by Erich Kitzm�ller
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <errno.h>
#include "padjoy.h"

char *LibName = "ammoQ's padJoy Joy Device Driver";

const unsigned char version = 1; // PSEmu 1.x library
const unsigned char revision = VERSION;
const unsigned char build = BUILD;

// Prototypes
static void loadConfig();
static void *thread_check_joydevice(void *arg);
static void initPadtime();
static PadJoyEvent *EventCode2PadJoyEvent(EventCode p_e);

// Filenames for device files, e.g. "/dev/input/js0"
static char devicefilename[MAXDEVICES][FILENAME_MAX+1] = {"/dev/input/js0", ""};

// File desciptors for device files
static int devicefile[MAXDEVICES] = { -1, -1 };

// use PCSX return values?
static int pcsx_style = 0;

// Use Threading for joy device input?
static int use_threads = 1;
static pthread_t joy_thread;
static int die_thread_die = 0;

// Emulate Dualshock(TM) analog pad?
static int use_analog = 0;

// any joydevice open?
static int joydevice_open;

// initialisation already done?
static int init_done = 0;

// calibration data
int minzero[MAXAXES];
int maxzero[MAXAXES];

// axes status - so only changing status are reported
int axestatus[MAXDEVICES][MAXAXES];

// Assignment of PSX buttons to Events
static EventCode PadButtons[MAXDEVICES][MAXPSXBUTTONS] = {{
    BUTTON_EVENT(0,10),	// L2
    BUTTON_EVENT(0,11), // R2
    BUTTON_EVENT(0,6),  // L1
    BUTTON_EVENT(0,7),	// R1
    BUTTON_EVENT(0,4),	// Triangle
    BUTTON_EVENT(0,1),	// Circle
    BUTTON_EVENT(0,0),	// Cross
    BUTTON_EVENT(0,3),	// Square
    BUTTON_EVENT(0,9),  // Select
    BUTTON_EVENT(0,12), // Left Analog
    BUTTON_EVENT(0,13), // Right Analog
    BUTTON_EVENT(0,8), // Start
    AXISMINUS_EVENT(0,1),// Up
    AXISPLUS_EVENT(0,0),	// Right
    AXISPLUS_EVENT(0,1),	// Down
    AXISMINUS_EVENT(0,0),	// Left
    ANALOGAXIS_EVENT(0,2,0),	// Left Anlaog X
    ANALOGAXIS_EVENT(0,3,0),	// Left Analog Y
    ANALOGAXIS_EVENT(0,4,0),	// Right Analog X
    ANALOGAXIS_EVENT(0,5,0)		// Right Analog Y
},
{ NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT,NO_EVENT }};

static Display *Dsp;

static EventCode macroLaunch[MAXDEVICES][MAXMACROS];
static EventCode macroEvents[MAXDEVICES][MAXMACROS][MAXMACROLENGTH];
static long macroInterval[MAXDEVICES][MAXMACROS][MAXMACROLENGTH];
static int macroActive[MAXDEVICES];
static int macroIndex[MAXDEVICES];
static long macroNext[MAXDEVICES];

unsigned short PadStat[MAXDEVICES] = {0xffff, 0xffff};

int AnalogValue[MAXDEVICES][MAXPSXBUTTONS-4] = {{127,127,127,127}, {127,127,127,127}};

char *PSEgetLibName(void) {
    return LibName;
}

unsigned long PSEgetLibType(void) {
    return 8;  // PSE_LT_PAD
}

unsigned long PSEgetLibVersion(void) {
    return version<<16|revision<<8|build;
}

void init_macros() {
    int i,j;

    for (i=0; i<MAXDEVICES; i++) {
        for (j=0; j<MAXMACROS; j++) {
            macroLaunch[i][j]=NO_EVENT;
            macroEvents[i][j][0]=NO_EVENT;
            macroInterval[i][j][0]=0;
        }
        macroActive[i]=-1;
        macroIndex[i]=0;
        macroNext[i]=0;
    }
}

long PADinit(long flags) {
    int i,j;

    init_macros();
    initPadtime();
    for (i=0; i<MAXDEVICES; i++) {
       maxzero[i] = 250;
       minzero[i] = -250;

       for (j=0; j<MAXAXES; j++) {
         axestatus[i][j] = AXESTS_UNKNOWN;
       }
    }
    loadConfig();

    return 0;
}

long PADshutdown(void) {
    return 0;
}

long PADopen(unsigned long *Disp) {
    int i,j;
    int res;
    PadJoyEvent *pje;

    if (init_done) {
        fprintf(stderr, "padJoy: pad already initialised\n");
	return 0;
    }

    Dsp = (Display *)*Disp;

    joydevice_open = 0;
    for (i=0; i<MAXDEVICES; i++) {
        if (devicefilename[i][0]) {
            devicefile[i] = open(devicefilename[i], O_RDONLY);
            if (devicefile[i] == -1) {
                fprintf(stderr, "padJoy: could not open %s\n", devicefilename[i]);
            }
            else {
                joydevice_open = 1;
            }
        }
        else {
            devicefile [i] = -1;
        }
    }

    for (i=0; i<MAXDEVICES; i++) {
      for (j=0; j<MAXAXES; j++) {
        axestatus[i][j] = AXESTS_UNUSED;
      }
    }

    for (i=0; i<MAXDEVICES; i++) {
        for (j=0; j<MAXPSXBUTTONS; j++) {
            pje = EventCode2PadJoyEvent(PadButtons[i][j]);

            if (pje->event_type == EVENTTYPE_AXISPLUS || pje->event_type == EVENTTYPE_AXISMINUS) {
                axestatus[pje->pad][pje->no] = AXESTS_UNKNOWN;
            }
            else if (pje->event_type == EVENTTYPE_ANALOG && use_analog) {
                axestatus[pje->pad][pje->no] = AXESTS_ANALOG;
            }
        }
    }

    if (use_threads) {
          die_thread_die = 0;
          if (joydevice_open) {
              fprintf(stderr, "padJoy: trying to start a thread; if it hangs, you must disable multithreading\n");
              sleep(1);
              res = pthread_create(&joy_thread, NULL, thread_check_joydevice, (void *) NULL);

              if (res!=0) {
                  fprintf(stderr, "padJoy: could not start joy device thread, using polling instead\n");
                  use_threads = 0;
              }
          }
    }

    init_done = 1;

    return 0;
}

long PADclose(void) {
    int i;
    for (i=0; i<2; i++) {
        if (devicefile[i] > -1) {
            close (devicefile[i]);
        }
    }

    if (use_threads) {
        die_thread_die = 1;
        if (joydevice_open) {
            pthread_join(joy_thread, (void **) NULL);
        }
    }

    init_done=0;

    return 0;
}

long PADquery(void) {
    return 3; // both pads
}

static long firstsecond=0;

static void initPadtime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    firstsecond = tv.tv_sec;
}

// construct a time on our own
long getPadtime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec-firstsecond)*10000+tv.tv_usec/100;
}

// get pending events
static int getPendingEvents(int millisecondstowait, EventCode *events, int maxevents, int useGDK, int checkJoydevice, int checkXKeyboard, long *timing) {
    fd_set rfds;
    int retval;
    struct js_event je;
    int i;
    int md;
    int eventsread=0;
    XEvent xe;
    int cntopen;
    struct timeval tv;
    int oldstatus;

    if (checkJoydevice) {

        FD_ZERO(&rfds);
        md = -1;
        cntopen=0;
        for (i=0; i<MAXDEVICES; i++) {
            if (devicefile[i]>-1) {
                FD_SET(devicefile[i], &rfds);
                cntopen++;
            }
            if (devicefile[i]>md) md = devicefile[i];
        }
        tv.tv_sec = millisecondstowait/1000;
        tv.tv_usec = 1000*(millisecondstowait%1000);

        retval = select(md+1, &rfds, NULL, NULL, &tv);

        while (retval && eventsread<maxevents-2*checkXKeyboard) {
            for (i=0; i<MAXDEVICES; i++) {
                if (devicefile[i]>-1 && FD_ISSET(devicefile[i], &rfds)) {
                    read (devicefile[i], &je, 8);

                    if (je.type == JS_EVENT_AXIS && je.number<MAXAXES) {
                        if (axestatus[i][je.number] == AXESTS_ANALOG) {
                            /* this axe should be reported analog */
                            events[eventsread++] = ANALOGAXIS_EVENT(i,je.number, (je.value+32768)>>8);
                            if (timing) {
                                (*timing)=getPadtime();
                                timing++;
                            }
                            if (eventsread==maxevents) return eventsread;

                        }
                        else if (je.value > maxzero[i]) {
                            if (axestatus[i][je.number] != AXESTS_PLUS &&
                                axestatus[i][je.number] != AXESTS_UNUSED) {

                                oldstatus = axestatus[i][je.number];

                                axestatus[i][je.number] = AXESTS_PLUS;

                                events[eventsread++] = AXISPLUS_EVENT(i,je.number);
                                if (timing) {
                                    (*timing)=getPadtime();
                                    timing++;
                                }
                                if (eventsread==maxevents) return eventsread;

                                if (oldstatus == AXESTS_MINUS) {
                                    events[eventsread++] = RELEASE_EVENT+AXISMINUS_EVENT(i,je.number);
                                    if (timing) {
                                        (*timing)=getPadtime();
                                        timing++;
                                    }
                                    if (eventsread==maxevents) return eventsread;
                                }

                            }
                        }
                        else if (je.value < minzero[i]) {
                            if (axestatus[i][je.number] != AXESTS_MINUS &&
                                axestatus[i][je.number] != AXESTS_UNUSED) {

                                oldstatus = axestatus[i][je.number];

                                axestatus[i][je.number] = AXESTS_MINUS;

                                events[eventsread++] = AXISMINUS_EVENT(i,je.number);
                                if (timing) {
                                    (*timing)=getPadtime();
                                    timing++;
                                }
                                if (eventsread==maxevents) return eventsread;

                                if (oldstatus == AXESTS_PLUS) {
                                    events[eventsread++] = RELEASE_EVENT+AXISPLUS_EVENT(i,je.number);
                                    if (timing) {
                                        (*timing)=getPadtime();
                                        timing++;
                                    }
                                    if (eventsread==maxevents) return eventsread;
                                }
                            }
                        }
                        else {
                            if (axestatus[i][je.number] != AXESTS_CENTER &&
                                axestatus[i][je.number] != AXESTS_UNUSED) {

                                oldstatus = axestatus[i][je.number];

                                axestatus[i][je.number] = AXESTS_CENTER;

                                if (oldstatus == AXESTS_PLUS) {
                                    events[eventsread++] = RELEASE_EVENT+AXISPLUS_EVENT(i,je.number);
                                    if (timing) {
                                        (*timing)=getPadtime();
                                        timing++;
                                    }
                                    if (eventsread==maxevents) return eventsread;
                                }
                                else if (oldstatus == AXESTS_MINUS) {
                                    events[eventsread++] = RELEASE_EVENT+AXISMINUS_EVENT(i,je.number);
                                    if (timing) {
                                        (*timing)=getPadtime();
                                        timing++;
                                    }
                                    if (eventsread==maxevents) return eventsread;
                                }
                            }
                        }
                    }
                    else if (je.type == JS_EVENT_BUTTON && je.number<MAXBUTTONS) {
                        events[eventsread++] = (je.value?0:RELEASE_EVENT) + BUTTON_EVENT(i,je.number);
                        if (timing) {
                            (*timing)=getPadtime();
                            timing++;
                        }
                        if (eventsread==maxevents) return eventsread;
                    }
                }
            }
            tv.tv_sec = 0;
            tv.tv_usec = 0;

            retval = select(md+1, &rfds, NULL, NULL, &tv);
        }

    }

    if (checkXKeyboard) {
        if (useGDK) {
          // not used anymore
        }
        else {
            while ((i=XPending(Dsp))) {
                while (i--) {
                    XNextEvent(Dsp, &xe);
                    switch (xe.type) {
                        case KeyPress:
                            events[eventsread++] = KEY_EVENT(XLookupKeysym((XKeyEvent *)&xe, 0));
                            if (timing) {
                                (*timing)=getPadtime();
                                timing++;
                            }
                            if (eventsread==maxevents) return eventsread;
                            break;
                        case KeyRelease:
                            events[eventsread++] = RELEASE_EVENT+KEY_EVENT(XLookupKeysym((XKeyEvent *)&xe, 0));
                            if (timing) {
                                (*timing)=getPadtime();
                                timing++;
                            }
                            if (eventsread==maxevents) return eventsread;
                            break;
                        case FocusIn:
                            XAutoRepeatOff(Dsp);
                            break;
                        case FocusOut:
                            XAutoRepeatOn(Dsp);
                            break;
                    }
                }
            }
        }
    }

    return eventsread;
}


// Key Event not used...
static EventCode keyLeftOver = NO_EVENT;

static void CheckPads(int checkJoydevice, int checkXKeyboard, int blocking) {
    EventCode events[MAXCNT];
    int cnt;
    int release;
    EventCode e;
    int i,j,k,l;
    int notfound;
    long now;
    int v;

    if (checkJoydevice || checkXKeyboard) {
        cnt = getPendingEvents(blocking<<8, events, MAXCNT, 0, checkJoydevice, checkXKeyboard, NULL);
    }
    else {
        cnt = 0;
    }

    // more events come from the macros
    // only process makros when blocking is not set, since
    // this cannot be done by the joy device thread

    if (!blocking) {
        now= -1;
        for (j=0; j<MAXDEVICES; j++) {
            if (macroActive[j]>-1) {
                if (now<0) {
                    now=getPadtime();
                }

                while (now>=macroNext[j] && cnt<MAXCNT && macroActive[j]>-1) {
                    events[cnt++]=macroEvents[j][macroActive[j]][macroIndex[j]];
                    macroIndex[j]++;
                    if (macroIndex[j] == MAXMACROLENGTH || macroEvents[j][macroActive[j]][macroIndex[j]] == NO_EVENT) {
                        macroActive[j]= -1;
                    }
                    else {
                        macroNext[j]+=macroInterval[j][macroActive[j]][macroIndex[j]];
                    }
                }
            }
        }
    }

    for (i=0; i<cnt; i++) {
        e = events[i];
        if (e>=RELEASE_EVENT) {
          release = 1;
          e -= RELEASE_EVENT;
        }
        else {
            release = 0;
        }

        notfound = 1;
        if (e>=FIRST_ANALOG_EVENT) {
            v = e & 0xff;
            e -= v;
            for (j=0; j<MAXDEVICES && notfound; j++) {
                for (k=16; k<MAXPSXBUTTONS && notfound; k++) {
                    if (PadButtons[j][k] == e) {
                       AnalogValue[j][k-16] = v;
                       notfound = 0;
                    }
                }
            }
        }
        else
        {
            for (j=0; j<MAXDEVICES && notfound; j++) {
                for (k=0; k<MAXPSXBUTTONS && notfound; k++) {
                    if (PadButtons[j][k] == e) {
                        notfound = 0;
                        if (release) {
                            PadStat[j]|=(1<<k);
                        }
                        else {
                            PadStat[j]&=~(1<<k);
                        }
                    }
                }
                for (k=0; k<MAXMACROS && notfound; k++) {
                    if (macroLaunch[j][k] == e) {
                        notfound=0;
                        if (release) {
                            // release all buttons pressed by the macro
                            for (l=macroIndex[j]; l<MAXMACROLENGTH && macroEvents[j][macroActive[j]][l] != NO_EVENT; l++) {
                                if (macroEvents[j][macroActive[j]][l] >= RELEASE_EVENT) {
                                    if (cnt<MAXCNT) {
                                        events[cnt++]=macroEvents[j][macroActive[j]][l];
                                    }
                                }

                            }

                            macroActive[j]= -1; // stop Makro from running
                        }
                        else {
                            macroActive[j]=k;
                            macroIndex[j]=0;
                            macroNext[j]=getPadtime();
                        }
                    }
                }
            }
        }
        if (notfound && e<FIRST_JOY_EVENT) {
             keyLeftOver = e;
        }
    }
}


long PADreadPort1(PadDataS *pad) {
    if (!use_threads) {
        CheckPads(joydevice_open, pcsx_style, 0);
    }
    else if (pcsx_style) {
        CheckPads(0, 1, 0);
    }
    else {
        CheckPads(0, 0, 0);
    }

    pad->buttonStatus = PadStat[0];
    if (use_analog) {
      pad->controllerType = 7; // analog Pad
      pad->leftJoyX = AnalogValue[0][0];
      pad->leftJoyY = AnalogValue[0][1];
      pad->rightJoyX = AnalogValue[0][2];
      pad->rightJoyY = AnalogValue[0][3];
    }
    else {
      pad->controllerType = 4; // standard
    }

    if (pcsx_style) { // ePSXe different from pcsx, swap bytes
       pad->buttonStatus = (pad->buttonStatus>>8)|(pad->buttonStatus<<8);
    }

    return 0;
}

long PADreadPort2(PadDataS *pad) {
    if (!use_threads) {
        CheckPads(joydevice_open, pcsx_style, 0);
    }
    else if (pcsx_style) {
        CheckPads(0, 1, 0);
    }
    else {
        CheckPads(0, 0, 0);
    }

    pad->buttonStatus = PadStat[1];
    if (use_analog) {
      pad->controllerType = 7; // analog Pad
      pad->leftJoyX = AnalogValue[1][0];
      pad->leftJoyY = AnalogValue[1][1];
      pad->rightJoyX = AnalogValue[1][2];
      pad->rightJoyY = AnalogValue[1][3];
    }
    else {
      pad->controllerType = 4; // standard
    }

    if (pcsx_style) { // ePSXe different from pcsx, swap bytes
       pad->buttonStatus = (pad->buttonStatus>>8)|(pad->buttonStatus<<8);
    }

    return 0;
}

long PADkeypressed(void) {
    unsigned short ksym;
    if (keyLeftOver == NO_EVENT) return 0;

    ksym = keyLeftOver-FIRST_KEY_EVENT;
    keyLeftOver = NO_EVENT;

    return ksym;
}

static void *thread_check_joydevice(void *arg) {
    while (!die_thread_die) {
        CheckPads(1, 0, 1);
    }
    return NULL;
}

// analyse Eventcode
static PadJoyEvent *EventCode2PadJoyEvent(EventCode p_e) {
    static PadJoyEvent event;
    EventCode e;
    int i,p;

    event.event_type = EVENTTYPE_NONE;
    event.pad = 0;
    event.no = 0;
    event.value = 0;

    if (!p_e) {
      return &event;
    }

    e = p_e;

    if (e>RELEASE_EVENT) {
      event.value = 0;
      e -= RELEASE_EVENT;
    }
    else {
      event.value = 1;
    }

    if (e && e<FIRST_JOY_EVENT) {
         event.event_type = EVENTTYPE_KEY;
         event.no = e;
         return &event;
    }

    if (e >= FIRST_ANALOG_EVENT) {
      event.event_type = EVENTTYPE_ANALOG;
      event.pad = (e-FIRST_ANALOG_EVENT)/(256*MAXAXES);
      event.no = (e-ANALOGAXIS_EVENT(event.pad,0,0))/256;
      event.value = e & 0xff;
      return &event;
    }


    for (p=0; p<MAXDEVICES; p++) {
        for (i=0; i<MAXAXES; i++) {
            if (e == AXISPLUS_EVENT(p,i)) {
                event.event_type = EVENTTYPE_AXISPLUS;
                event.pad = p;
                event.no = i;
                return &event;
            }
            if (e == AXISMINUS_EVENT(p,i)) {
                event.event_type = EVENTTYPE_AXISMINUS;
                event.pad = p;
                event.no = i;
                return &event;
            }
        }

        for (i=0; i<MAXBUTTONS; i++) {
            if (e == BUTTON_EVENT(p,i)) {
				event.event_type = EVENTTYPE_BUTTON;
                event.pad = p;
                event.no = i;
                return &event;
            }
        }
    }

    return &event;
}


// reversal of EventCode2String
static EventCode String2EventCode(char *s) {
    static char buffer[256];
    int i,p;
    char *q;
    char push_release;
    EventCode e;

    if (s[0]>='0' && s[0]<='9') return atoi(s);  // allow numeric input

    e=0;
    push_release = 'P';

    switch(s[0]) {
      case 'K':
        push_release = s[1];
        strncpy(buffer, s+3, 255);
        q=buffer;
        i=1;
        while (*q) {
          if (*q=='"') i=!i;
          if (*q==' ' && !i)
            *q='\0';
          else
            q++;
        }
        if (s[2]=='"' && buffer[0] && buffer[strlen(buffer)-1]=='"') {
          buffer[strlen(buffer)-1] = '\0';
          e = XStringToKeysym(buffer);
        }
        break;
      case 'A':
        if (s[1]>='0' && s[1]<='1' && strlen(s)>=5) {
          p = s[1]-'0';
          push_release = s[2];
          i = atoi(s+3);
          q=s+3;
          while (*q && *q!='+' && *q!='-') q++;
          if (*q=='+')
            e = AXISPLUS_EVENT(p,i);
          else if (*q=='-')
            e = AXISMINUS_EVENT(p,i);
        }
        break;
      case 'B':
        if (s[1]>='0' && s[1]<='1' && strlen(s)>=4) {
          p = s[1]-'0';
          push_release = s[2];
          i = atoi(s+3);
          e = BUTTON_EVENT(p,i);
        }
        break;
      case 'X':
        if (s[1]>='0' && s[1]<='1' && strlen(s)>=5) {
          p = s[1]-'0';
          i = atoi(s+3);
          q=s+3;
          while (*q && *q!='v') q++;
          if (*q=='v')
            e = ANALOGAXIS_EVENT(p,i,atoi(q+1));
        }
        break;
    }

    if (push_release=='R')
      return e+RELEASE_EVENT;
    else
      return e;
}

static void loadConfig() {
    FILE *f;
    int i;
    char line[FILENAME_MAX+30];
    int pad=0;
    int macronr=0;
    char *val;

    f=fopen("cfg/padJoy.cfg", "r");
    if (!f) {
        fprintf(stderr, "padJoy: cannot open cfg/padJoy.cfg");
        return;
    }

    while(!feof(f)) {
        fgets(line, FILENAME_MAX+29, f);
        i=strlen(line)-1;
        while (i>0 && line[i]<32) line[i--]='\0';

        val=NULL;
        while(i>0) {
           if (line[i]=='=') val = line+(i+1);
           i--;
        }
        if (val) {
            while (*val==' ') val++;
        }

        if (!strcmp(line, "[general]")) {
            // nothing to do
        }
        else if (!strncmp(line, "pcsx_style", 10)) {
           pcsx_style = atoi(val);
        }
        else if (!strncmp(line, "use_threads", 11)) {
           use_threads = atoi(val);
        }
        else if (!strncmp(line, "use_analog", 10)) {
           use_analog = atoi(val);
        }

        else if (!strcmp(line, "[pad 1]")) {
            pad = 0;
        }
        else if (!strcmp(line, "[pad 2]")) {
            pad = 1;
        }
        else if (!strncmp(line, "[macro ", 7)) {
            macronr = atoi(line+7)-1;
            if (macronr<0 || macronr>=MAXMACROS) macronr=0;
        }
        else if (!strncmp(line, "devicefilename", 14)) {
           strcpy(devicefilename[pad], val);
        }
        else if (!strncmp(line, "minzero", 7)) {
           minzero[pad] = atoi(val);
        }
        else if (!strncmp(line, "maxzero", 7)) {
           maxzero[pad] = atoi(val);
        }
        else if (!strncmp(line, "event_l2", 8)) PadButtons[pad][0] = String2EventCode(val);
        else if (!strncmp(line, "event_r2", 8)) PadButtons[pad][1] = String2EventCode(val);
        else if (!strncmp(line, "event_l1", 8)) PadButtons[pad][2] = String2EventCode(val);
        else if (!strncmp(line, "event_r1", 8)) PadButtons[pad][3] = String2EventCode(val);
        else if (!strncmp(line, "event_triangle", 14)) PadButtons[pad][4] = String2EventCode(val);
        else if (!strncmp(line, "event_circle", 12)) PadButtons[pad][5] = String2EventCode(val);
        else if (!strncmp(line, "event_cross", 11)) PadButtons[pad][6] = String2EventCode(val);
        else if (!strncmp(line, "event_square", 12)) PadButtons[pad][7] = String2EventCode(val);
        else if (!strncmp(line, "event_select", 12)) PadButtons[pad][8] = String2EventCode(val);
        else if (!strncmp(line, "event_lanalog", 13)) PadButtons[pad][9] = String2EventCode(val);
        else if (!strncmp(line, "event_ranalog", 13)) PadButtons[pad][10] = String2EventCode(val);
        else if (!strncmp(line, "event_start", 11)) PadButtons[pad][11] = String2EventCode(val);
        else if (!strncmp(line, "event_up", 8)) PadButtons[pad][12] = String2EventCode(val);
        else if (!strncmp(line, "event_right", 11)) PadButtons[pad][13] = String2EventCode(val);
        else if (!strncmp(line, "event_down", 10)) PadButtons[pad][14] = String2EventCode(val);
        else if (!strncmp(line, "event_left", 10)) PadButtons[pad][15] = String2EventCode(val);
        else if (!strncmp(line, "event_lanax", 11)) PadButtons[pad][16] = String2EventCode(val);
        else if (!strncmp(line, "event_lanay", 11)) PadButtons[pad][17] = String2EventCode(val);
        else if (!strncmp(line, "event_ranax", 11)) PadButtons[pad][18] = String2EventCode(val);
        else if (!strncmp(line, "event_ranay", 11)) PadButtons[pad][19] = String2EventCode(val);
        else if (!strncmp(line, "event_launch", 12)) macroLaunch[pad][macronr] = String2EventCode(val);
        else if (!strncmp(line, "events", 6)) {
            i=0;
            while (*val) {
                macroEvents[pad][macronr][i++]=String2EventCode(val);
                while (*val && *val!=' ') val++;
                if (*val==' ') val++;
            }
            macroEvents[pad][macronr][i]=NO_EVENT;
        }
        else if (!strncmp(line, "interval", 8)) {
            i=0;
            while (*val) {
                macroInterval[pad][macronr][i++]=atol(val);
                while (*val && *val!=' ') val++;
                if (*val==' ') val++;
            }
        }
        else fprintf(stderr, "padJoy: dont understand %s\n", line);
   }
}


long PADconfigure(void) {
    system("cfg/cfgPadJoy");
    return 0;
}


/*---------------------------------------------------------------------*/
/*                          About dialogue stuff                       */
/*---------------------------------------------------------------------*/


void PADabout(void) {
    system("cfg/cfgPadJoy -about");
}

long PADtest(void) {
    int i;
    int f;

    int r=1;

    for (i=0; i<2; i++) {
        if (devicefilename[i][0]) {
            r = 0;
            f = open(devicefilename[i], O_RDONLY);
            if (f == -1) {
                return -1;
            }
            close (f);
        }
    }

    return r;
}
