/* doomgeneric_vita.c – Chex Quest 2 on PS Vita */
#define VITA_GAME_DATA_DIR "ux0:/data/chexquest2/"

#include "d_event.h"
#include "deh_str.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_argv.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

extern void D_PostEvent(event_t *ev);
extern char *savegamedir;
#include "opl3.h"
#include <math.h>
#include <psp2/apputil.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TICRATE 35
#define SCREENWIDTH 320
#define SCREENHEIGHT 200
#define VITA_W 960
#define VITA_H 544
#define OUTPUT_RATE 48000
#define AUDIO_GRANULARITY 256
#define MIX_CHANNELS 16

byte *I_VideoBuffer = NULL;
int screenvisible = 1, screensaver_mode = 0, vanilla_keyboard_mapping = 0;
int usegamma = 0, usemouse = 0, snd_musicdevice = 3; /* 3 = SB/OPL for music */
int mouse_acceleration = 0, mouse_threshold = 0;

static SceUID fb_memuid;
static void *fb_base = NULL;
static int display_ready = 0, frame_count = 0;
static uint32_t cmap[256];
static uint32_t base_time = 0;

static uint32_t get_ms(void) { return sceKernelGetProcessTimeLow() / 1000; }

static void debug_log(const char *msg) {
  FILE *f = fopen(VITA_GAME_DATA_DIR "debug.log", "a");
  if (f) {
    fprintf(f, "%s\n", msg);
    fclose(f);
  }
}
static void debug_logf(const char *fmt, ...) {
  char buf[512];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof(buf), fmt, a);
  va_end(a);
  debug_log(buf);
}

static void init_display(void) {
  int sz = (960 * 544 * 4 + 0xFFFFF) & ~0xFFFFF;
  SceDisplayFrameBuf fb;
  int ret;
  fb_memuid = sceKernelAllocMemBlock(
      "framebuffer", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, sz, NULL);
  if (fb_memuid < 0)
    fb_memuid = sceKernelAllocMemBlock(
        "framebuffer", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, sz, NULL);
  if (fb_memuid < 0) {
    debug_logf("Alloc failed: 0x%08X", fb_memuid);
    return;
  }
  sceKernelGetMemBlockBase(fb_memuid, &fb_base);
  memset(fb_base, 0, sz);
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  fb.base = fb_base;
  fb.pitch = 960;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = 960;
  fb.height = 544;
  ret = sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  debug_logf("SetFrameBuf: 0x%08X", ret);
  sceDisplayWaitVblankStart();
  display_ready = 1;
}

/* Input */
#define KQUEUE_SZ 64
#define DEADZONE 35
static struct {
  int pressed;
  unsigned char key;
} kq[KQUEUE_SZ];
static int kq_r = 0, kq_w = 0;
static SceCtrlData pad_prev;
static int input_init = 0, analog_held[6];
static int current_weapon = 1, weapon_cycle_cooldown = 0;
static unsigned char pending_weapon_release = 0; /* key to release next tic */
/* D-Pad sinistra/destra: ripetizione tenendo premuto (tick di gioco ≈ 35 Hz) */
static int weapon_l_charge = 0, weapon_r_charge = 0;
static int weapon_touch_prev_in_bar = 0;
#define WEAPON_KEY_GAP 3       /* min tra un cambio arma e il successivo */
#define WEAPON_HOLD_INITIAL 12 /* attesa prima della prima ripetizione */
#define WEAPON_HOLD_REPEAT 4   /* tra una ripetizione e l'altra mentre tieni */

static void kq_push(int p, unsigned char k) {
  int n = (kq_w + 1) % KQUEUE_SZ;
  if (n == kq_r)
    return;
  kq[kq_w].pressed = p;
  kq[kq_w].key = k;
  kq_w = n;
}
static void analog_axis(int val, int nk, int pk, int *nh, int *ph) {
  int wn = val<-DEADZONE, wp = val> DEADZONE;
  if (wn && !*nh) {
    kq_push(1, nk);
    *nh = 1;
  }
  if (!wn && *nh) {
    kq_push(0, nk);
    *nh = 0;
  }
  if (wp && !*ph) {
    kq_push(1, pk);
    *ph = 1;
  }
  if (!wp && *ph) {
    kq_push(0, pk);
    *ph = 0;
  }
}

static void pulse_weapon_digit(void) {
  kq_push(1, (unsigned char)('0' + current_weapon));
  pending_weapon_release = (unsigned char)('0' + current_weapon);
  weapon_cycle_cooldown = WEAPON_KEY_GAP;
}

static void do_poll_input(void) {
  SceCtrlData pad;
  int i;
  if (!input_init) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_START);
    memset(&pad_prev, 0, sizeof(pad_prev));
    memset(analog_held, 0, sizeof(analog_held));
    input_init = 1;
  }
  sceCtrlPeekBufferPositive(0, &pad, 1);
  if (weapon_cycle_cooldown > 0)
    weapon_cycle_cooldown--;

  /* Face buttons + triggers */
  {
    struct {
      unsigned btn;
      unsigned char key;
    } bm[] = {{SCE_CTRL_CROSS, KEY_USE},
              {SCE_CTRL_SQUARE, KEY_FIRE},
              {SCE_CTRL_CIRCLE, KEY_RALT},
              {SCE_CTRL_TRIANGLE, KEY_TAB},
              {SCE_CTRL_RTRIGGER, KEY_FIRE},
              {SCE_CTRL_LTRIGGER, KEY_RSHIFT},
              {SCE_CTRL_START, KEY_ESCAPE},
              {SCE_CTRL_SELECT, KEY_ENTER},
              {0, 0}};
    for (i = 0; bm[i].btn; i++) {
      int now = (pad.buttons & bm[i].btn) != 0;
      int was = (pad_prev.buttons & bm[i].btn) != 0;
      if (now && !was)
        kq_push(1, bm[i].key);
      if (!now && was)
        kq_push(0, bm[i].key);
    }
  }

  /* Release weapon key from previous tic */
  if (pending_weapon_release) {
    kq_push(0, pending_weapon_release);
    pending_weapon_release = 0;
  }

  /* D-Pad su/giù: disponibili (menu / Doom li usa come frecce se l’engine li mappa) */
  /* D-Pad sx/dx: cambio arma veloce; tieni premuto per scorrere in sequenza */

  {
    int l = (pad.buttons & SCE_CTRL_LEFT) != 0;
    int lw = (pad_prev.buttons & SCE_CTRL_LEFT) != 0;
    int r = (pad.buttons & SCE_CTRL_RIGHT) != 0;
    int rw = (pad_prev.buttons & SCE_CTRL_RIGHT) != 0;

    int can_weapon = weapon_cycle_cooldown == 0;

    if (!r) {
      weapon_r_charge = 0;
    } else {
      if (!rw) {
        if (can_weapon) {
          current_weapon = current_weapon >= 7 ? 1 : current_weapon + 1;
          pulse_weapon_digit();
        }
        weapon_r_charge = WEAPON_HOLD_INITIAL;
      } else if (can_weapon && weapon_r_charge > 0) {
        weapon_r_charge--;
        if (weapon_r_charge == 0) {
          current_weapon = current_weapon >= 7 ? 1 : current_weapon + 1;
          pulse_weapon_digit();
          weapon_r_charge = WEAPON_HOLD_REPEAT;
        }
      }
    }

    if (!l) {
      weapon_l_charge = 0;
    } else {
      if (!lw) {
        if (can_weapon) {
          current_weapon = current_weapon <= 1 ? 7 : current_weapon - 1;
          pulse_weapon_digit();
        }
        weapon_l_charge = WEAPON_HOLD_INITIAL;
      } else if (can_weapon && weapon_l_charge > 0) {
        weapon_l_charge--;
        if (weapon_l_charge == 0) {
          current_weapon = current_weapon <= 1 ? 7 : current_weapon - 1;
          pulse_weapon_digit();
          weapon_l_charge = WEAPON_HOLD_REPEAT;
        }
      }
    }
  }

  /* Analog sticks: left = move/strafe, right = turn */
  analog_axis(pad.ly - 128, KEY_UPARROW, KEY_DOWNARROW, &analog_held[0],
              &analog_held[1]);
  analog_axis(pad.lx - 128, KEY_STRAFE_L, KEY_STRAFE_R, &analog_held[2],
              &analog_held[3]);
  analog_axis(pad.rx - 128, KEY_LEFTARROW, KEY_RIGHTARROW, &analog_held[4],
              &analog_held[5]);

  /* Front touch: barra alta = arma (un colpo per tap; niente ripetizioni ogni frame) */
  {
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    int in_bar = touch.reportNum > 0 && (touch.report[0].y / 2 < 60);
    if (in_bar) {
      int slot =
          ((int)(touch.report[0].x / 2)) / ((int)(VITA_W / 7));
      if (slot >= 0 && slot < 7 && !weapon_touch_prev_in_bar &&
          weapon_cycle_cooldown == 0 && !pending_weapon_release) {
        current_weapon = slot + 1;
        kq_push(1, (unsigned char)('1' + slot));
        kq_push(0, (unsigned char)('1' + slot));
        weapon_cycle_cooldown = WEAPON_KEY_GAP;
      }
      weapon_touch_prev_in_bar = 1;
    } else {
      weapon_touch_prev_in_bar = 0;
    }
  }
  pad_prev = pad;
}

/* SFX ENGINE */
typedef struct {
  const byte *data;
  int length, pos_fixed, step_fixed;
  int vol_left, vol_right, handle, active, lumpnum;
} mix_channel_t;

static mix_channel_t mix_ch[MIX_CHANNELS];
static int sfx_port = -1;
static SceUID sfx_thread_id = -1, sfx_mutex = -1;
static volatile int sfx_running = 0;
static volatile int sfx_master_vol = 15;
static int next_handle = 1, audio_ready = 0;
static int16_t __attribute__((aligned(64))) sfx_buf[2][AUDIO_GRANULARITY * 2];
static int sfx_buf_idx = 0;

#define SFX_CACHE_MAX 128
typedef struct {
  int lumpnum;
  const byte *samples;
  int length, samplerate;
} sfx_cache_entry_t;
static sfx_cache_entry_t sfx_cache[SFX_CACHE_MAX];
static int sfx_cache_count = 0;

static sfx_cache_entry_t *sfx_cache_get(int lumpnum) {
  int i;
  byte *raw;
  int rawlen, format, rate, nsamples;
  for (i = 0; i < sfx_cache_count; i++)
    if (sfx_cache[i].lumpnum == lumpnum)
      return &sfx_cache[i];
  rawlen = W_LumpLength(lumpnum);
  if (rawlen < 8)
    return NULL;
  raw = W_CacheLumpNum(lumpnum, PU_STATIC);
  if (!raw)
    return NULL;
  format = raw[0] | (raw[1] << 8);
  rate = raw[2] | (raw[3] << 8);
  nsamples = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
  if (format != 3)
    return NULL;
  if (rate < 4000 || rate > 48000)
    rate = 11025;
  if (nsamples > rawlen - 8)
    nsamples = rawlen - 8;
  if (nsamples <= 0)
    return NULL;
  {
    const byte *pcm = raw + 8;
    int pcm_len = nsamples;
    if (pcm_len > 32) {
      pcm += 16;
      pcm_len -= 32;
    }
    if (sfx_cache_count >= SFX_CACHE_MAX)
      return NULL;
    i = sfx_cache_count++;
    sfx_cache[i].lumpnum = lumpnum;
    sfx_cache[i].samples = pcm;
    sfx_cache[i].length = pcm_len;
    sfx_cache[i].samplerate = rate;
  }
  return &sfx_cache[i];
}

/* OPL3 MUSIC ENGINE */
#define GENMIDI_NUM_INSTRS 175
#define GENMIDI_HEADER "#OPL_II#"
#define GENMIDI_FLAG_FIXED 0x0001
#define OPL_NUM_VOICES 9
#define PERCUSSION_CHAN 15

#pragma pack(push, 1)
typedef struct {
  uint8_t tremolo, attack, sustain, waveform, scale, level;
} genmidi_op_t;
typedef struct {
  genmidi_op_t modulator;
  uint8_t feedback;
  genmidi_op_t carrier;
  uint8_t unused;
  int16_t base_note_offset;
} genmidi_voice_t;
typedef struct {
  uint16_t flags;
  uint8_t fine_tuning, fixed_note;
  genmidi_voice_t voices[2];
} genmidi_instr_t;
#pragma pack(pop)

static const uint8_t opl_mod_offset[9] = {0x00, 0x01, 0x02, 0x08, 0x09,
                                          0x0A, 0x10, 0x11, 0x12};
static const uint8_t opl_car_offset[9] = {0x03, 0x04, 0x05, 0x0B, 0x0C,
                                          0x0D, 0x13, 0x14, 0x15};
static const uint16_t opl_freq_table[12] = {0x157, 0x16B, 0x181, 0x198,
                                            0x1B0, 0x1CA, 0x1E5, 0x202,
                                            0x220, 0x241, 0x263, 0x287};

typedef struct {
  int active, mus_channel, note, volume;
  uint32_t age;
} opl_voice_t;
typedef struct {
  int volume, patch, pitch_bend;
} opl_mus_chan_t;
typedef struct {
  opl3_chip chip;
  genmidi_instr_t *genmidi;
  int genmidi_loaded;
  opl_voice_t voices[OPL_NUM_VOICES];
  opl_mus_chan_t channels[16];
  uint32_t voice_age;
  const byte *mus_data;
  int mus_len, mus_pos, score_start, score_len;
  int playing, looping, delay_left, tick_samples, tick_counter, music_volume;
} opl_music_t;

static opl_music_t opl_music;
static SceUID mus_mutex = -1;
static uint8_t opl_reg_b0[9];

static void opl_write(uint16_t reg, uint8_t val) {
  OPL3_WriteReg(&opl_music.chip, reg, val);
}

static void load_genmidi(void) {
  int lump;
  byte *data;
  int len;
  opl_music.genmidi_loaded = 0;
  lump = W_CheckNumForName("GENMIDI");
  if (lump < 0) {
    debug_log("GENMIDI not found");
    return;
  }
  len = W_LumpLength(lump);
  data = W_CacheLumpNum(lump, PU_STATIC);
  if (len < 8 + (int)sizeof(genmidi_instr_t) * GENMIDI_NUM_INSTRS)
    return;
  if (memcmp(data, GENMIDI_HEADER, 8) != 0)
    return;
  opl_music.genmidi = (genmidi_instr_t *)(data + 8);
  opl_music.genmidi_loaded = 1;
}

static void opl_write_operator(int slot, genmidi_op_t *op, int vol) {
  int l, fl;
  opl_write(0x20 + slot, op->tremolo);
  if (vol >= 0) {
    l = 0x3F - (op->level & 0x3F);
    l = (l * vol) / 127;
    fl = 0x3F - l;
    if (fl < 0)
      fl = 0;
    if (fl > 0x3F)
      fl = 0x3F;
    opl_write(0x40 + slot, (op->scale & 0xC0) | fl);
  } else {
    opl_write(0x40 + slot, (op->scale & 0xC0) | (op->level & 0x3F));
  }
  opl_write(0x60 + slot, op->attack);
  opl_write(0x80 + slot, op->sustain);
  opl_write(0xE0 + slot, op->waveform & 0x07);
}

static void opl_set_instrument(int voice, genmidi_voice_t *gv, int volume) {
  int is_add = gv->feedback & 0x01;
  opl_write(0xC0 + voice, (gv->feedback & 0x0F) | 0x30);
  opl_write_operator(opl_mod_offset[voice], &gv->modulator,
                     is_add ? volume : -1);
  opl_write_operator(opl_car_offset[voice], &gv->carrier, volume);
}

static void opl_update_volume(int voice, int volume, genmidi_voice_t *gv) {
  int l, fl;
  l = 0x3F - (gv->carrier.level & 0x3F);
  l = (l * volume) / 127;
  fl = 0x3F - l;
  if (fl < 0)
    fl = 0;
  if (fl > 0x3F)
    fl = 0x3F;
  opl_write(0x40 + opl_car_offset[voice], (gv->carrier.scale & 0xC0) | fl);
  if (gv->feedback & 0x01) {
    l = 0x3F - (gv->modulator.level & 0x3F);
    l = (l * volume) / 127;
    fl = 0x3F - l;
    if (fl < 0)
      fl = 0;
    if (fl > 0x3F)
      fl = 0x3F;
    opl_write(0x40 + opl_mod_offset[voice], (gv->modulator.scale & 0xC0) | fl);
  }
}

static void opl_key_on(int voice, int note) {
  int oct, fn;
  uint16_t freq;
  if (note < 0)
    note = 0;
  if (note > 127)
    note = 127;
  oct = (note / 12) - 1;
  fn = note % 12;
  if (oct < 0)
    oct = 0;
  if (oct > 7)
    oct = 7;
  freq = opl_freq_table[fn];
  opl_write(0xA0 + voice, freq & 0xFF);
  opl_reg_b0[voice] = 0x20 | ((oct & 7) << 2) | ((freq >> 8) & 3);
  opl_write(0xB0 + voice, opl_reg_b0[voice]);
}
static void opl_key_off(int v) {
  opl_reg_b0[v] &= ~0x20;
  opl_write(0xB0 + v, opl_reg_b0[v]);
}
static void opl_silence_voice(int v) {
  opl_reg_b0[v] = 0;
  opl_write(0xB0 + v, 0);
  opl_write(0xA0 + v, 0);
}

static int opl_alloc_voice(int ch, int pri) {
  int i, best;
  uint32_t oldest;
  (void)pri;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (!opl_music.voices[i].active)
      return i;
  best = 0;
  oldest = 0xFFFFFFFF;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (opl_music.voices[i].age < oldest) {
      oldest = opl_music.voices[i].age;
      best = i;
    }
  opl_key_off(best);
  opl_music.voices[best].active = 0;
  return best;
}

static genmidi_voice_t *get_voice_instr(int vi) {
  int ch, patch;
  if (!opl_music.genmidi_loaded)
    return NULL;
  ch = opl_music.voices[vi].mus_channel;
  patch = opl_music.channels[ch].patch;
  if (ch == PERCUSSION_CHAN) {
    int n = opl_music.voices[vi].note;
    if (n >= 35 && n <= 81)
      patch = 128 + n - 35;
    else
      return NULL;
  }
  if (patch < 0 || patch >= GENMIDI_NUM_INSTRS)
    return NULL;
  return &opl_music.genmidi[patch].voices[0];
}

static void mus_opl_note_on(int channel, int note, int volume) {
  int voice, patch, mn;
  genmidi_instr_t *inst;
  genmidi_voice_t *gv;
  if (!opl_music.genmidi_loaded)
    return;
  patch = opl_music.channels[channel].patch;
  if (channel == PERCUSSION_CHAN) {
    if (note < 35 || note > 81)
      return;
    patch = 128 + note - 35;
  }
  if (patch < 0 || patch >= GENMIDI_NUM_INSTRS)
    return;
  inst = &opl_music.genmidi[patch];
  gv = &inst->voices[0];
  voice = opl_alloc_voice(channel, volume >= 0 ? volume : 64);
  if (inst->flags & GENMIDI_FLAG_FIXED)
    mn = inst->fixed_note;
  else {
    mn = note;
    int off = (int)(int16_t)gv->base_note_offset;
    if (off > -48 && off < 48)
      mn += off;
  }
  if (mn < 0)
    mn = 0;
  if (mn > 127)
    mn = 127;
  if (volume < 0)
    volume = opl_music.channels[channel].volume;
  if (volume > 127)
    volume = 127;
  opl_set_instrument(voice, gv, volume);
  opl_key_on(voice, mn);
  opl_music.voices[voice].active = 1;
  opl_music.voices[voice].mus_channel = channel;
  opl_music.voices[voice].note = note;
  opl_music.voices[voice].volume = volume;
  opl_music.voices[voice].age = opl_music.voice_age++;
}

static void mus_opl_note_off(int ch, int note) {
  int i;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (opl_music.voices[i].active && opl_music.voices[i].mus_channel == ch &&
        opl_music.voices[i].note == note) {
      opl_key_off(i);
      opl_music.voices[i].active = 0;
    }
}
static void mus_opl_all_off(int ch) {
  int i;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (opl_music.voices[i].mus_channel == ch && opl_music.voices[i].active) {
      opl_key_off(i);
      opl_music.voices[i].active = 0;
    }
}
static byte mus_rb(void) {
  if (opl_music.mus_pos >= opl_music.mus_len)
    return 0;
  return opl_music.mus_data[opl_music.mus_pos++];
}

static void mus_process_event(void) {
  byte ev, channel, type;
  int last, i;
  if (!opl_music.playing)
    return;
  if (opl_music.mus_pos >= opl_music.score_start + opl_music.score_len) {
    if (opl_music.looping) {
      opl_music.mus_pos = opl_music.score_start;
      for (i = 0; i < OPL_NUM_VOICES; i++) {
        opl_silence_voice(i);
        opl_music.voices[i].active = 0;
      }
    } else
      opl_music.playing = 0;
    return;
  }
  ev = mus_rb();
  channel = ev & 0x0F;
  type = (ev >> 4) & 0x07;
  last = ev & 0x80;
  switch (type) {
  case 0: {
    byte n = mus_rb();
    mus_opl_note_off(channel, n & 0x7F);
    break;
  }
  case 1: {
    byte nb = mus_rb();
    int n = nb & 0x7F, v = -1;
    if (nb & 0x80) {
      v = mus_rb() & 0x7F;
      opl_music.channels[channel].volume = v;
    }
    mus_opl_note_on(channel, n, v);
    break;
  }
  case 2: {
    byte pb = mus_rb();
    opl_music.channels[channel].pitch_bend = pb;
    break;
  }
  case 3: {
    byte sys = mus_rb();
    if (sys == 10 || sys == 11 || sys == 14)
      mus_opl_all_off(channel);
    break;
  }
  case 4: {
    byte ctrl = mus_rb(), val = mus_rb();
    if (ctrl == 0)
      opl_music.channels[channel].patch = val;
    else if (ctrl == 3) {
      opl_music.channels[channel].volume = val & 0x7F;
      for (i = 0; i < OPL_NUM_VOICES; i++) {
        if (opl_music.voices[i].active &&
            opl_music.voices[i].mus_channel == channel) {
          genmidi_voice_t *gv = get_voice_instr(i);
          if (gv) {
            int cv = (opl_music.voices[i].volume * (val & 0x7F)) / 127;
            if (cv > 127)
              cv = 127;
            opl_update_volume(i, cv, gv);
          }
        }
      }
    }
    break;
  }
  case 5:
  case 6:
    if (opl_music.looping) {
      opl_music.mus_pos = opl_music.score_start;
      for (i = 0; i < OPL_NUM_VOICES; i++) {
        opl_silence_voice(i);
        opl_music.voices[i].active = 0;
      }
    } else
      opl_music.playing = 0;
    return;
  default:
    break;
  }
  if (last) {
    int delay = 0;
    byte db;
    do {
      db = mus_rb();
      delay = (delay << 7) | (db & 0x7F);
    } while (db & 0x80);
    opl_music.delay_left = delay;
  }
}

static void mus_opl_tick(void) {
  if (!opl_music.playing)
    return;
  while (opl_music.delay_left <= 0 && opl_music.playing)
    mus_process_event();
  if (opl_music.delay_left > 0)
    opl_music.delay_left--;
}

static int opl_diag_done = 0;
static void opl_mix_into(int32_t *accum, int ns) {
  int s, mv;
  if (!opl_music.playing)
    return;
  mv = opl_music.music_volume;
  if (mv <= 0)
    return;
  for (s = 0; s < ns; s++) {
    int16_t buf[4];
    opl_music.tick_counter--;
    if (opl_music.tick_counter <= 0) {
      opl_music.tick_counter = opl_music.tick_samples;
      mus_opl_tick();
    }
    memset(buf, 0, sizeof(buf));
    OPL3_GenerateResampled(&opl_music.chip, buf);
    /* OPL output — * 2 gives clean output without clipping */
    accum[s * 2 + 0] += ((int32_t)buf[0] * mv * 2) / 15;
    accum[s * 2 + 1] += ((int32_t)buf[1] * mv * 2) / 15;
    /* One-time diagnostic */
    if (!opl_diag_done && (buf[0] != 0 || buf[1] != 0)) {
      opl_diag_done = 1;
      debug_logf("OPL output: first non-zero sample buf[0]=%d buf[1]=%d mv=%d",
                 buf[0], buf[1], mv);
    }
  }
}

/* Combined audio mixing */
static void mix_into(int16_t *out, int nsamples) {
  int i, ch, mvol;
  int32_t accum[AUDIO_GRANULARITY * 2];
  memset(accum, 0, nsamples * 2 * sizeof(int32_t));
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  mvol = sfx_master_vol;
  if (mvol < 0)
    mvol = 0;
  if (mvol > 15)
    mvol = 15;
  for (i = 0; i < nsamples; i++) {
    int32_t al = 0, ar = 0;
    for (ch = 0; ch < MIX_CHANNELS; ch++) {
      mix_channel_t *c = &mix_ch[ch];
      int pos, sample;
      if (!c->active || !c->data)
        continue;
      pos = c->pos_fixed >> 16;
      if (pos >= c->length) {
        c->active = 0;
        continue;
      }
      sample = ((int)c->data[pos] - 128) * 256;
      al += (sample * c->vol_left) >> 8;
      ar += (sample * c->vol_right) >> 8;
      c->pos_fixed += c->step_fixed;
    }
    accum[i * 2 + 0] += (al * mvol) / 15;
    accum[i * 2 + 1] += (ar * mvol) / 15;
  }
  sceKernelUnlockMutex(sfx_mutex, 1);
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_mix_into(accum, nsamples);
    sceKernelUnlockMutex(mus_mutex, 1);
  }
  for (i = 0; i < nsamples * 2; i++) {
    int32_t v = accum[i];
    if (v > 32767)
      v = 32767;
    if (v < -32768)
      v = -32768;
    out[i] = (int16_t)v;
  }
}

static int sfx_thread_func(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  while (sfx_running) {
    int16_t *buf = sfx_buf[sfx_buf_idx];
    mix_into(buf, AUDIO_GRANULARITY);
    sceAudioOutOutput(sfx_port, buf);
    sfx_buf_idx ^= 1;
  }
  return 0;
}

static void start_audio_system(void) {
  int ret, vols[2], i;
  if (audio_ready)
    return;
  memset(mix_ch, 0, sizeof(mix_ch));
  memset(sfx_buf, 0, sizeof(sfx_buf));
  memset(sfx_cache, 0, sizeof(sfx_cache));
  sfx_cache_count = 0;
  sfx_buf_idx = 0;
  memset(&opl_music, 0, sizeof(opl_music));
  memset(opl_reg_b0, 0, sizeof(opl_reg_b0));
  OPL3_Reset(&opl_music.chip, OUTPUT_RATE);
  opl_write(0x01, 0x20);
  opl_write(0x08, 0x40);
  opl_write(0xBD, 0x00);
  for (i = 0; i < 9; i++)
    opl_silence_voice(i);
  opl_music.music_volume = 15;
  opl_music.tick_samples = OUTPUT_RATE / 140;
  opl_music.tick_counter = opl_music.tick_samples;
  sfx_mutex = sceKernelCreateMutex("sfx_mutex", 0, 0, NULL);
  if (sfx_mutex < 0)
    return;
  mus_mutex = sceKernelCreateMutex("mus_mutex", 0, 0, NULL);
  sfx_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRANULARITY,
                                 OUTPUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
  if (sfx_port < 0)
    sfx_port =
        sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRANULARITY,
                            OUTPUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
  if (sfx_port < 0) {
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
    if (mus_mutex >= 0) {
      sceKernelDeleteMutex(mus_mutex);
      mus_mutex = -1;
    }
    return;
  }
  vols[0] = SCE_AUDIO_VOLUME_0DB;
  vols[1] = SCE_AUDIO_VOLUME_0DB;
  sceAudioOutSetVolume(
      sfx_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
  sfx_running = 1;
  sfx_thread_id = sceKernelCreateThread("doom_audio", sfx_thread_func,
                                        0x10000100, 0x10000, 0, 0, NULL);
  if (sfx_thread_id < 0) {
    sfx_running = 0;
    sceAudioOutReleasePort(sfx_port);
    sfx_port = -1;
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
    if (mus_mutex >= 0) {
      sceKernelDeleteMutex(mus_mutex);
      mus_mutex = -1;
    }
    return;
  }
  ret = sceKernelStartThread(sfx_thread_id, 0, NULL);
  if (ret < 0) {
    sfx_running = 0;
    sceKernelDeleteThread(sfx_thread_id);
    sfx_thread_id = -1;
    sceAudioOutReleasePort(sfx_port);
    sfx_port = -1;
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
    if (mus_mutex >= 0) {
      sceKernelDeleteMutex(mus_mutex);
      mus_mutex = -1;
    }
    return;
  }
  audio_ready = 1;
  debug_log("Audio OK");
}

/* DG interface */
void DG_Init(void) {
  debug_log("DG_Init called");
  base_time = get_ms();
}
void DG_DrawFrame(
    void) { /* intentionally empty – rendering via I_FinishUpdate */ }
void DG_SleepMs(uint32_t ms) { sceKernelDelayThread(ms * 1000); }
uint32_t DG_GetTicksMs(void) { return get_ms() - base_time; }
int DG_GetKey(int *pressed, unsigned char *key) {
  (void)pressed;
  (void)key;
  return 0;
}
void DG_SetWindowTitle(const char *t) { (void)t; }

/* I_* system */
void I_Init(void) {}
void I_Quit(void) {
  sfx_running = 0;
  if (sfx_thread_id >= 0) {
    sceKernelWaitThreadEnd(sfx_thread_id, NULL, NULL);
    sceKernelDeleteThread(sfx_thread_id);
  }
  if (sfx_port >= 0)
    sceAudioOutReleasePort(sfx_port);
  if (sfx_mutex >= 0)
    sceKernelDeleteMutex(sfx_mutex);
  if (mus_mutex >= 0)
    sceKernelDeleteMutex(mus_mutex);
  sceKernelExitProcess(0);
}
void I_Error(const char *error, ...) {
  char buf[512];
  va_list a;
  va_start(a, error);
  vsnprintf(buf, sizeof(buf), error, a);
  va_end(a);
  debug_logf("I_Error: %s", buf);
  sfx_running = 0;
  sceKernelDelayThread(2000000); /* 2s pause so log flushes */
  sceKernelExitProcess(0);
}
void I_WaitVBL(int c) { sceKernelDelayThread(c * 14286); }
int I_GetTime(void) {
  uint32_t ms = get_ms() - base_time;
  return (int)(ms * TICRATE / 1000);
}
void I_Sleep(int ms) { sceKernelDelayThread(ms * 1000); }
byte *I_ZoneBase(int *size) {
  byte *p;
  *size = 16 * 1024 * 1024;
  p = (byte *)malloc(*size);
  if (!p) {
    debug_log("ZoneBase: 16MB alloc failed, trying 8MB");
    *size = 8 * 1024 * 1024;
    p = (byte *)malloc(*size);
  }
  if (!p) {
    debug_log("FATAL: ZoneBase alloc failed entirely");
    *size = 4 * 1024 * 1024;
    p = (byte *)malloc(*size);
  }
  debug_logf("ZoneBase: allocated %d bytes at %p", *size, (void *)p);
  return p;
}
void I_Tactile(int a, int b, int c) {
  (void)a;
  (void)b;
  (void)c;
}
int I_ConsoleStdout(void) { return 0; }
boolean I_GetMemoryValue(unsigned int o, void *v, int s) {
  (void)o;
  (void)v;
  (void)s;
  return 0;
}
void I_AtExit(void (*f)(void), boolean r) {
  (void)f;
  (void)r;
}
void I_PrintBanner(const char *m) { (void)m; }
void I_PrintDivider(void) {}
void I_PrintStartupBanner(const char *g) { (void)g; }
void I_DisplayFPSDots(boolean d) { (void)d; }
void I_CheckIsScreensaver(void) {}
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(void (*f)(boolean g)) { (void)f; }
int I_GetTime_RealTime(void) { return I_GetTime(); }
int I_GetTimeMS(void) { return (int)(get_ms() - base_time); }
void I_InitTimer(void) { base_time = get_ms(); }

/* VIDEO */
void I_InitGraphics(void) {
  int i;
  debug_log("I_InitGraphics: allocating video buffer");
  I_VideoBuffer = (byte *)calloc(SCREENWIDTH * SCREENHEIGHT, 1);
  if (!I_VideoBuffer) {
    debug_log("FATAL: I_VideoBuffer alloc failed");
    sceKernelExitProcess(0);
  }
  for (i = 0; i < 256; i++)
    cmap[i] = 0xFF000000u | ((uint32_t)i << 16) | ((uint32_t)i << 8) | i;
  debug_log("I_InitGraphics: done");
}
void I_SetPalette(byte *pal) {
  int i;
  for (i = 0; i < 256; i++) {
    uint32_t r = pal[i * 3 + 0], g = pal[i * 3 + 1], b = pal[i * 3 + 2];
    cmap[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
  }
}
void I_FinishUpdate(void) {
  uint32_t *dst;
  int x, y, step_x, step_y, sy_f;
  if (!display_ready || !I_VideoBuffer || !fb_base)
    return;
  dst = (uint32_t *)fb_base;
  step_x = (SCREENWIDTH << 16) / VITA_W;
  step_y = (SCREENHEIGHT << 16) / VITA_H;
  sy_f = 0;
  for (y = 0; y < VITA_H; y++) {
    int sy = sy_f >> 16;
    uint32_t *dr;
    byte *sr;
    int sx_f;
    if (sy >= SCREENHEIGHT)
      sy = SCREENHEIGHT - 1;
    dr = dst + y * 960;
    sr = I_VideoBuffer + sy * SCREENWIDTH;
    sx_f = 0;
    for (x = 0; x < VITA_W; x++) {
      int sx = sx_f >> 16;
      if (sx >= SCREENWIDTH)
        sx = SCREENWIDTH - 1;
      dr[x] = cmap[sr[sx]];
      sx_f += step_x;
    }
    sy_f += step_y;
  }
  {
    SceDisplayFrameBuf dfb;
    memset(&dfb, 0, sizeof(dfb));
    dfb.size = sizeof(dfb);
    dfb.base = fb_base;
    dfb.pitch = 960;
    dfb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    dfb.width = 960;
    dfb.height = 544;
    sceDisplaySetFrameBuf(&dfb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  }
  sceDisplayWaitVblankStart();
  frame_count++;
}
void I_ShutdownGraphics(void) {}
void I_StartFrame(void) {}
void I_StartTic(void) {
  event_t event;
  do_poll_input();
  while (kq_r != kq_w) {
    event.type = kq[kq_r].pressed ? ev_keydown : ev_keyup;
    event.data1 = kq[kq_r].key;
    event.data2 = kq[kq_r].key;
    event.data3 = 0;
    D_PostEvent(&event);
    kq_r = (kq_r + 1) % KQUEUE_SZ;
  }
}
void I_UpdateNoBlit(void) {}
void I_ReadScreen(byte *scr) {
  if (I_VideoBuffer)
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}
void I_EnableLoadingDisk(void) {}
void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_SetWindowTitle(char *t) { (void)t; }
void I_BindVideoVariables(void) {}
int I_GetPaletteIndex(int r, int g, int b) {
  (void)r;
  (void)g;
  (void)b;
  return 0;
}
void I_InitScale(void) {}
void I_InitInput(void) {}
void I_ShutdownInput(void) {}
void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

/* SOUND interface */
void I_SetChannels(void) {}
void I_SetSfxVolume(int volume) { sfx_master_vol = volume; }
int I_GetSfxLumpNum(sfxinfo_t *sfx) {
  char namebuf[16];
  if (!sfx || !sfx->name || sfx->name[0] == '\0')
    return -1;
  snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
  return W_CheckNumForName(namebuf);
}
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {
  int i, l;
  for (i = 0; i < num_sounds; i++) {
    l = I_GetSfxLumpNum(&sounds[i]);
    if (l >= 0)
      sfx_cache_get(l);
  }
}
int I_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep) {
  int lumpnum, best, i, oldest, handle;
  sfx_cache_entry_t *entry;
  mix_channel_t *c;
  (void)channel;
  if (!audio_ready || !sfx)
    return 0;
  lumpnum = sfx->lumpnum;
  if (lumpnum < 0) {
    lumpnum = I_GetSfxLumpNum(sfx);
    if (lumpnum < 0)
      return 0;
  }
  entry = sfx_cache_get(lumpnum);
  if (!entry || entry->length <= 0)
    return 0;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  best = 0;
  oldest = 0x7FFFFFFF;
  for (i = 0; i < MIX_CHANNELS; i++) {
    if (!mix_ch[i].active) {
      best = i;
      goto found;
    }
    if (mix_ch[i].handle < oldest) {
      oldest = mix_ch[i].handle;
      best = i;
    }
  }
found:
  c = &mix_ch[best];
  c->data = entry->samples;
  c->length = entry->length;
  c->pos_fixed = 0;
  c->lumpnum = lumpnum;
  c->step_fixed = (int)(((int64_t)entry->samplerate << 16) / OUTPUT_RATE);
  if (c->step_fixed <= 0)
    c->step_fixed = (11025 << 16) / OUTPUT_RATE;
  {
    int vl, vr;
    if (sep < 0 || sep > 255)
      sep = 128;
    vr = (sep * 256) / 255;
    vl = 256 - vr;
    if (vl < 0)
      vl = 0;
    if (vl > 256)
      vl = 256;
    if (vr < 0)
      vr = 0;
    if (vr > 256)
      vr = 256;
    c->vol_left = (vl * vol) / 127;
    c->vol_right = (vr * vol) / 127;
    if (c->vol_left > 256)
      c->vol_left = 256;
    if (c->vol_right > 256)
      c->vol_right = 256;
  }
  handle = next_handle++;
  if (next_handle > 0x7FFFFF00)
    next_handle = 1;
  c->handle = handle;
  c->active = 1;
  sceKernelUnlockMutex(sfx_mutex, 1);
  return handle;
}
void I_StopSound(int handle) {
  int i;
  if (sfx_mutex < 0)
    return;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  for (i = 0; i < MIX_CHANNELS; i++)
    if (mix_ch[i].active && mix_ch[i].handle == handle) {
      mix_ch[i].active = 0;
      break;
    }
  sceKernelUnlockMutex(sfx_mutex, 1);
}
boolean I_SoundIsPlaying(int handle) {
  int i;
  boolean r = false;
  if (sfx_mutex < 0)
    return false;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  for (i = 0; i < MIX_CHANNELS; i++)
    if (mix_ch[i].active && mix_ch[i].handle == handle) {
      r = true;
      break;
    }
  sceKernelUnlockMutex(sfx_mutex, 1);
  return r;
}
void I_UpdateSound(void) {}
void I_UpdateSoundParams(int handle, int vol, int sep) {
  int i;
  if (sfx_mutex < 0)
    return;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  for (i = 0; i < MIX_CHANNELS; i++) {
    if (mix_ch[i].active && mix_ch[i].handle == handle) {
      int vl, vr;
      if (sep < 0 || sep > 255)
        sep = 128;
      vr = (sep * 256) / 255;
      vl = 256 - vr;
      if (vl < 0)
        vl = 0;
      if (vl > 256)
        vl = 256;
      if (vr < 0)
        vr = 0;
      if (vr > 256)
        vr = 256;
      mix_ch[i].vol_left = (vl * vol) / 127;
      mix_ch[i].vol_right = (vr * vol) / 127;
      if (mix_ch[i].vol_left > 256)
        mix_ch[i].vol_left = 256;
      if (mix_ch[i].vol_right > 256)
        mix_ch[i].vol_right = 256;
      break;
    }
  }
  sceKernelUnlockMutex(sfx_mutex, 1);
}
void I_InitMusic(void); /* forward decl */
void I_InitSound(boolean use_sfx_prefix) {
  (void)use_sfx_prefix;
  start_audio_system();
  /* doomgeneric's S_Init does NOT call I_InitMusic,
     so we must initialize music (GENMIDI) here */
  I_InitMusic();
  debug_log("I_InitSound + I_InitMusic complete");
}
void I_ShutdownSound(void) {
  sfx_running = 0;
  if (sfx_thread_id >= 0) {
    sceKernelWaitThreadEnd(sfx_thread_id, NULL, NULL);
    sceKernelDeleteThread(sfx_thread_id);
    sfx_thread_id = -1;
  }
  if (sfx_port >= 0) {
    sceAudioOutReleasePort(sfx_port);
    sfx_port = -1;
  }
  if (sfx_mutex >= 0) {
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
  }
  if (mus_mutex >= 0) {
    sceKernelDeleteMutex(mus_mutex);
    mus_mutex = -1;
  }
  audio_ready = 0;
}
void I_BindSoundVariables(void) {}

/* MUSIC interface */
void I_InitMusic(void) {
  debug_log("I_InitMusic: loading GENMIDI");
  load_genmidi();
  debug_logf("I_InitMusic: genmidi_loaded=%d", opl_music.genmidi_loaded);
}
void I_ShutdownMusic(void) {
  int i;
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_music.playing = 0;
    for (i = 0; i < OPL_NUM_VOICES; i++) {
      opl_silence_voice(i);
      opl_music.voices[i].active = 0;
    }
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_SetMusicVolume(int v) {
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    /* Engine sends 0–127, scale to 0–15 for OPL */
    opl_music.music_volume = (v * 15) / 127;
    if (opl_music.music_volume < 1 && v > 0)
      opl_music.music_volume = 1; /* don't mute if engine wants any sound */
    if (opl_music.music_volume > 15)
      opl_music.music_volume = 15;
    debug_logf("I_SetMusicVolume: engine=%d opl=%d", v, opl_music.music_volume);
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_PauseSong(void) {
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_music.playing = 0;
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_ResumeSong(void) {
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    if (opl_music.mus_data)
      opl_music.playing = 1;
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_StopSong(void) {
  int i;
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_music.playing = 0;
    for (i = 0; i < OPL_NUM_VOICES; i++) {
      opl_silence_voice(i);
      opl_music.voices[i].active = 0;
    }
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
boolean I_MusicIsPlaying(void) {
  boolean r;
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    r = opl_music.playing ? true : false;
    sceKernelUnlockMutex(mus_mutex, 1);
  } else
    r = opl_music.playing ? true : false;
  return r;
}
void *I_RegisterSong(void *data, int len) {
  byte *d = (byte *)data, *md;
  int so, sl, i;
  debug_logf("I_RegisterSong: data=%p len=%d", data, len);
  if (!data || len < 16)
    return NULL;
  if (d[0] != 'M' || d[1] != 'U' || d[2] != 'S' || d[3] != 0x1A) {
    debug_log("I_RegisterSong: not MUS format");
    return (void *)1;
  }
  debug_log("I_RegisterSong: valid MUS data");
  sl = d[4] | (d[5] << 8);
  so = d[6] | (d[7] << 8);
  if (so >= len || so < 12)
    return (void *)1;
  if (sl <= 0 || so + sl > len)
    sl = len - so;
  md = (byte *)malloc(len);
  if (!md)
    return (void *)1;
  memcpy(md, data, len);
  if (mus_mutex >= 0)
    sceKernelLockMutex(mus_mutex, 1, NULL);
  if (opl_music.mus_data) {
    free((void *)opl_music.mus_data);
    opl_music.mus_data = NULL;
  }
  opl_music.playing = 0;
  for (i = 0; i < OPL_NUM_VOICES; i++) {
    opl_silence_voice(i);
    opl_music.voices[i].active = 0;
  }
  opl_music.mus_data = md;
  opl_music.mus_len = len;
  opl_music.score_start = so;
  opl_music.score_len = sl;
  opl_music.mus_pos = so;
  opl_music.delay_left = 0;
  opl_music.tick_counter = opl_music.tick_samples;
  opl_music.voice_age = 0;
  for (i = 0; i < 16; i++) {
    opl_music.channels[i].volume = 100;
    opl_music.channels[i].patch = 0;
    opl_music.channels[i].pitch_bend = 64;
  }
  if (mus_mutex >= 0)
    sceKernelUnlockMutex(mus_mutex, 1);
  return (void *)md;
}
void I_UnRegisterSong(void *handle) {
  int i;
  if (!handle || handle == (void *)1)
    return;
  if (mus_mutex >= 0)
    sceKernelLockMutex(mus_mutex, 1, NULL);
  opl_music.playing = 0;
  for (i = 0; i < OPL_NUM_VOICES; i++) {
    opl_silence_voice(i);
    opl_music.voices[i].active = 0;
  }
  if (opl_music.mus_data == (const byte *)handle) {
    opl_music.mus_data = NULL;
    opl_music.mus_len = 0;
  }
  if (mus_mutex >= 0)
    sceKernelUnlockMutex(mus_mutex, 1);
  free(handle);
}
void I_PlaySong(void *handle, boolean looping) {
  int i;
  debug_logf("I_PlaySong: handle=%p looping=%d", handle, looping);
  if (!handle || handle == (void *)1)
    return;
  if (mus_mutex >= 0)
    sceKernelLockMutex(mus_mutex, 1, NULL);
  if (opl_music.mus_data == (const byte *)handle) {
    opl_music.mus_pos = opl_music.score_start;
    opl_music.delay_left = 0;
    opl_music.looping = looping ? 1 : 0;
    opl_music.tick_counter = opl_music.tick_samples;
    for (i = 0; i < OPL_NUM_VOICES; i++) {
      opl_silence_voice(i);
      opl_music.voices[i].active = 0;
    }
    opl_music.playing = 1;
  }
  if (mus_mutex >= 0)
    sceKernelUnlockMutex(mus_mutex, 1);
}

/* CD stubs */
int I_CDMusInit(void) { return 0; }
void I_CDMusShutdown(void) {}
void I_CDMusUpdate(void) {}
void I_CDMusStop(void) {}
int I_CDMusPlay(int t) {
  (void)t;
  return 0;
}
void I_CDMusSetVolume(int v) { (void)v; }
int I_CDMusFirstTrack(void) { return 0; }
int I_CDMusLastTrack(void) { return 0; }
int I_CDMusTrackLength(int t) {
  (void)t;
  return 0;
}
void I_Endoom(byte *d) { (void)d; }
char *gus_patch_path = "";
int gus_ram_kb = 0;

/* MAIN */
int main(int argc, char **argv) {
  int i;
  const char *iwad = NULL;
  const char *pwad = NULL;
  const char *iwad_paths[] = {
      VITA_GAME_DATA_DIR "CHEX.WAD",
      VITA_GAME_DATA_DIR "chex.wad",
      VITA_GAME_DATA_DIR "Chex.wad",
      NULL};
  const char *pwad_paths[] = {
      VITA_GAME_DATA_DIR "CHEX2.WAD",
      VITA_GAME_DATA_DIR "chex2.wad",
      VITA_GAME_DATA_DIR "Chex2.wad",
      NULL};
  SceAppUtilInitParam ip;
  SceAppUtilBootParam bp;
  (void)argc;
  (void)argv;

  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  memset(&ip, 0, sizeof(ip));
  memset(&bp, 0, sizeof(bp));
  sceAppUtilInit(&ip, &bp);

  sceIoMkdir("ux0:/data/", 0777);
  sceIoMkdir(VITA_GAME_DATA_DIR, 0777);
  sceIoRemove(VITA_GAME_DATA_DIR "debug.log");
  debug_log("=== Chex Quest 2 Vita v1.0 ===");

  debug_log("Step 1: init_display");
  init_display();
  if (!display_ready) {
    debug_log("FATAL: no display");
    sceKernelDelayThread(3000000);
    sceKernelExitProcess(0);
    return 1;
  }
  debug_log("Step 2: display OK");
  base_time = get_ms();

  /* Set savegamedir early — D_DoomMain reads it during init */
  savegamedir = strdup(VITA_GAME_DATA_DIR);
  debug_logf("savegamedir: %s", savegamedir);

  debug_log("Step 3: searching WADs");
  for (i = 0; iwad_paths[i]; i++) {
    SceUID fd = sceIoOpen(iwad_paths[i], SCE_O_RDONLY, 0);
    if (fd >= 0) {
      sceIoClose(fd);
      iwad = iwad_paths[i];
      break;
    }
  }
  for (i = 0; pwad_paths[i]; i++) {
    SceUID fd = sceIoOpen(pwad_paths[i], SCE_O_RDONLY, 0);
    if (fd >= 0) {
      sceIoClose(fd);
      pwad = pwad_paths[i];
      break;
    }
  }

  if (!iwad) {
    debug_log("FATAL: no CHEX.WAD (or chex.wad) IWAD found!");
    sceKernelDelayThread(5000000);
    sceKernelExitProcess(0);
    return 1;
  }
  debug_logf("IWAD: %s", iwad);
  if (pwad)
    debug_logf("PWAD: %s", pwad);
  else
    debug_log(
        "WARNING: no CHEX2.WAD found — running IWAD-only (Chex Quest 1)");

  base_time = get_ms();

  debug_log("Step 4: calling doomgeneric_Create");
  if (pwad) {
    char *nargv[] = {"ChexQuest2", "-iwad",      (char *)iwad,
                     "-file",      (char *)pwad, NULL};
    doomgeneric_Create(5, nargv);
  } else {
    char *nargv[] = {"ChexQuest2", "-iwad", (char *)iwad, NULL};
    doomgeneric_Create(3, nargv);
  }

  /* D_DoomMain() overrides savegamedir — fix it for Vita */
  savegamedir = strdup(VITA_GAME_DATA_DIR);
  sceIoMkdir(VITA_GAME_DATA_DIR, 0777);
  debug_logf("savegamedir (post-init): %s", savegamedir);

  /* D_DoomMain() returned — game loop is doomgeneric_Tick() */
  debug_log("Entering main loop (doomgeneric_Tick)");
  while (1) {
    doomgeneric_Tick();
  }
  return 0;
}
