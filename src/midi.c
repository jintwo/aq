#include <SDL2/SDL.h>
#include "midi.h"


MidiMessageFn midi_callback;

static void midi_platform_init(void);


void midi_init(MidiMessageFn fn) {
  midi_callback = fn;
  midi_platform_init();
}


static void send_message(MidiMessage msg) {
  if (midi_callback) { midi_callback(msg); }
}


#ifdef __linux__

#include <sys/select.h>

static const int sizes[] = {
  #define X(e, val, len) [val] = len,
  MIDI_TYPE_LIST
  #undef X
};

typedef struct { FILE *fp; int fd; } MidiInput;
static MidiInput midi_inputs[16];


static int midi_thread(void *udata) {
  for (;;) {
    /* prepare readset for select */
    fd_set readset;
    int max_fd = 0;
    FD_ZERO(&readset);
    for (int i = 0; midi_inputs[i].fp; i++) {
      MidiInput mi = midi_inputs[i];
      FD_SET(mi.fd, &readset);
      if (mi.fd > max_fd) { max_fd = mi.fd; }
    }

    /* wait for input */
    select(max_fd + 1, &readset, NULL, NULL, NULL);

    /* handle inputs */
    for (int i = 0; midi_inputs[i].fp; i++) {
      MidiInput mi = midi_inputs[i];
      if (FD_ISSET(mi.fd, &readset)) {
        /* read midi message */
        MidiMessage msg;
        msg.b[0] = fgetc(mi.fp);
        int n = sizes[msg.b[0]];
        for (int i = 1; i < n; i++) {
          msg.b[i] = fgetc(mi.fp);
        }
        send_message(msg);
      }
    }
  }

  return 0;
}


static void midi_platform_init(void) {
  /* find and open inputs */
  for (int i = 1; i < 16; i++) {
    char filename[32];
    sprintf(filename, "/dev/midi%d", i);
    FILE *fp = fopen(filename, "rb");
    if (fp) {
      midi_inputs[i - 1] = (MidiInput) { .fp = fp, .fd = fileno(fp) };
    }
  }

  /* init input thread */
  SDL_CreateThread(midi_thread, "Midi Input", NULL);
}

#endif



#ifdef _WIN32

#include <windows.h>

static void CALLBACK midi_input_callback(HMIDIIN hMidiIn, UINT wMsg,
  DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
  if (wMsg == MIM_DATA) {
    MidiMessage msg;
    memcpy(&msg, &dwParam1, sizeof(msg));
    send_message(msg);
  }
}


static void midi_platform_init(void) {
  int n = midiInGetNumDevs();

  /* init all midi devices */
  for (int i = 0; i < n; i++) {
    HMIDIIN dev = NULL;
    int res = midiInOpen(&dev, i, (DWORD_PTR) midi_input_callback, i, CALLBACK_FUNCTION);
    expect(res == MMSYSERR_NOERROR);
    midiInStart(dev);
  }
}

#endif