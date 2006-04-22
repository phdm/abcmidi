/*
 * abc2midi - program to convert abc files to MIDI files.
 * Copyright (C) 1999 James Allwright
 * e-mail: J.R.Allwright@westminster.ac.uk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* genmidi.c  
 * This is the code for generating MIDI output from the stored music held
 * in arrays feature, num, denom, pitch. The top-level routine is
 * writetrack(). This file is part of abc2midi.
 *
 * 14th January 1999
 * James Allwright
 *
 */
/* for Microsoft Visual C++ Ver 6 and higher */
#ifdef _MSC_VER
#define ANSILIBS
#endif

#include "abc.h"
#include "parseabc.h"
#include "queues.h"
#include "genmidi.h"
#include "midifile.h"
#include <stdio.h>
#ifdef ANSILIBS
#include <string.h>
#include <ctype.h>
#endif
/* define USE_INDEX if your C libraries have index() instead of strchr() */
#ifdef USE_INDEX
#define strchr index
#endif
#include <stdlib.h>

void   single_note_tuning_change(int midikey, float midipitch);

float ranfrac ()
{
return rand()/(float) RAND_MAX;
}

void setbeat();
static void parse_drummap(char **s);

/* global variables grouped roughly by function */

extern int lineno; /* source line being parsed */

extern char** atext;

/* Named guitar chords */
extern int chordnotes[MAXCHORDNAMES][6];
extern int chordlen[MAXCHORDNAMES];

/* general purpose storage structure */
/* these 4 arrays are used to hold the tune data */
extern int *pitch, *num, *denom;
extern featuretype *feature;
extern int notes;

extern int verbose;
extern int sf, mi;
extern int gchordvoice, wordvoice, drumvoice, dronevoice;
extern int gchordtrack, drumtrack, dronetrack;
extern int nsplits;

/* Part handling */
extern struct vstring part;
int parts, partno, partlabel;
int part_start[26], part_count[26];
long introlen, lastlen, partlen[26];
int partrepno;
/* int additive;  not supported any more [SS] 2004-10-08*/
int err_num, err_denom;

extern int voicesused;

/* Tempo handling (Q: field) */
extern long tempo;
extern int time_num, time_denom; /* time sig. for the tune */
extern int mtime_num, mtime_denom; /* current time sig. when generating MIDI */
int div_factor;
int division = DIV;

long delta_time; /* time since last MIDI event */
long tracklen, tracklen1;

/* output file generation */
extern int ntracks;

/* bar length checking */
int bar_num, bar_denom, barno, barsize;
int b_num, b_denom;
extern int barchecking;


/* time signature after header processed */
extern int header_time_num,header_time_denom;



/* generating MIDI output */
int beat;
int loudnote, mednote, softnote;
int beataccents;
int velocity_increment = 10; /* for crescendo and decrescendo */
char beatstring[100]; 
int nbeats;
int channel, program;
#define MAXCHANS 16
int channels[MAXCHANS + 3];
int  transpose;
int  global_transpose=0;

/* karaoke handling */
extern int karaoke, wcount;
int kspace;
char* wordlineptr;
extern char** words;
int thismline, thiswline, windex, thiswfeature;
int wordlineplace;
int nowordline;
int waitforbar;
int wlineno, syllcount;
int lyricsyllables, musicsyllables;
/* the following are booleans to select features in current track */
int wordson, noteson, gchordson, temposon, drumson, droneon;

/* Generating accompaniment */
int gchords, g_started;
int basepitch, inversion, chordnum;
int gchordnotes[6],gchordnotes_size;

struct notetype {
  int base;
  int chan;
  int vel;
};
struct notetype gchord, fun;
int g_num, g_denom;
int g_next;
char gchord_seq[40];
int gchord_len[40];
int g_ptr;


struct dronestruct {
   int chan;  /* MIDI channel assigned to drone */
   int event; /* stores time in MIDI pulses when last drone event occurred*/
   int prog;  /* MIDI program (instrument) to use for drone */
   int pitch1;/* MIDI pitch of first drone tone */
   int vel1;  /* MIDI velocity (loudness) of first drone tone */
   int pitch2;/* MIDI pitch of second drone tone */
   int vel2;  /* MIDI velocity (loudnress) of second drone tone */
} drone;



/* Generating drum track */
int drum_num, drum_denom;
char drum_seq[40];
int drum_len[40];
int drum_velocity[40], drum_program[40];
int drum_ptr, drum_on;

int notecount=0;  /* number of notes in a chord [ABC..] */
int notedelay=10;  /* time interval in MIDI ticks between */
                   /*  start of notes in chord */
int chordattack=0;
int staticnotedelay=10;  /* introduced to handle !arpeggio! */
int staticchordattack=0;
int totalnotedelay=0; /* total time delay introduced */


/* channel 10 drum handling */
int drum_map[256];

void reduce(a, b)
/* elimate common factors in fraction a/b */
int *a, *b;
{
  int sign;
  int t, n, m;

  if (*a < 0) {
    sign = -1;
    *a = -*a;
  } else {
    sign = 1;
  };
  /* find HCF using Euclid's algorithm */
  if (*a > *b) {
    n = *a;
    m = *b;
  } else {
    n = *b;
    m = *a;
  };
  while (m != 0) {
    t = n % m;
    n = m;
    m = t;
  };
  *a = (*a/n)*sign;
  *b = *b/n;
}

void addunits(a, b)
/* add a/b to the count of units in the bar */
int a, b;
{
  bar_num = bar_num*(b*b_denom) + (a*b_num)*bar_denom;
  bar_denom = bar_denom * (b*b_denom);
  reduce(&bar_num, &bar_denom);
}

void configure_gchord()
/* creates a list of notes to played as chord for
 * a specific guitar chord. Most of the code figures out
 * how to order the notes when inversions are encountered.
*/
{
 int j;
 int inchord, note;
 gchordnotes_size = 0;

inchord = 0;
if (inversion != -1) {
/* try to match inversion with basepitch+chordnotes.. */
  for (j=0; j<chordlen[chordnum]; j++) {
       if ((basepitch + chordnotes[chordnum][j]) % 12 == inversion % 12) {
            inchord = j;
	    };
       };
  if ((inchord == 0) && (inversion > basepitch)) {
        inversion = inversion - 12;
        gchordnotes[gchordnotes_size] = inversion+gchord.base;
        gchordnotes_size++;
        };
   };
for (j=0; j<chordlen[chordnum]; j++) {
    note = basepitch + chordnotes[chordnum][j]; 
   if (j < inchord) 
    note += 12;
   gchordnotes[gchordnotes_size] = gchord.base+note;
   gchordnotes_size++;
   };
}



void set_gchords(s)
char* s;
/* set up a string which indicates how to generate accompaniment from */
/* guitar chords (i.e. "A", "G" in abc). */
/* called from dodeferred(), startfile() and setbeat() */
{
  int seq_len;
  char* p;
  int j;

  p = s;
  j = 0;
  seq_len = 0;
    while ((strchr("zcfbghijGHIJ", *p) != NULL) && (j <39)) {
    if (*p == 0) break;
    gchord_seq[j] = *p;
    p = p + 1;
    if ((*p >= '0') && (*p <= '9')) {
      gchord_len[j] = readnump(&p);
    } else {
      gchord_len[j] = 1;
    };
    seq_len = seq_len + gchord_len[j];
    j = j + 1;
  };
  if (seq_len == 0) {
    event_error("Bad gchord");
    gchord_seq[0] = 'z';
    gchord_len[0] = 1;
    seq_len = 1;
  };
  gchord_seq[j] = '\0';
  if (j == 39) {
    event_error("Sequence string too long");
  };
  /* work out unit delay in 1/4 notes*/
  g_num = mtime_num * 4;
  g_denom = mtime_denom * seq_len;
  reduce(&g_num, &g_denom);
/*  printf("%s  %d %d\n",s,g_num,g_denom); */
}

void set_drums(s)
char* s;
/* set up a string which indicates drum pattern */
/* called from dodeferred() */
{
  int seq_len, count, drum_hits;
  char* p;
  int i, j, place;

  p = s;
  count = 0;
  drum_hits = 0;
  seq_len = 0;
  while (((*p == 'z') || (*p == 'd')) && (count<39)) {
    if (*p == 'd') {
      drum_hits = drum_hits + 1;
    };
    drum_seq[count] = *p;
    p = p + 1;
    if ((*p >= '0') && (*p <= '9')) {
      drum_len[count] = readnump(&p);
    } else {
      drum_len[count] = 1;
    };
    seq_len = seq_len + drum_len[count];
    count = count + 1;
  };
  drum_seq[count] = '\0';
  if (seq_len == 0) {
    event_error("Bad drum sequence");
    drum_seq[0] = 'z';
    drum_len[0] = 1;
    seq_len = 1;
  };
  if (count == 39) {
    event_error("Drum sequence string too long");
  };
  /* look for program and velocity specifiers */
  for (i = 0; i<count; i++) {
    drum_program[i] = 35;
    drum_velocity[i] = 80;
  };
  skipspace(&p);
  i = 0;
  place = 0;
  while (isdigit(*p)) {
    j = readnump(&p);
    if (i < drum_hits) {
      while (drum_seq[place] != 'd') {
        place = place + 1;
      };
      if (j > 127) {
        event_error("Drum program must be in the range 0-127");
      } else {
        drum_program[place] = j;
      };
      place = place + 1;
    } else {
      if (i < 2*count) {
        if (i == drum_hits) {
          place = 0;
        };
        while (drum_seq[place] != 'd') {
          place = place + 1;
        };
        if ((j < 1) || (j > 127)) {
          event_error("Drum velocity must be in the range 1-127");
        } else {
          drum_velocity[place] = j;
        };
        place = place + 1;
      };
    };
    i = i + 1;
    skipspace(&p);
  };
  if (i > 2*drum_hits) {
    event_error("Too many data items for drum sequence");
  };
  /* work out unit delay in 1/4 notes*/
  drum_num = mtime_num * 4;
  drum_denom = mtime_denom * seq_len;
  reduce(&drum_num, &drum_denom);
}

static void checkbar(pass)
/* check to see we have the right number of notes in the bar */
int pass;
{
  char msg[80];
  
  if (barchecking) {
    /* only generate these errors once */
    if (noteson && (partrepno == 0)) {
      /* allow zero length bars for typesetting purposes */
      if ((bar_num-barsize*(bar_denom) != 0) &&
          (bar_num != 0) && ((pass == 2) || (barno != 0))) {
        sprintf(msg, "Bar %d has %d", barno, bar_num);
        if (bar_denom != 1) {
          sprintf(msg+strlen(msg), "/%d", bar_denom);
        };
        sprintf(msg+strlen(msg), " units instead of %d", barsize);
        if (pass == 2) {
          strcat(msg, " in repeat");
        };
        event_warning(msg);
      };
    };
  };
  if (bar_num > 0) {
    barno = barno + 1;
  };
  bar_num = 0;
  bar_denom = 1;
  /* zero place in gchord sequence */
  if (gchordson) {
    g_ptr = 0;
    addtoQ(0, g_denom, -1, g_ptr, 0);
  };
  if (drumson) {
    drum_ptr = 0;
    addtoQ(0, drum_denom, -1, drum_ptr, 0);
  };
}

static void softcheckbar(pass)
/* allows repeats to be in mid-bar */
int pass;
{
  if (barchecking) {
    if ((bar_num-barsize*(bar_denom) >= 0) || (barno <= 0)) {
      checkbar(pass);
    };
  };
}

static void save_state(vec, a, b, c, d, e)
/* save status when we go into a repeat */
int vec[5];
int a, b, c, d, e;
{
  vec[0] = a;
  vec[1] = b;
  vec[2] = c;
  vec[3] = d;
  vec[4] = e;
}

static void restore_state(vec, a, b, c, d, e)
/* restore status when we loop back to do second repeat */
int vec[5];
int *a, *b, *c, *d, *e;
{
  *a = vec[0];
  *b = vec[1];
  *c = vec[2];
  *d = vec[3];
  *e = vec[4];
}

static int findchannel()
/* work out next available channel */
{
  int j;

  j = 0;
  while ((j<MAXCHANS+3) && (channels[j] != 0)) {
    j = j + 1;
  };
  if (j >= MAXCHANS + 3) {
    event_error("Too many channels required");
    j = 0;
  };
  return (j);
}

static void fillvoice(partno, xtrack, voice)
/* check length of this voice at the end of a part */
/* if it is zero, extend it to the correct length */
int partno, xtrack, voice;
{
  char msg[100];
  long now;

  now = tracklen + delta_time;
  if (partlabel == -1) {
    if (xtrack == 1) {
      introlen = now;
    } else {
      if (now == 0) {
        delta_time = delta_time + introlen;
        now = introlen;
      } else {
        if (now != introlen) {
          sprintf(msg, "Time 0-%ld voice %d, has length %ld", 
                  introlen, voice, now);
          event_error(msg);
        };
      };
    };
  } else {
    if (xtrack == 1) {
      partlen[partlabel] = now - lastlen;
    } else {
      if (now - lastlen == 0) {
        delta_time = delta_time + partlen[partlabel];
        now = now + partlen[partlabel];
      } else {
        if (now - lastlen != partlen[partlabel]) {
          sprintf(msg, "Time %ld-%ld voice %d, part %c has length %ld", 
                  lastlen, lastlen+partlen[partlabel], voice, 
                  (char) (partlabel + (int) 'A'), 
                  now-lastlen);
          event_error(msg);
        };
      };
    };
  };
  lastlen = now;
}

static int findpart(j)
int j;
/* find out where next part starts and update partno */
{
  int place;

  place = j;
  partno = partno + 1;
  if (partno < parts) {
    partlabel = (int)part.st[partno] - (int)'A';
  }
  while ((partno < parts) &&
         (part_start[partlabel] == -1)) {
    event_error("Part not defined");
    partno = partno + 1;
    if (partno < parts) {
      partlabel = (int)part.st[partno] - (int)'A';
    }
  };
  if (partno >= parts) {
    place = notes;
  } else {
    partrepno = part_count[partlabel];
    part_count[partlabel]++;
    place = part_start[partlabel];
  };
  if (verbose) {
    if (partno < parts) {
      printf("Doing part %c number %d of %d\n", part.st[partno], partno, parts);
    };
  };
  return(place);
}

static int partbreak(xtrack, voice, place)
/* come to part label in note data - check part length, then advance to  */
/* next part if there was a P: field in the header */
int xtrack, voice, place;
{
  int newplace;

  newplace = place;
  if (xtrack > 0) {
    fillvoice(partno, xtrack, voice);
  };
  if (parts != -1) {
    /* go to next part label */
    newplace = findpart(newplace);
  };
  partlabel = (int) pitch[newplace] - (int)'A';
  return(newplace);
}

static int findvoice(initplace, voice, xtrack)
/* find where next occurence of correct voice is */
int initplace;
int voice, xtrack;
{
  int foundvoice;
  int j;

  foundvoice = 0;
  j = initplace;
  while ((j < notes) && (foundvoice == 0)) {
    if (feature[j] == PART) {
      j = partbreak(xtrack, voice, j);
      if (voice == 1) {
        foundvoice = 1;
      } else {
        j = j + 1;
      };
    } else {
      if ((feature[j] == VOICE) && (pitch[j] == voice)) {
        foundvoice = 1;
      } else {
        j = j + 1;
      };
    };
  };
  return(j);
}

static void text_data(s)
/* write text event to MIDI file */
char* s;
{   
  mf_write_meta_event(delta_time, text_event, s, strlen(s));
  tracklen = tracklen + delta_time;
  delta_time = 0L;
}

static void karaokestarttrack (track)
int track;
/* header information for karaoke track based on w: fields */
{
  int j;
  int done;
  char atitle[200];

/*
 *  Print Karaoke file headers in track 0.
 *  @KMIDI KARAOKE FILE - Karaoke midi file marker)
 */
   if (track == 0)
   {
      text_data("@KMIDI KARAOKE FILE");
   }
/*
 *  Name track 2 "Words" for the lyrics track.
 *  @LENGL - language
 *  Print @T information.
 *  1st @T line signifies title.
 *  2nd @T line signifies author.
 *  3rd @T line signifies copyright.
 */
   if (track == 2)
   {
      mf_write_meta_event(0L, sequence_name, "Words", 5);
      kspace = 0;
      text_data("@LENGL");
      strcpy(atitle, "@T");
   }
   else 
/*
 *  Write name of song as sequence name in track 0 and as track 1 name. 
 *  Print general information about the file using @I marker.
 *  Add to tracks 0 and 1 for various Karaoke (and midi) players to find.
 */
      strcpy(atitle, "@I");

  j = 0;
  done = 3;
     
  while ((j < notes) && (done > 0))
  {
     j = j+1;
     if (feature[j] == TITLE) {
        if (track != 2)
           mf_write_meta_event(0L, sequence_name, atext[pitch[j]], strlen (atext[pitch[j]]));
        strcpy(atitle+2, atext[pitch[j]]);
        text_data(atitle);
        done--;
     }
     if (feature[j] == COMPOSER) {
        strcpy(atitle+2, atext[pitch[j]]);
        text_data(atitle);
        done--;
     }     
     if (feature[j] == COPYRIGHT) {
        strcpy(atitle+2, atext[pitch[j]]);
        text_data(atitle);
        done--;
     }
  }
}

static int findwline(startline)
int startline;
/* Find next line of lyrics at or after startline. */
{
  int place;
  int done;
  int newwordline;
  int inwline, extending;
  int versecount, target;

  /* printf("findwline called with %d\n", startline); */
  done = 0;
  inwline = 0;
  nowordline = 0;
  newwordline = -1;
  target = partrepno;
  if (startline == thismline) {
    versecount = 0;
    extending = 0;
  } else {
    versecount = target;
    extending = 1;
  };
  if (thismline == -1) {
    event_error("First lyrics line must come after first music line");
  } else {
    place = startline + 1;
    /* search for corresponding word line */
    while ((place < notes) && (!done)) {
      switch (feature[place]) {
      case WORDLINE:
        inwline = 1;
        /* wait for words for this pass */
        if (versecount == target) {
          thiswfeature = place;
          newwordline = place;
          windex = pitch[place];
          wordlineplace = 0;
          done = 1;
        };
        break;
      case WORDSTOP:
        if (inwline) {
          versecount = versecount + 1;
        };
        inwline = 0;
        /* stop if we are part-way through a lyric set */
        if  (extending) {
          done = 1;
        };
        break;
      case PART:
        done = 1;
        break;
      case VOICE:
        done = 1;
        break;
      case MUSICLINE:
        done = 1;
        break;
      default:
        break;
      };
      place = place + 1;
      if (done && (newwordline == -1) && (versecount > 0) && (!extending)) {
        target = partrepno % versecount ;
        versecount = 0;
        place = startline+1;
        done = 0;
        inwline = 0;
      };
    };
    if (newwordline == -1) {
      /* remember that we couldn't find lyrics */
      nowordline = 1;
      if (lyricsyllables == 0) {
        event_warning("Line of music without lyrics");
      };
    };  
  };
  return(newwordline);
}



static int getword(place, w)
/* picks up next syllable out of w: field.
 * It strips out all the control codes ~ - _  * in the
 * words and sends each syllable out to the Karoake track.
 * Using the place variable, it loops through each character
 * in the word until it encounters a space or next control
 * code. The syllstatus variable controls the loop. After,
 * the syllable is sent, it then positions the place variable
 * to the next syllable or control code.
 * inword   --> grabbing the characters in the syllable and
 *             putting them into syllable for output.
 * postword --> finished grabbing all characters
 * foundnext--> ready to repeat process for next syllable
 * empty    --> between syllables.
 *
 * The variable i keeps count of the number of characters
 * inserted into the syllable[] char for output to the
 * karoake track. The kspace variables signals that a
 * space was encountered.
 */
int* place;
int w;
{
  char syllable[200];
  char c;
  int i;
  int syllcount;
  enum {empty, inword, postword, foundnext, failed} syllstatus;

  i = 0;
  syllcount = 0;
  if (w >= wcount) {
    syllable[i] = '\0';
    return ('\0');
  };
  if (*place == 0) {
    if ((w % 2) == 0) {
      syllable[i] = '/'; 
    } else {
      syllable[i] = '\\'; 
    };
    i = i + 1;
  };
  if (kspace) {
    syllable[i] = ' ';
    i = i + 1;
  };
  syllstatus = empty;
  c = *(words[w]+(*place));
  while ((syllstatus != postword) && (syllstatus != failed)) {
    syllable[i] = c;
    switch(c) {
    case '\0':
      if (syllstatus == empty) {
        syllstatus = failed;
      } else {
        syllstatus = postword;
        kspace = 1;
      };
      break;
    case '~':
      syllable[i] = ' ';
      syllstatus = inword;
      *place = *place + 1;
      i = i + 1;
      break;
    case '\\':
      if (*(words[w]+(*place+1)) == '-') {
        syllable[i] = '-';
        syllstatus = inword;
        *place = *place + 2;
        i = i + 1;
      } else {
        /* treat like plain text */
        *place = *place + 1;
        if (i>0) {
          syllstatus = inword;
          i = i + 1;
        };
      };
      break;
    case ' ':
      if (syllstatus == empty) {
        *place = *place + 1;
      } else {
        syllstatus = postword;
        *place = *place + 1;
        kspace = 1;
      };
      break;
    case '-':
      if (syllstatus == inword) {
        syllstatus = postword;
        *place = *place + 1;
        kspace = 0;
      } else {
        *place = *place + 1;
      };
      break;
    case '*':
      if (syllstatus == empty) {
        syllstatus = postword;
        *place = *place + 1;
      } else {
        syllstatus = postword;
      };
      break;
    case '_':
      if (syllstatus == empty) {
        syllstatus = postword;
        *place = *place + 1;
      } else {
        syllstatus = postword;
      };
      break;
    case '|':
      if (syllstatus == empty) {
        syllstatus = failed;
        *place = *place + 1;
      } else {
        syllstatus = postword;
        *place = *place + 1;
        kspace = 1;
      };
      waitforbar = 1;
      break;
    default:
      /* copying plain text character across */
      /* first character must be alphabetic */
      if ((i>0) || isalpha(syllable[0])) {
        syllstatus = inword;
        i = i + 1;
      };
      *place = *place + 1;
      break;
    };
    c = *(words[w]+(*place));
  };
  syllable[i] = '\0';
  if (syllstatus == failed) {
    syllcount = 0;
  } else {
    syllcount = 1;
    if (strlen(syllable) > 0) {
      text_data(syllable);
    };
  };
  /* now deal with anything after the syllable */
  while ((syllstatus != failed) && (syllstatus != foundnext)) {
    c = *(words[w]+(*place));
    switch (c) {
    case ' ':
      *place = *place + 1;
      break;
    case '-':
      *place = *place + 1;
      kspace = 0;
      break;
    case '\t':
      *place = *place + 1;
      break;
    case '_':
      *place = *place + 1;
      syllcount = syllcount + 1;
      break;
    case '|':
      if (waitforbar == 0) {
        *place = *place + 1;
        waitforbar = 1;
      } else {
        syllstatus = failed;
      };
      break;
    default:
      syllstatus = foundnext;
      break;
    };  
  };
  return(syllcount);
}




static void write_syllable(place)
int place;
/* Write out a syllable. This routine must check that it has a line of 
 * lyrics and find one if it doesn't have one. The function is called
 * for each note encountered in feature[j] when the global variable
 * wordson is set. The function keeps count of the number of notes
 * in the music and words in the lyrics so that we can check that 
 * they match at the end of a music line. When waitforbar is set
 * by getword, the function  does nothing (allows feature[j]
 * to advance to next feature) until waitforbar is set to 0
 * (by writetrack).                                                 */
{
  musicsyllables = musicsyllables + 1;
  if (waitforbar) {
    lyricsyllables = lyricsyllables + 1;
    return;
  };
  if ((!nowordline) && (!waitforbar)) {
    if (thiswline == -1) {
      thiswline = findwline(thismline);
    };
    if (!nowordline) {
      int done;

      done = 0;
      while (!done) {
        if (syllcount == 0) {
          /* try to get fresh word */
          syllcount = getword(&wordlineplace, windex);
          if (waitforbar) {
            done = 1;
            if (syllcount == 0) {
              lyricsyllables = lyricsyllables + 1;
            };
          } else {
            if (syllcount == 0) {
              thiswline = findwline(thiswline);
              if (thiswline == -1) {
                done = 1;
              };
            };
          };
        };
        if (syllcount > 0) {
          /* still finishing off a multi-syllable item */
          syllcount = syllcount - 1;
          lyricsyllables = lyricsyllables + 1;
          done = 1;
        };
      };
    };
  };
}

static void checksyllables()
/* check line of lyrics matches line of music. It grabs
 * all remaining syllables in the lyric line counting
 * them as it goes along. It then checks that the number
 * of syllables matches the number of notes for that music
 * line
*/
{
  int done;
  int syllcount;
  char msg[80];

  /* first make sure all lyric syllables are read */
  done = 0;
  while (!done) {
    syllcount = getword(&wordlineplace, windex);
    if (syllcount > 0) {
      lyricsyllables = lyricsyllables + syllcount;
    } else {
      thiswline = findwline(thiswline);
      if (thiswline == -1) {
        done = 1;
      } else {
        windex = pitch[thiswline];
      };
    };
  };
  if (lyricsyllables != musicsyllables) {
    sprintf(msg, "Verse %d mismatch;  %d syllables in music %d in lyrics",
                partrepno+1, musicsyllables, lyricsyllables);
    event_error(msg);
  };
  lyricsyllables = 0;
  musicsyllables = 0;
}

static int inlist(place, passno)
int place;
int passno;
/* decide whether passno matches list/number for variant section */
/* handles representation of [X in the abc */
{
  int a, b;
  char* p;
  int found;
  char msg[100];

  /* printf("passno = %d\n", passno); */
  if (denom[place] != 0) {
    /* special case when this is variant ending for only one pass */
    if (passno == denom[place]) {
      return(1);
    } else {
      return(0);
    };
  } else {
    /* must scan list */
    p = atext[pitch[place]];
    found = 0;
    while ((found == 0) && (*p != '\0')) {
      if (!isdigit(*p)) {
        sprintf(msg, "Bad variant list : %s", atext[pitch[place]]);
        event_error(msg);
        found = 1;
      };
      a = readnump(&p);
      if (passno == a) {
        found = 1;
      };
      if (*p == '-') {
        p = p + 1;
        b = readnump(&p);
        if ((passno >= a) && (passno <= b)) {
          found = 1;
        };
      };
      if (*p == ',') {
        p = p + 1;
      };
    };
    return(found);
  };
}

void set_meter(n, m)
/* set up variables associated with meter */
int n, m;
{
  mtime_num = n;
  mtime_denom = m;
  time_num =n; 
  time_denom=m; 
  /* set up barsize */
  barsize = n;
  if (barsize % 3 == 0) {
    beat = 3;
  } else {
    if (barsize % 2 == 0) {
      beat = 2;
    } else {
      beat = barsize;
    };
  };
  /* correction factor to make sure we count in the right units */
  if (m > 4) {
    b_num = m/4;
    b_denom = 1;
  } else {
   b_num = 1;
   b_denom = 4/m;
  };
}

static void write_meter(n, m)
/* write meter to MIDI file */
int n, m;
{
  int t, dd;
  char data[4];

  set_meter(n, m);

  dd = 0;
  t = m;
  while (t > 1) {
    dd = dd + 1;
    t = t/2;
  };
  data[0] = (char)n;
  data[1] = (char)dd;
  if (n%2 == 0) {
    data[2] = (char)(24*2*n/m);
  } else {
    data[2] = (char)(24*n/m);
  };
  data[3] = 8;
  mf_write_meta_event(0L, time_signature, data, 4);
}

static void write_keysig(sf, mi)
/* Write key signature to MIDI file */
int sf, mi;
{
  char data[2];

  data[0] = (char) (0xff & sf);
  data[1] = (char) mi;
  mf_write_meta_event(0L, key_signature, data, 2);
}

static void midi_noteon(delta_time, pitch, chan, vel)
/* write note on event to MIDI file */
long delta_time;
int pitch, chan, vel;
{
  char data[2];
#ifdef NOFTELL
  extern int nullpass;
#endif
  if (chan == 9) data[0] = (char) drum_map[pitch];
  else           data[0] = (char) pitch;
  data[1] = (char) vel;
  if (channel >= MAXCHANS) {
    event_error("Channel limit exceeded\n");
  } else {
    mf_write_midi_event(delta_time, note_on, chan, data, 2);
#ifdef NOFTELL
    if (nullpass != 1) {
      channels[chan] = 1;
    };
#else
    channels[chan] = 1;
#endif
  };
}

void midi_noteoff(delta_time, pitch, chan)
/* write note off event to MIDI file */
long delta_time;
int pitch, chan;
{
  char data[2];

  if (chan == 9) data[0] = (char) drum_map[pitch];
  else           data[0] = (char) pitch;
  data[1] = (char) 0;
  if (channel >= MAXCHANS) {
    event_error("Channel limit exceeded\n");
  } else {
    mf_write_midi_event(delta_time, note_off, chan, data, 2);
  };
}

static void noteon_data(pitch, channel, vel)
/* write note to MIDI file and adjust delta_time */
int pitch, channel, vel;
{
  midi_noteon(delta_time, pitch, channel, vel);
  tracklen = tracklen + delta_time;
  delta_time = 0L;
}

static void noteon(n)
/* compute note data and call noteon_data to write MIDI note event */
int n;
{
  int i, vel;

  /* set velocity */
  if(beataccents == 0) 
    vel = mednote;
  else if (nbeats > 0) {
      if ((bar_num*nbeats)%(bar_denom*barsize) != 0) {
        /* not at a defined beat boundary */
        vel = softnote;
      } else {
        /* find place in beatstring */
        i = ((bar_num*nbeats)/(bar_denom*barsize))%nbeats;
        switch(beatstring[i]) {
        case 'f':
        case 'F':
          vel = loudnote;
          break;
        case 'm':
        case 'M':
          vel = mednote;
          break;
        default:
        case 'p':
        case 'P':
          vel = softnote;
          break;
        };
      };
     } else {
      /* no beatstring - use beat algorithm */
      if (bar_num == 0) {
          vel = loudnote;
     } else {
        if ((bar_denom == 1) && ((bar_num % beat) == 0)) {
          vel = mednote;
     } else {
          vel = softnote;
        };
      };
    }
  if (channel == 9) noteon_data(pitch[n],channel,vel);
  else noteon_data(pitch[n] + transpose + global_transpose, channel, vel);
}

static void write_program(p, channel)
/* write 'change program' (new instrument) command to MIDI file */
int p, channel;
{
  char data[1];

  data[0] = p;
  if (channel >= MAXCHANS) {
    event_error("Channel limit exceeded\n");
  } else {
    mf_write_midi_event(delta_time, program_chng, channel, data, 1);
  };
  tracklen = tracklen + delta_time;
  delta_time = 0L;
}

static void write_event(event_type, channel, data, n)
/* write MIDI special event such as control_change or pitch_wheel */
int event_type;
int channel, n;
char data[];
{
  if (channel >= MAXCHANS) {
    event_error("Channel limit exceeded\n");
  } else {
    mf_write_midi_event(delta_time, event_type, channel, data, n);
  };
}

static char *select_channel(chan, s)
char *s;
int *chan;
/* used by dodeferred() to set channel to be used */
/* reads 'bass', 'chord' or nothing from string pointed to by p */
{
  char sel[40];
  char *p;

  p = s;
  skipspace(&p);
  *chan = channel;
  if (isalpha(*p)) {
    readstr(sel, &p, 40);
    skipspace(&p);
    if (strcmp(sel, "bass") == 0) {
      *chan = fun.chan;
    };
    if (strcmp(sel, "chord") == 0) {
      *chan = gchord.chan;
    };
  };
  return(p);
}

static void dodeferred(s,noteson)
/* handle package-specific command which has been held over to be */
/* interpreted as MIDI is being generated */
char* s;
int noteson;
{
  char* p;
  char command[40];
  int done;
  int val;

  p = s;
  skipspace(&p);
  readstr(command, &p, 40);
  skipspace(&p);
  done = 0;
  if (strcmp(command, "program") == 0) {
    int chan, prog;

    skipspace(&p);
    prog = readnump(&p);
    chan = channel;
    skipspace(&p);
    if ((*p >= '0') && (*p <= '9')) {
      chan = prog - 1;
      prog = readnump(&p);
    };
    if (noteson) {
      write_program(prog, chan);
    };
    done = 1;
  };
  if (strcmp(command, "gchord") == 0) {
    set_gchords(p);
    done = 1;
  };
  if (strcmp(command, "drum") == 0) {
    set_drums(p);
    done = 1;
  };
  if ((strcmp(command, "chordprog") == 0))  {
    int prog;

    prog = readnump(&p);
    if (gchordson) {
      write_program(prog, gchord.chan);
    };
    done = 1;
  };
  if ((strcmp(command, "bassprog") == 0)) {
    int prog;

    prog = readnump(&p);
    if (gchordson) {
      write_program(prog, fun.chan);
    };
    done = 1;
  };
  if (strcmp(command, "chordvol") == 0) {
    gchord.vel = readnump(&p);
    done = 1;
  };
  if (strcmp(command, "bassvol") == 0) {
    fun.vel = readnump(&p);
    done = 1;
  };
  if (strcmp(command, "drone") == 0) {
    skipspace(&p);
    val = readnump(&p);
    if (val > 0) drone.prog = val;
    skipspace(&p);
    val = readnump(&p);
    if (val >0 ) drone.pitch1 = val;
    skipspace(&p);
    val = readnump(&p);
    if (val >0)  drone.pitch2 = val;
    skipspace(&p);
    val = readnump(&p);
    if (val >0) drone.vel1 = val;
    skipspace(&p);
    val = readnump(&p);
    if (val >0) drone.vel2 = val;
    if (drone.prog > 127) event_error("drone prog must be in the range 0-127");
    if (drone.pitch1 >127) event_error("drone pitch1 must be in the range 0-127");
    if (drone.vel1 >127) event_error("drone vel1 must be in the range 0-127");
    if (drone.pitch2 >127) event_error("drone pitch1 must be in the range 0-127");
    if (drone.vel2 >127) event_error("drone vel1 must be in the range 0-127");
    done = 1;
  }

  if (strcmp(command, "beat") == 0) {
    skipspace(&p);
    loudnote = readnump(&p);
    skipspace(&p);
    mednote = readnump(&p);
    skipspace(&p);
    softnote = readnump(&p);
    skipspace(&p);
    beat = readnump(&p);
    if (beat == 0) {
      beat = barsize;
    };
    done = 1;
  };

  if (strcmp(command, "beatmod") == 0) {
    skipspace(&p);
    velocity_increment = readsnump(&p);
    loudnote += velocity_increment;
    mednote  += velocity_increment;
    softnote += velocity_increment;
    if (loudnote > 127) loudnote = 127;
    if (mednote  > 127) mednote = 127;
    if (softnote > 127) softnote = 127;
    if (loudnote < 0)   loudnote = 0;
    if (mednote  < 0)   mednote = 0;
    if (softnote < 0)   softnote = 0;
    done = 1;
    }

  if (strcmp(command, "beatstring") == 0) {
    int count;

    skipspace(&p);
    count = 0;
    while ((count < 99) && (strchr("fFmMpP", *p) != NULL)) {
      beatstring[count] = *p;
      count = count + 1;
      p = p + 1;
    };
    beatstring[count] = '\0';
    if (strlen(beatstring) == 0) {
      event_error("beatstring expecting string of 'f', 'm' and 'p'");
    }
    nbeats = strlen(beatstring);
    done = 1;
  }
  if (strcmp(command, "control") == 0) {
    int chan, n, datum;
    char data[20];

    p = select_channel(&chan, p);
    n = 0;
    while ((n<20) && (*p >= '0') && (*p <= '9')) {
      datum = readnump(&p);
      if (datum > 127) {
        event_error("data must be in the range 0 - 127");
        datum = 0;
      };
      data[n] = (char) datum;
      n = n + 1;
      skipspace(&p);
    };
    write_event(control_change, chan, data, n);
    done = 1;
  };

  if( strcmp(command, "beataccents") == 0) {
    beataccents = 1;
    done = 1;
  }
  if( strcmp(command, "nobeataccents") == 0) {
    beataccents = 0;
    done = 1;
  }

  if (strcmp(command,"portamento") == 0) {
   int chan, datum;
   char data[4];
   p = select_channel(&chan, p);
   data[0] = 65;
   data[1] = 127;
   /* turn portamento on */
   write_event(control_change, chan, data, 2);
   data[0] = 5; /* coarse portamento */
   datum = readnump(&p);
   if (datum > 63) {
        event_error("data must be in the range 0 - 63");
        datum = 0;
      };
   data[1] =(char) datum;
   write_event(control_change, chan, data, 2);
   done = 1;
   } 

  if (strcmp(command,"noportamento") == 0) {
   int chan;
   char data[4];
   p = select_channel(&chan, p);
   data[0] = 65;
   data[1] = 0;
   /* turn portamento off */
   write_event(control_change, chan, data, 2);
   done = 1;
   }

  if (strcmp(command, "pitchbend") == 0) {
    int chan, n, datum;
    char data[2];

    p = select_channel(&chan, p);
    n = 0;
    data[0] = 0;
    data[1] = 0;
    while ((n<2) && (*p >= '0') && (*p <= '9')) {
      datum = readnump(&p);
      if (datum > 255) {
        event_error("data must be in the range 0 - 255");
        datum = 0;
      };
      data[n] = (char) datum;
      n = n + 1;
      skipspace(&p);
    };
/* don't write pitchbend in the header track [SS] 2005-04-02 */
    if (noteson) {
       write_event(pitch_wheel, chan, data, 2);
       tracklen = tracklen + delta_time;
       delta_time = 0L;
       } 
    done = 1;
  };

  if (strcmp(command, "snt") == 0) {  /*single note tuning */
    int midikey;
    float midipitch;
    midikey = readnump(&p);
    sscanf(p,"%f", &midipitch);
    single_note_tuning_change(midikey,  midipitch);
    done = 1;
    }
   

  if (strcmp(command,"chordattack") == 0) {
    staticnotedelay = readnump(&p);
    notedelay = staticnotedelay;
    done = 1;
  };
  if (strcmp(command,"randomchordattack") == 0) {
    staticchordattack = readnump(&p);
    chordattack = staticchordattack;
    done = 1;
  };
  if (strcmp(command,"drummap") == 0) {
    parse_drummap(&p);
    done = 1;
  };


  if (done == 0) {
    char errmsg[80];
    sprintf(errmsg, "%%%%MIDI command \"%s\" not recognized",command);
    event_error(errmsg);
  };
}

static void delay(a, b, c)
/* wait for time a/b */
int a, b, c;
{
  int dt;

  dt = (div_factor*a)/b + c;
  err_num = err_num * b + ((div_factor*a)%b)*err_denom;
  err_denom = err_denom * b;
  reduce(&err_num, &err_denom);
  dt = dt + (err_num/err_denom);
  err_num = err_num%err_denom;
  timestep(dt, 0);
}

static void save_note(num, denom, pitch, chan, vel)
/* output 'note on' queue up 'note off' for later */
int num, denom;
int pitch, chan, vel;
{
/* don't transpose drum channel */
  if(chan == 9) {noteon_data(pitch, chan, vel);
                addtoQ(num, denom, pitch, chan, -1);}
  else  {noteon_data(pitch + transpose + global_transpose, chan, vel);
        addtoQ(num, denom, pitch + transpose + global_transpose, chan, -1);}
}




void dogchords(i)
/* generate accompaniment notes */
int i;
{
int j;
  if ((i == g_ptr) && (g_ptr < (int) strlen(gchord_seq))) {
    int len;
    char action;

    action = gchord_seq[g_ptr];
    len = gchord_len[g_ptr];
    if ((chordnum == -1) && (action == 'c')) {
      action = 'f';
    };
    switch (action) {

    case 'z':
      break;

    case 'f':
      if (g_started && gchords) {
        /* do fundamental */
        save_note(g_num*len, g_denom, basepitch+fun.base, fun.chan, fun.vel);
      };
      break;

    case 'b':
      if (g_started && gchords) {
        /* do fundamental */
        save_note(g_num*len, g_denom, basepitch+fun.base, fun.chan, fun.vel);
      };
/* There is no break here so the switch statement continues into the next case 'c' */ 

    case 'c':
      /* do chord with handling of any 'inversion' note */
      if (g_started && gchords) {
          for(j=0;j<gchordnotes_size;j++)
          save_note(g_num*len, g_denom, gchordnotes[j], 
		    gchord.chan, gchord.vel);
        };
      break;

    case 'g':
      if(gchordnotes_size>0 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[0],gchord.chan, gchord.vel); 
      break;

    case 'h':
      if(gchordnotes_size >1 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[1],gchord.chan, gchord.vel); 
      break;

    case 'i':
      if(gchordnotes_size >2 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[2],gchord.chan, gchord.vel); 
      break;

    case 'j':
      if(gchordnotes_size >3 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[3],gchord.chan, gchord.vel); 
      break;

    case 'G':
      if(gchordnotes_size>0 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[0]-12,gchord.chan, gchord.vel); 
      break;

    case 'H':
      if(gchordnotes_size >1 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[1]-12,gchord.chan, gchord.vel); 
      break;

    case 'I':
      if(gchordnotes_size >2 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[2]-12,gchord.chan, gchord.vel); 
      break;

    case 'J':
      if(gchordnotes_size >3 && g_started && gchords)
        save_note(g_num*len, g_denom, gchordnotes[3]-12,gchord.chan, gchord.vel); 
      break;


     default:
       printf("no such gchord code %c\n",action);
      };


    g_ptr = g_ptr + 1;
    addtoQ(g_num*len, g_denom, -1, g_ptr, 0);
    };
};

void dodrums(i)
/* generate drum notes */
int i;
{
  if ((i == drum_ptr) && (drum_ptr < (int) strlen(drum_seq))) {
    int len;
    char action;

    action = drum_seq[drum_ptr];
    len = drum_len[drum_ptr];
    switch (action) {
    case 'z':
      break;
    case 'd':
      if (drum_on) {
        save_note(drum_num*len, drum_denom, drum_program[drum_ptr], 9, 
                  drum_velocity[drum_ptr]);
      };
    };
    drum_ptr = drum_ptr + 1;
    addtoQ(drum_num*len, drum_denom, -1, drum_ptr, 0);
  };
}

void start_drone()
{
    int delta;
/*    delta = tracklen - drone.event; */
    delta = delta_time - drone.event;    
    if (drone.event == 0)  write_program(drone.prog, drone.chan);
    midi_noteon(delta,drone.pitch1+global_transpose,drone.chan,drone.vel1);
    midi_noteon(delta,drone.pitch2+global_transpose,drone.chan,drone.vel2);
/*    drone.event = tracklen;*/
    drone.event = delta_time;
}


void stop_drone()
{
    int delta;
/*    delta = tracklen - drone.event; */
    delta = delta_time - drone.event;
    midi_noteoff(delta,drone.pitch1+global_transpose,drone.chan);
    midi_noteoff(0,drone.pitch2+global_transpose,drone.chan);
/*    drone.event = tracklen; */
    drone.event = delta_time;
}



void progress_sequence(i)
int i;
{
  if (gchordson) {
    dogchords(i);
  };
  if (drumson) {
    dodrums(i);
  };
}

void init_drum_map()
{
int i;
for (i=0;i<256;i++)
   drum_map[i] = i;
}

static void parse_drummap(char **s)
/* parse abc note and advance character pointer */
/* code stolen from parseabc.c and simplified */
{
  int mult;
  char accidental, note;
  int octave;
  int midipitch;
  int mapto;
  char msg[80];
  char *anoctave = "cdefgab";
  int scale[7] = {0, 2, 4, 5, 7, 9, 11};

  mult = 1;
  accidental = ' ';
  note = ' ';
  /* read accidental */
  switch (**s) {
  case '_':
    accidental = **s;
    *s = *s + 1;
    if (**s == '_') {
      *s = *s + 1;
      mult = 2;
    };
    break;
  case '^':
    accidental = **s;
    *s = *s + 1;
    if (**s == '^') {
      *s = *s + 1;
      mult = 2;
    };
    break;
  case '=':
    accidental = **s;
    *s = *s + 1;
    if (**s == '^') {
      accidental = **s;
      *s = *s + 1;
      } 
    else if (**s == '_') { 
      accidental = **s;
      *s = *s + 1;
      } 
    break;
  default:
    /* do nothing */
    break;
  };
  if ((**s >= 'a') && (**s <= 'g')) {
    note = **s;
    octave = 1;
    *s = *s + 1;
    while ((**s == '\'') || (**s == ',')) {
      if (**s == '\'') {
        octave = octave + 1;
        *s = *s + 1;
      };
      if (**s == ',') {
        sprintf(msg, "Bad pitch specifier , after note %c", note);
        event_error(msg);
        octave = octave - 1;
        *s = *s + 1;
      };
    };
  } else {
    octave = 0;
    if ((**s >= 'A') && (**s <= 'G')) {
      note = **s + 'a' - 'A';
      *s = *s + 1;
      while ((**s == '\'') || (**s == ',')) {
        if (**s == ',') {
          octave = octave - 1;
          *s = *s + 1;
        };
        if (**s == '\'') {
          sprintf(msg, "Bad pitch specifier ' after note %c", note + 'A' - 'a');
          event_error(msg);
          octave = octave + 1;
          *s = *s + 1;
        };
      };
    };
  };
  /*printf("note = %d octave = %d accidental = %d\n",note,octave,accidental);*/
  midipitch = (int) ((long) strchr(anoctave, note) - (long) anoctave);
  if (midipitch <0 || midipitch > 6) {
    event_error("Malformed note in drummap : expecting a-g or A-G");
    return;
    } 
  midipitch = scale[midipitch];
  if (accidental == '^') midipitch += mult;
  if (accidental == '_') midipitch -= mult;
  midipitch = midipitch + 12*octave + 60;
  skipspace(s);
  mapto = readnump(s);
  if (mapto == 0) {
      event_error("Bad drummap: expecting note followed by space and number");
       return;
      }
  if (mapto < 35 || mapto > 81) event_warning ("drummap destination should be between 35 and 81 inclusive");
  /*printf("midipitch = %d map to %d \n",midipitch,mapto);*/ 
  drum_map[midipitch] = mapto;
}

static void starttrack()
/* called at the start of each MIDI track. Sets up all necessary default */
/* and initial values */
{
  int i;

  loudnote = 105;
  mednote = 95;
  softnote = 80;
  beatstring[0] = '\0';
  beataccents = 1;
  nbeats = 0;
  transpose = 0;
/* make sure meter is reinitialized for every track
 * in case it was changed in the middle of the last track */
  set_meter(header_time_num,header_time_denom);
  div_factor = division;
  gchords = 1;
  partno = -1;
  partlabel = -1;
  g_started = 0;
  g_ptr = 0;
  drum_ptr = 0;
  Qinit();
  if (noteson) {
    channel = findchannel();
  } else {
    /* set to valid value just in case - should never be used */
    channel = 0;
  };
  if (gchordson) {
    addtoQ(0, g_denom, -1, g_ptr, 0);
    fun.base = 36;
    fun.vel = 80;
    gchord.base = 48;
    gchord.vel = 75;
    if (noteson) {
      channels[channel] = 1;
    };
    fun.chan = findchannel();
    channels[fun.chan] = 1;
    gchord.chan = findchannel();
    channels[fun.chan] = 0;
    if (noteson) {
      channels[channel] = 0;
    };
  };
  if (droneon) {
    drone.event =0;
    drone.chan = findchannel();
    drone.prog  = 70; /* bassoon */
    drone.vel1 =  80;
    drone.pitch1 = 45;
    drone.vel2  = 80;
    drone.pitch2=  33;
    }

  g_next = 0;
  partrepno = 0;
  thismline = -1;
  thiswline = -1;
  nowordline = 0;
  waitforbar = 0;
  musicsyllables = 0;
  lyricsyllables = 0;
  for (i=0; i<26; i++) {
    part_count[i] = 0;
  };
}

long writetrack(xtrack)
/* this routine writes a MIDI track  */
int xtrack;
{
  int trackvoice;
  int inchord;
  int in_varend;
  int j, pass;
  int maxpass;
  int expect_repeat;
  int slurring;
  int state[5];
  int texton;
  int timekey;

/*  printf("writing track %d\n",xtrack); */

  /* default is a normal track */
  timekey=1;
  tracklen = 0L;
  delta_time = 0L;
  trackvoice = xtrack;
 /* if (trackvoice >= ntracks) trackvoice = xtrack-ntracks+32; */
  wordson = 0;
  noteson = 1;
  gchordson = 0;
  temposon = 0;
  texton = 1;
  drumson = 0;
  droneon = 0;
  in_varend = 0;
  maxpass = 2;
  notedelay = staticnotedelay;
  chordattack = staticchordattack;
  if (karaoke) {
    if (xtrack < 3)                  
       karaokestarttrack(xtrack);
    /* lyrics are in track 2 (track count starts from 0) */
    if (xtrack == 2) {
      kspace = 0;
      noteson = 0;
      wordson = 1;
/*
 *  Turn text off for H:, A: and other fields.
 *  Putting it in Karaoke Words track (track 2) can throw off some Karaoke players.
 */   
      texton = 0;
      gchordson = 0;
      trackvoice = wordvoice;
    } else {
      if (trackvoice > 2) trackvoice = xtrack - 1;
    };
  };
  /* is this accompaniment track ? */
  if ((gchordvoice != 0) && (xtrack == gchordtrack)) {
    noteson = 0; 
    gchordson = 1;
    drumson = 0;
    droneon = 0;
    temposon = 0;
    trackvoice = gchordvoice;
/* be sure set_meter is called before setbeat even if we
 * have to call it more than once at the start of the track */
    set_meter(header_time_num,header_time_denom);
/*    printf("calling setbeat for accompaniment track\n"); */
    setbeat();
  };
  /* is this drum track ? */
  if ((drumvoice != 0) && (xtrack == drumtrack)) {
    noteson = 0;
    gchordson = 0;
    drumson = 1;
    droneon =0;
    temposon = 0;
    trackvoice = drumvoice;
  };
  /* is this drone track ? */
  if ((dronevoice != 0) && (xtrack == dronetrack)) {
    noteson = 0;
    gchordson = 0;
    drumson = 0;
    droneon = 1;
    temposon = 0;
    trackvoice = drumvoice;
   };
  if (verbose) {
    printf("track %d, voice %d\n", xtrack, trackvoice);
  };
  if (xtrack == 0) {
    mf_write_tempo(tempo);
    /* write key */
    write_keysig(sf, mi);
    /* write timesig */
    write_meter(time_num, time_denom);
    gchordson = 0;
    temposon = 1;
    if (ntracks > 1) {
       /* type 1 files have no notes in first track */
       noteson = 0;
       texton = 0;
       trackvoice = 1;
       timekey=0;
       /* return(0L); */
    }
  }
  starttrack();
  inchord = 0;
  /* write notes */
  j = 0;
  if ((voicesused) && (trackvoice != 1)) {
    j = findvoice(j, trackvoice, xtrack);
  };
  barno = 0;
  bar_num = 0;
  bar_denom = 1;
  err_num = 0;
  err_denom = 1;
  pass = 1;
  save_state(state, j, barno, div_factor, transpose, channel);
  slurring = 0;
  expect_repeat = 0;
  while (j < notes) {
    switch(feature[j]) {
    case NOTE:
      if (wordson) {
        write_syllable(j);
      };
      if (noteson) {
        if (inchord) {
           notecount++;
           if (notecount > 1) {
                if(chordattack > 0) notedelay = (int) (chordattack*ranfrac());
                delta_time += notedelay;
                totalnotedelay += notedelay;
                }
           }
        noteon(j);
        /* set up note off */
       if (channel == 9) 
        addtoQ(num[j], denom[j], drum_map[pitch[j]], channel, -totalnotedelay -1);
        else addtoQ(num[j], denom[j], pitch[j] + transpose +global_transpose, channel, -totalnotedelay -1);
      };
      if (!inchord) {
        delay(num[j], denom[j], 0);
        addunits(num[j], denom[j]);
        notecount =0;
        totalnotedelay=0;
      };
      break;
    case TNOTE:
      if (wordson) {
        /* counts as 2 syllables : note + first tied note.
	 * We ignore any bar line placed between tied notes
	 * since this causes write_syllable to lose synchrony
	 * with the notes.                                    */
        write_syllable(j);
        waitforbar = 0;
        write_syllable(j);
      };
      if (noteson) {
        noteon(j);
        /* set up note off */
       if (channel == 9) 
        addtoQ(num[j], denom[j], drum_map[pitch[j]], channel, -totalnotedelay -1);
        else addtoQ(num[j], denom[j], pitch[j] + transpose +global_transpose, channel, -totalnotedelay -1);
      };
      break;
    case OLDTIE:
      if (wordson) {
        /* extra syllable beyond first two in a tie */
        write_syllable(j);
      };
      break;
    case REST:
      if (!inchord) {
        delay(num[j], denom[j], 0);
        addunits(num[j], denom[j]);
      };
      break;
    case CHORDON:
      inchord = 1;
      break;
    case CHORDOFF:
    case CHORDOFFEX:
      if (wordson) {
        write_syllable(j);
      };
      inchord = 0;
      delay(num[j], denom[j], 0);
      totalnotedelay=0;
      notecount=0;
      notedelay = staticnotedelay;
      chordattack = staticchordattack;
      addunits(num[j], denom[j]);
      break;
    case LINENUM:
      /* get correct line number for diagnostics */
      lineno = pitch[j];
      break;
    case MUSICLINE:
      if (wordson) {
        thismline = j;
        nowordline = 0;
      };
      break;
    case MUSICSTOP:
      if (wordson) {
        checksyllables();
      };
      break;
    case PART:
      in_varend = 0;
      j = partbreak(xtrack, trackvoice, j);
      if (parts == -1) {
        char msg[1];

        msg[0] = (char) pitch[j];
        mf_write_meta_event(0L, marker, msg, 1);
      };
      break;
    case VOICE:
      /* search on for next occurence of voice */
      j = findvoice(j, trackvoice, xtrack);
      break;
    case TEXT:
      if (texton) {
        mf_write_meta_event(0L, text_event, atext[pitch[j]],
                          strlen(atext[pitch[j]]));
      };
      break;
    case TITLE:
/*  Write name of song as sequence name in track 0 and as track 1 name. */
/*  karaokestarttrack routine handles this instead if tune is a Karaoke tune. */
        if (!karaoke) {
           if (xtrack < 2)
              mf_write_meta_event(0L, sequence_name, atext[pitch[j]],
                                strlen(atext[pitch[j]]));
        }
      break;
    case SINGLE_BAR:
      waitforbar = 0;
      checkbar(pass);
      break;
    case DOUBLE_BAR:  /* || */
      in_varend = 0;
      waitforbar = 0;
      softcheckbar(pass);
      break;

    case BAR_REP: /* |: */
    /* ensures that two |: don't occur in a row                */
    /* saves position of where to return when :| is encountered */
      in_varend = 0;
      waitforbar = 0;
      softcheckbar(pass);
      if ((pass==1)&&(expect_repeat)) {
        event_error("Expected end repeat not found at |:");
      };
      save_state(state, j, barno, div_factor, transpose, channel);
      expect_repeat = 1;
      pass = 1;
      maxpass=2;
      break;

    case REP_BAR:  /* :|  */
    /* ensures it was preceded by |: so we know where to return */
    /* returns index j to the place following |:                */ 
      in_varend = 0;
      waitforbar = 0;
      softcheckbar(pass);
      if (pass == 1) {
         if (!expect_repeat) {
            event_error("Found unexpected :|");
          } else {
          /*  pass = 2;  [SS] 2004-10-14 */
            pass++;   /* we may have multiple repeats */
            restore_state(state, &j, &barno, &div_factor, &transpose, &channel);
            slurring = 0;
            expect_repeat = 0;
          };

      } 
      else {
     /* we could have multi repeats.                        */
     /* pass = 1;          [SS] 2004-10-14                  */
     /* we could have accidently have                       */
     /*   |: .sect 1..  :| ...sect 2 :|.  We  don't want to */
     /* go back to sect 1 when we encounter :| in sect 2.   */
     /* We signal that we will expect |: but we wont't check */
            if(pass < maxpass)
              {
              expect_repeat = 0;
              pass++;   /* we may have multiple repeats */
              restore_state(state, &j, &barno, &div_factor, &transpose, &channel);
              slurring = 0;
              }
      };
      break;

    case PLAY_ON_REP: /* |[1 or |[2 or |1 or |2 */
    /* keeps count of the pass number and selects the appropriate   */
    /* to be played for each pass. This code was designed to handle */ 
    /* multirepeats using the inlist() function however the pass    */
    /* variable is not set up correctly for multirepeats.           */
      {
        int passnum;
        char errmsg[80];
 
        if (in_varend != 0) {
          event_error("Need || |: :| or ::  to mark end of variant ending");
        };
        passnum = -1;
        if (((expect_repeat)||(pass>1))) {
          passnum = pass;
        }

    /** else {                    // additivity remnants
          if (parts != -1) {
            passnum = partrepno+1;
          };
        };
     ***/
        if (passnum == -1) {
          event_error("multiple endings do not follow |: or ::");
          passnum = 1;
        };
       if (inlist(j, passnum) != 1) {
          j = j + 1;
     /* if this is not the variant ending to be played on this pass*/
     /* then skip to the end of this section watching out for voice*/
     /* changes. Usually a section end with a :|, but the last     */
     /* last section could end with almost anything including a    */
     /* PART change.                                               */
          if(feature[j] == VOICE) j = findvoice(j, trackvoice, xtrack);
          while ((j<notes) && (feature[j] != REP_BAR) && 
                 (feature[j] != BAR_REP) &&
                 (feature[j] != PART) &&
                 (feature[j] != DOUBLE_BAR) &&
                 (feature[j] != THICK_THIN) &&
                 (feature[j] != THIN_THICK) &&
                 (feature[j] != PLAY_ON_REP)) {
            j = j + 1;
            if(feature[j] == VOICE) j = findvoice(j, trackvoice, xtrack);
          };
          barno = barno + 1;
          if ((j == notes) /* || (feature[j] == PLAY_ON_REP) */) { 
          /* end of tune was encountered before finding end of */
          /* variant ending.  */
            sprintf(errmsg, 
              "Cannot find :| || [| or |] to close variant ending");
            event_error(errmsg);
          } else {
            if (feature[j] == PART) {
              j = j - 1; 
            };
          };
        } else {
          in_varend = 1;   /* segment matches pass number, we play it */
         /* printf("playing at %d for pass %d\n",j,passnum); */
          maxpass = pass+1;
        };
      };
      break;

    case DOUBLE_REP:     /*  ::  */
      in_varend = 0;
      waitforbar = 0;
      softcheckbar(pass);
      if (pass > 1) {
        /* Already gone through last time. Process it as a |:*/
        /* and continue on.                                  */
        expect_repeat = 1;
        save_state(state, j, barno, div_factor, transpose, channel);
        pass = 1;
        maxpass=2;
      } else {
          /* should do a repeat unless |: is missing.       */
          if (!expect_repeat) {
            /* missing |: don't repeat but set up for next repeat */
            /* section.                                           */
            event_error("Found unexpected ::");
            expect_repeat = 1;
            save_state(state, j, barno, div_factor, transpose, channel);
            pass = 1;
          } else {
            /* go back and do the repeat */
            restore_state(state, &j, &barno, &div_factor, &transpose, &channel);
            slurring = 0;
            /*pass = 2;  [SS] 2004-10-14*/
            pass++;
          };
      };
      break;

    case GCHORD:
      basepitch = pitch[j];
      inversion = num[j];
      chordnum = denom[j];
      g_started = 1;
      configure_gchord();
      break;
    case GCHORDON:
      if (gchordson) {
        gchords = 1;
      };
      break;
    case GCHORDOFF:
      gchords = 0;
      break;
    case DRUMON:
      if (drumson) {
        drum_on = 1;
      };
      break;
    case DRUMOFF:
      drum_on = 0;
      break;
    case DRONEON:
      if ((dronevoice != 0) && (xtrack == dronetrack)) 
         start_drone();
      break;
    case DRONEOFF:
      if ((dronevoice != 0) && (xtrack == dronetrack)) 
         stop_drone();
      break;
    case ARPEGGIO:
       notedelay = 3*staticnotedelay;
       chordattack=3*staticchordattack;
       break;
    case DYNAMIC:
      dodeferred(atext[pitch[j]],noteson);
      break;
    case KEY:
      if(timekey) write_keysig(pitch[j], denom[j]);
      break;
    case TIME:
      if(timekey) {
        barchecking = pitch[j];
        write_meter(num[j], denom[j]);
        setbeat();   /* NEW [SS] 2003-APR-27 */
        }
      break;
    case TEMPO:
      if (temposon) {
        char data[3];
/*
        long newtempo;

        newtempo = ((long)num[j]<<16) | ((long)denom[j] & 0xffff);
        printf("New tempo = %ld [%x %x]\n", newtempo, num[j], denom[j]);
*/
        data[0] = num[j] & 0xff;
        data[1] = (denom[j]>>8) & 0xff;
        data[2] = denom[j] & 0xff;
        mf_write_meta_event(delta_time, set_tempo, data, 3);
        tracklen = tracklen + delta_time;
        delta_time = 0L;
/*
        if (j > 0) {
          div_factor = pitch[j];
        };
*/
      };
      break;
    case CHANNEL:
      channel = pitch[j];
      break;
    case TRANSPOSE:
      transpose = pitch[j];
      break;
    case GTRANSPOSE:
      global_transpose = pitch[j];
      break;
    case RTRANSPOSE:
      global_transpose +=  pitch[j];
      break;
    case SLUR_ON:
      if (slurring) {
        event_error("Unexpected start of slur found");
      };
      slurring = 1;
      break;
    case SLUR_OFF:
      if (!slurring) {
        event_error("Unexpected end of slur found");
      };
      slurring = 0;
      break;
    case COPYRIGHT:
       if (xtrack == 0) {
          mf_write_meta_event(delta_time, copyright_notice, atext[pitch[j]], strlen (atext[pitch[j]]));
       }
      break;
    default:
      break;
    };
    j = j + 1;
  };
  if ((expect_repeat)&&(pass==1)) {
    event_error("Missing :| at end of tune");
  };
  clearQ();
  tracklen = tracklen + delta_time;
  if (xtrack == 1) {
    tracklen1 = tracklen;
  } else {
    if ((xtrack != 0) && (tracklen != tracklen1)) {
      char msg[100];

      sprintf(msg, "Track %d is %ld units long not %ld",
              xtrack, tracklen, tracklen1);
      event_warning(msg);
    };
  };
  return (delta_time);
}


char *featname[] = {
"SINGLE_BAR", "DOUBLE_BAR", "BAR_REP", "REP_BAR",
"PLAY_ON_REP", "REP1", "REP2", "BAR1",
"REP_BAR2", "DOUBLE_REP", "THICK_THIN", "THIN_THICK",
"PART", "TEMPO", "TIME", "KEY",
"REST", "TUPLE", "NOTE", "NONOTE",
"OLDTIE", "TEXT", "SLUR_ON", "SLUR_OFF",
"TIE", "CLOSE_TIE", "TITLE", "CHANNEL",
"TRANSPOSE", "RTRANSPOSE", "GTRANSPOSE", "GRACEON",
"GRACEOFF", "SETGRACE", "SETC", "GCHORD",
"GCHORDON", "GCHORDOFF", "VOICE", "CHORDON",
"CHORDOFF", "CHORDOFFEX", "DRUMON", "DRUMOFF",
"DRONEON", "DRONEOFF", "SLUR_TIE", "TNOTE",
"LT", "GT", "DYNAMIC", "LINENUM",
"MUSICLINE", "MUSICSTOP", "WORDLINE", "WORDSTOP",
"INSTRUCTION", "NOBEAM", "CHORDNOTE", "CLEF",
"PRINTLINE", "NEWPAGE", "LEFT_TEXT", "CENTRE_TEXT",
"VSKIP", "COPYRIGHT", "COMPOSER", "ARPEGGIO",
"SPLITVOICE"
}; 

dumpfeat (int from, int to)
{
int i,j;
for (i=from;i<=to;i++)
  {
  j = feature[i]; 
  printf("%d %s   %d %d/%d \n",i,featname[j],pitch[i],num[i],denom[i]);
  }
}


/* see file queues.c for routines to handle note queue */

