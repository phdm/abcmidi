/*
 * yaps - program to convert abc files to PostScript.
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

/* yapstree.c - back-end for abc parser. */
/* generates a data structure suitable for typeset music */

#include <stdio.h>
#ifdef USE_INDEX
#define strchr index
#endif
#ifdef ANSILIBS
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#else
char* strchr();
#endif
#include "abc.h"
#include "parseabc.h"
#include "parser2.h"
#include "structs.h"
#include "drawtune.h"

extern void setscaling(char *s);
extern void font_command();
extern void setup_fonts();
extern void printtune(struct tune *t);
extern void set_keysig(struct key *k, struct key *newval);

struct voice* cv;
struct tune thetune;

char outputname[256];
char outputroot[256];
char matchstring[256];
int fileopen;

int repcheck;
int xinhead;
int xinbody;
int suppress;
int debugging;
int pagenumbering;
int separate_voices;
int print_xref;
int landscape;
int barnums,nnbars;
extern int gchords_above;
extern int decorators_passback[DECSIZE]; /* a kludge for passing
information from the event_handle_instruction to parsenote
in parseabc.c */

enum linestattype {fresh, midmusic, endmusicline, postfield};
enum linestattype linestat;


void setfract(f, a, b)
struct fract* f;
int a, b;
/* assign value to fraction */
{
  f->num = a;
  f->denom = b;
}

void reducef(f)
struct fract* f;
/* reducef fraction to smallest terms */
{
  int t, n, m;

  /* find HCF using Euclid's algorithm */
  if (f->num > f->denom) {
    n = f->num;
    m = f->denom;
  } else {
    n = f->denom;
    m = f->num;
  };
  while (m != 0) {
    t = n % m;
    n = m;
    m = t;
  };
  f->num = f->num/n;
  f->denom = f->denom/n;
}

static struct fract* newfract(int a, int b)
/* create an initialized fraction */
{
  struct fract* f;

  f = (struct fract*)checkmalloc(sizeof(struct fract));
  f->num = a;
  f->denom = b;
  return(f);
}

static struct slurtie* newslurtie()
/* create a new slur/tie data structure */
{
  struct slurtie* f;

  f = (struct slurtie*)checkmalloc(sizeof(struct slurtie));
  f->begin = NULL;
  f->end = NULL;
  f->crossline = 0;
  return(f);
}

static struct atempo* newtempo(int count, int n, int m, int relative,
                               char *pre, char *post)
/* create an initialized tempo data structure */
{
  struct atempo* t;

  t = (struct atempo*)checkmalloc(sizeof(struct atempo));
  t->count = count;
  t->basenote.num = n;
  t->basenote.denom = m;
  t->relative = relative;
  if (pre == NULL) {
    t->pre = NULL;
  } else {
    t->pre = addstring(pre);
  };
  if (post == NULL) {
    t->post = NULL;
  } else {
    t->post = addstring(post);
  };
  return(t);
}

static struct vertspacing* newvertspacing()
/* create new vertspacing structure */
{
  struct vertspacing* p;

  p = (struct vertspacing*)checkmalloc(sizeof(struct vertspacing));
  p->height = 0.0;
  p->descender = 0.0;
  p->yend = 0.0;
  p->yinstruct = 0.0;
  p->ygchord = 0.0;
  p->ywords = 0.0;
  return(p);
}

static struct tuple* newtuple(int n, int q, int r, int label)
/* create tuple data structure */
{
  struct tuple* f;

  f = (struct tuple*)checkmalloc(sizeof(struct tuple));
  f->n = n;
  f->q = q;
  f->r = r;
  f->label = label;
  f->beamed = 1;
  return(f);
}

static struct chord* newchord()
/* create chord data structure */
{
  struct chord* f;

  f = (struct chord*)checkmalloc(sizeof(struct chord));
  f->ytop = 0;
  f->ybot = 0;
  return(f);
}

struct aclef* newclef(enum cleftype t, int octave)
/* create and initialize clef data structure */
{
  struct aclef* f;

  f = (struct aclef*)checkmalloc(sizeof(struct aclef));
  f->type = t;
  f->octave = octave;
  return(f);
}

struct key* newkey(char* name, int sharps, char accidental[], int mult[])
/* create and initialize key signature */
{
  struct key* k;
  int i;

  k = (struct key*)checkmalloc(sizeof(struct key));
  k->name = addstring(name);
  k->sharps = sharps;
  for (i=0; i<7; i++) {
    k->map[i] = accidental[i];
    k->mult[i] = mult[i];
  };
  return(k);
}

void init_llist(struct llist* l)
/* initialize a linked list */
{
  l->first = NULL;
  l->last = NULL;
  l->place = NULL;
}

void addtolist(struct llist* p, void* item)
/* append an item to a linked list */
{
  struct el* x;

  x = (struct el*)checkmalloc(sizeof(struct el));
  x->next = NULL;
  x->datum = item;
  if (p->first == NULL) {
    p->first = x;
    p->last = x;
  } else {
    p->last->next = x;
    p->last = x;
  };
}

void* firstitem(struct llist* p)
/* find the first item in the list */
/* also initialize for a traversal using nextitem() */
{
  if (p == NULL) {
    return(NULL);
  };
  p->place = p->first;
  if (p->place == NULL) {
    return(NULL);
  } else {
    return(p->place->datum);
  };
}

void* nextitem(struct llist* p)
/* return 'next' item in the list. Successive calls return successive    */
/* items or NULL after the end of the list has been reached. firstitem() */
/* must be called prior to the first call to nextitem()                  */
{
  if (p->place == NULL) {
    return(NULL);
  } else {
    p->place = p->place->next;
    if (p->place == NULL) {
      return(NULL);
    } else {
      return(p->place->datum);
    };
  };
}

void freellist(struct llist* l)
/* frees up all dynamically allocated memory used to build the linked list */
{
  void* p;
  struct el* e;
  struct el* olde;

  /* printf("freellist\n"); */
  if (l != NULL) {
    p = firstitem(l);
    while (p != NULL) {
      free(p);
      p = nextitem(l);
    };
    e = l->first;
    while (e != NULL) {
      olde = e;
      e = e->next;
      free(olde);
    };
    init_llist(l);
  };
}

static void closebeam(struct voice* v)
/* called after a run of notes to be beamed together */
{
  struct note* n;
  struct feature* ft;
  int stemup;
  int ingrace;

  if (cv->tuplenotes > 0) {
    cv->thistuple->beamed = 0;
  };
  if (v->beamroot == v->beamend) {
    ft = v->beamroot;
    n = ft->item;
    n->beaming = single;
    v->beamroot = NULL;
    return;
  }; 
  if (v->beammax + v->beammin > 2*4) {
    stemup = 0;
  } else {
    stemup = 1;
  };
  ft = v->beamroot;
  ingrace = 0;
  while ((ft != NULL) && (ft != v->beamend)) {
    switch (ft->type) {
    case NOTE:
      if (ingrace == 0) {
        n = ft->item;
        n->stemup = stemup;
      };
      break;
    case GRACEON:
      ingrace = 1;
      break;
    case GRACEOFF:
      ingrace = 0;
      break;
    default:
      break;
    };
    ft = ft->next;
  };
  if (ft == v->beamend) {
    n = ft->item;
    n->stemup = stemup;
    n->beaming = endbeam;
  } else {
    printf("closebeam: internal data error\n");
    exit(1);
  };
  v->beamroot = NULL;
}

static void closegracebeam(struct voice* v)
/* called after a run of grace notes to be beamed together */
{
  struct note* n;
  struct feature* ft;

  ft = v->gracebeamend;
  if (ft == NULL) {
    event_error("Missing grace notes");
  } else {
    n = ft->item;
    if (v->gracebeamroot == v->gracebeamend) {
      n->beaming = single;
    } else {
      n->beaming = endbeam;
    }; 
  };
}

static void insertnote(struct feature* chordplace, struct feature* newfeature)
/* place NOTE in decreasing pitch order within chord */
{
  struct note* n;
  struct note* newnote;
  struct feature* f;
  struct feature* previous;
  int foundplace;

  newnote = newfeature->item;
  previous = chordplace;
  f = chordplace->next;
  foundplace = 0;
  n = NULL;
  while ((f != NULL)&&(f->type==NOTE)&&(foundplace == 0)) {
    n = f->item;
    if (newnote->y > n->y) {
      foundplace = 1;
    } else {
      previous = f;
      f = f->next;
    };
  };
  /* printvoiceline in drawtune.c expects the gchord or
   * instructions to be associated with the first note in
   * chord. If the notes are reordered then we need
   * to move these fields.
   * if previous == chordplace then move n->gchords and
   * n->instructions to newnote->gchords and newnote->instructions
   */
  if (previous == chordplace && n != NULL) {
	  newnote->gchords = n->gchords;
	  newnote->instructions = n->instructions;
	  n->gchords = NULL;
	  n->instructions = NULL;
  }
  newfeature->next = previous->next;
  previous->next = newfeature;
  if (newfeature->next == NULL) {
    cv->last = newfeature;
  };
}

static void beamitem(featuretype mytype, void* newitem, struct feature* x)
/* This routine is responsible for working out which notes are to be */
/* beamed together and recording this in the note structure record */
{
  struct note* n;

  /* deal with beaming here */
  if (cv->ingrace) {
    if (mytype == NOTE) {
      n = newitem;
      n->stemup = 1;
      if (cv->gracebeamroot == NULL) {
        cv->gracebeamroot = x;
        cv->gracebeamend = x;
        n->beaming = startbeam;
      } else {
        cv->gracebeamend = x;
        n->beaming = midbeam;
      };
    };
  } else {
    if (cv->beamroot != NULL) {
      switch (mytype) {
      case NOTE:
        n = newitem;
        if (n->base_exp >= -2) {
          n->beaming = single;
          closebeam(cv);
        } else {
          /* extend beam */
          if (n->y > cv->beammax) {
            cv->beammax = n->y;
          };
          if (n->y < cv->beammin) {
            cv->beammin = n->y;
          };
          cv->beamend = x;
          n->beaming = midbeam;
        };
        break;
      case KEY:
      case REST:
      case NOBEAM:
      case SINGLE_BAR:
      case DOUBLE_BAR:
      case BAR_REP:
      case REP_BAR:
      case BAR1:
      case REP_BAR2:
      case DOUBLE_REP:
      case THICK_THIN:
      case THIN_THICK:
      case TUPLE:
      case MUSICSTOP:
        /* closebeam */
        closebeam(cv);
        break;
      default:
        break;
      };
    } else {
      if (mytype == NOTE) {
        n = newitem;
        if (n->base_exp >= -2) {
          n->beaming = single;
        } else {
          n->beaming = startbeam;
          cv->beammax = n->y;
          cv->beammin = n->y;
          cv->beamroot = x;
          cv->beamend = x;
        };
      };
    };
  };
}

static struct feature* addfeature(featuretype mytype, void* newitem)
/* append a new data element to the linked list for the current voice */
/* The element can be a note or lots of other things */
{
  struct voice* p;
  struct feature* x;

  p = cv;
  /* printf("in addfeature type=%d\n", mytype); */
  if (cv == NULL) {
    printf("ERROR: no current voice in addfeature type=%d\n", mytype);
    printf("xinhead = %d xinbody = %d\n", xinhead, xinbody);
    exit(0);
  };
  x = (struct feature*)checkmalloc(sizeof(struct feature));
  x->next = NULL;
  x->type = mytype;
  x->item = newitem;
  x->xleft = 0;
  x->xright = 0;
  x->yup = 0;
  x->ydown = 0;
  if (cv->first == NULL) {
    cv->first = x;
    cv->last = x;
    beamitem(mytype, newitem, x);
  } else {
    if ((cv->last == NULL)||(cv->last->next != NULL)) {
      printf("expecting NULL at list end!\n");
      exit(0);
    };
    if ((cv->inchord)&&(mytype==NOTE)) {
      insertnote(cv->chordplace, x);
    } else {
      cv->last->next = x;
      cv->last = x;
      beamitem(mytype, newitem, x);
    };
  };
  return(x);
}

struct llist* newlist()
/* create and initialize a new linked list */
{
  struct llist* l;

  l = (struct llist*)checkmalloc(sizeof(struct llist));
  init_llist(l);
  return(l);
}

static int notenum(int octave, char ch, enum cleftype clef, int clefoctave)
/* converts note to number for stave position */
/* note E is zero (bottom line of stave) */
{
  int n;

  n = 5 + (7 + ch -'c')%7 + 7*(octave-1);
  switch (clef) {
  case treble:
    break;
  case soprano:
    n = n + 2;
    break;
  case mezzo:
    n = n + 4;
    break;
  case alto:
    n = n + 6;
    break;
  case tenor:
    n = n + 8;
    break;
  case baritone:
    n = n + 10;
    break;
  case bass:
    n = n + 12;
    break;
  };
  switch (clefoctave) {
  case -22:
    n = n + 21;
    break;
  case -15:
    n = n + 14;
    break;
  case -8:
    n = n + 7;
    break;
  case 8:
    n = n - 7;
    break;
  case 15:
    n = n - 14;
    break;
  case 22:
    n = n - 21;
    break;
  default:
    break;
  };
  return(n);
}

int count_dots(int *base, int *base_exp, int n, int m)
/* convert fraction to 2^base_exp followed by dots */
/* previously used 1/base instead of 2^base_exp */
/* -1 indicates a note value which is impossible to represent */
{
  int dots;
  int start;
  int a, b;

  *base = 1; /* set default value if we fail */
  *base_exp = 0;
  if ((n<1)||(m<1)) {
    return(-1);
  };
  /* check denominator is power of 2 */
  a = m;
  while (a>1) {
    if (a%2 == 1) {
      return(-1);
    };
    a = a/2;
  };
  dots = 0;
  start = m;
  while (start < n) {
    start = start*2;
    *base_exp = *base_exp + 1;
  };
  while (start > n) {
    start = start/2;
    *base_exp = *base_exp - 1;
  };
  if (start == 0) {
    printf("Problem with %d / %d\n", n, m);
    exit(0);
  };
  *base = m/start;
  a = n - start;
  b = start;
  while (a>0) {
    dots = dots + 1; 
    b = b/2;
    a = a - b;
    if (a< 0) {
      return(-1);
    };
  };
  return(dots);
}

static char* decstring(int decorators[])
/* creates a string of decorators (ornament, staccato, roll, up-bow etc.) */
/* from a boolean array */
{
  int i, j;
  char decs[DECSIZE+1];

  j = 0;
  for (i=0; i<DECSIZE; i++) {
    if (decorators[i]) {
      decs[j] = decorations[i];
      j = j + 1;
    };
  };
  decs[j] = '\0';
  if (j==0) {
    return(NULL);
  } else {
    return(addstring(decs));
  };
}

static struct note* newnote(decorators, xaccidental, xmult, xnote, xoctave, 
                            a, b)
int decorators[DECSIZE];
int xmult;
char xaccidental, xnote;
int xoctave;
int a, b;
/* create and set up the fields for a note structure */
{
  struct note* n;

  n = (struct note*)checkmalloc(sizeof(struct note));
  setfract(&n->len, a, b);
  reducef(&n->len);
  n->dots = count_dots(&n->base, &n->base_exp, n->len.num, n->len.denom);
/*
  if (n->dots == -1) {
    event_error("Illegal note length");
  };
*/
  n->accents = decstring(decorators);
  n->accidental = xaccidental;
  n->acc_offset = 0;
  n->fliphead = 0;
  n->mult = xmult; 
  n->octave = xoctave;
  n->pitch = xnote;
  n->y = notenum(xoctave, xnote, cv->clef->type, cv->clef->octave);
  if (n->y < 4) {
    n->stemup = 1;
  } else {
    n->stemup = 0;
  };
  n->stemlength = 0.0;
  n->syllables = NULL;
  if (cv->ingrace) {
    n->gchords = NULL;
    n->instructions = NULL;
  } else {
    n->gchords = cv->gchords_pending;
    cv->gchords_pending = NULL;
    n->instructions = cv->instructions_pending;
    cv->instructions_pending = NULL;
  };
  return(n);
}

static struct rest* newrest(int a, int b, int multi)
/* create and set up a new rest structure */
{
  struct rest* n;

  n = (struct rest*)checkmalloc(sizeof(struct rest));
  setfract(&n->len, a, b);
  n->dots = count_dots(&n->base, &n->base_exp, a, b);
  if (n->dots == -1) {
    event_error("Illegal rest length");
  };
  n->multibar = multi;
  if (cv->ingrace) {
    n->gchords = NULL;
    n->instructions = NULL;
  } else {
    n->gchords = cv->gchords_pending;
    cv->gchords_pending = NULL;
    n->instructions = cv->instructions_pending;
    cv->instructions_pending = NULL;
  };
  return(n);
}

static void addunits(f, n, m)
struct fract* f;
int n, m;
/* add n/m to fraction pointed to by f */
{
  f->num = n*f->denom*(cv->unitlen.num) + f->num*(m*cv->unitlen.denom);
  f->denom = (m*cv->unitlen.denom)*f->denom;
  reducef(f);
}

static struct voice* newvoice(int n)
/* create and set up a new voice data structure */
{
  struct voice* v;

  v = (struct voice*)checkmalloc(sizeof(struct voice));
  v->first = NULL;
  v->last = NULL;
  v->voiceno = n;
  v->octaveshift = thetune.octaveshift;
  setfract(&v->unitlen, thetune.unitlen.num, thetune.unitlen.denom);
  v->changetime = 0;
  v->inslur = 0;
  v->ingrace = 0;
  v->inchord = 0;
  v->expect_repeat = 0;
  v->tuplenotes = 0;
  v->thistuple = NULL;
  v->tuple_count = 0;
  v->brokenpending = -1;
  v->tiespending = 0;
  v->slurpending = 0;
  v->slurcount = 0;
  v->barno = 0;
  v->barchecking = thetune.barchecking;
  setfract(&v->barlen, thetune.meter.num, thetune.meter.denom);
  v->clef = newclef(thetune.clef.type, thetune.clef.octave);
  if (thetune.keysig == NULL) {
    printf("Trying to set up voice with no key signature\n");
    exit(0);
  } else {
    v->keysig = newkey(thetune.keysig->name, thetune.keysig->sharps,
                       thetune.keysig->map, thetune.keysig->mult);
  };
  v->tempo = NULL;
  setfract(&v->barcount, 0, 1);
  setfract(&v->meter, thetune.meter.num, thetune.meter.denom);
  v->lastnote = NULL;
  v->laststart = NULL;
  v->lastend = NULL;
  v->line = header;
  v->thisstart = NULL;
  v->thisend = NULL;
  v->gchords_pending = NULL;
  v->instructions_pending = NULL;
  v->beamed_tuple_pending = 0;
  v->linestart = NULL;
  v->lineend = NULL;
  v->more_lyrics = 0;
  v->lyric_errors = 0;
  v->thischord = NULL;
  v->chordplace = NULL;
  v->beamroot = NULL;
  v->beamend = NULL;
  v->gracebeamroot = NULL;
  v->gracebeamend = NULL;
  return(v);
}

static void setvoice(int n)
/* set current voice to voice n. If voice n does not exist, create it */
{
  struct voice* v;
  struct el* l;
  int done;

  if (thetune.voices.first == NULL) {
    cv = newvoice(n);
    v = cv;
    addtolist(&thetune.voices, (void*)v);
  } else {
    l = thetune.voices.first;
    done = 0;
    while ((done == 0) && (l != NULL)) {
      if (((struct voice*)l->datum)->voiceno == n) {
        done = 1;
        cv = (struct voice*)l->datum;
      } else {
       l = l->next;
      };
    };
    if (done == 0) {
      cv = newvoice(n);
      v = cv;
      addtolist(&thetune.voices, (void*)v);
    };
  };
}

static void init_tune(struct tune* t, int x) 
/* initialize tune structure */
{
  t->no = x;
  t->octaveshift = 0;
  init_llist(&t->title);
  t->composer = NULL;
  t->origin = NULL;
  t->parts = NULL;
  init_llist(&t->notes);
  init_llist(&t->voices);
  setfract(&t->meter, 0, 1);
  setfract(&t->unitlen, 0, 1);
  t->cv = NULL;
  t->keysig = NULL;
  t->clef.type = treble;
  t->clef.octave = 0;
  t->tempo = NULL;
  init_llist(&t->words);
};

static void freekey(struct key* k)
/* free up memory allocated for key data structure */
{
  if (k != NULL) { 
    if (k->name != NULL) {
      free(k->name);
    };
    free(k);
  };
}

static void freetempo(struct atempo *t)
/* free up memory allocated for temp data structure */
{
  if (t->pre != NULL) {
    free(t->pre);
  };
  if (t->post != NULL) {
    free(t->post);
  };
}

static void freefeature(void* item, featuretype type)
/* free memory allocated for feature in voice */
{
  struct note *n;
  struct rest *r;
  struct atempo *t;

  switch(type) {
  case NOTE:
    n = item;
    if (n->accents != NULL) {
      free(n->accents);
    };
    if (n->syllables != NULL) {
      freellist(n->syllables);
      free(n->syllables);
    };
    if (n->gchords != NULL) {
      freellist(n->gchords);
      free(n->gchords);
    };
    if (n->instructions != NULL) {
      freellist(n->instructions);
      free(n->instructions);
    };
    free(n);
    break;
  case REST:
    r = item;
    if (r->gchords != NULL) {
      freellist(r->gchords);
      free(r->gchords);
    };
    if (r->instructions != NULL) {
      freellist(r->instructions);
      free(r->instructions);
    };
    free(r);
    break;
  case KEY:
    freekey(item);
    break;
  case TEMPO:
    freetempo(item);
    break;
  case CLEF:
  case TIME:
  case PART:
  case CHORDON:
  case TUPLE:
  case SLUR_ON:
  case TIE:
  case PLAY_ON_REP:
  case LEFT_TEXT:
  case PRINTLINE:
    if (item != NULL) {
      free(item);
    };
    break;
  default:
    break;
  };
}

static void freevoice(struct voice* v)
/* free up memory allocated for voice data structure and voice data */
{
  struct feature* ft;
  struct feature* oldft;

  ft = v->first;
  while (ft != NULL) {
    freefeature(ft->item, ft->type);
    oldft = ft;
    ft = ft->next;
    free(oldft);
  };
  if (v->keysig != NULL) {
    freekey(v->keysig);
    v->keysig = NULL;
  };
  if (v->tempo != NULL) {
    freetempo(v->tempo);
    v->tempo = NULL;
  };
  if (v->clef != NULL) {
    free(v->clef);
  };
  v->clef = NULL;
}

static void freetune(struct tune* t)
/* free up all dynamically allocated memory associated with tune */
{
  struct voice* v;

  if (t->composer != NULL) {
    free(t->composer);
    t->composer = NULL;
  };
  if (t->origin != NULL) {
    free(t->origin);
    t->origin = NULL;
  };
  if (t->parts != NULL) {
    free(t->parts);
    t->parts = NULL;
  };
  freellist(&t->title);
  freellist(&t->notes);
  if (t->keysig != NULL) {
    freekey(t->keysig);
    t->keysig = NULL;
  };
  if (t->tempo != NULL) {
    freetempo(t->tempo);
    t->tempo = NULL;
  };
  v = firstitem(&t->voices);
  while (v != NULL) {
    freevoice(v);
    v = nextitem(&t->voices);
  };
  freellist(&t->voices);
  freellist(&t->words);
}

static int checkmatch(int refno)
/* compares current reference number against list of numbers */
/* following -e argument */
/* returns 1 if tune has been selected and 0 otherwise */
{
  int select;
  int n1, n2;
  char* place;

  place = matchstring;
  if (strlen(matchstring)==0) {
    select = 1;
  } else {
    select = 0;
    while ((select==0)&&(*place >= '0')&&(*place <='9')) {
      n1 = readnump(&place);
      if (n1 == refno) {
        select = 1;
      } else {
        if (*place == ',') {
          place = place+1;
        } else {
          if (*place == '-') {
            place = place+1;
            n2 = readnump(&place);
            if ((refno >= n1)&&(refno <= n2)) {
              select = 1;
            } else {
              if (*place == ',') {
                place = place+1;
              };
            };
          };
        };
      };
    };
    if ((select==0)&&(*place != '\0')) {
      event_warning("Number list after -e not fully parsed");
    };
  };    
  return(select);
}

void event_init(argc, argv, filename)
int argc;
char* argv[];
char** filename;
/* initialization routine - called once at the start of the program */
/* interprets the parameters in argv */
{
  char* place;
  int filearg;
  int refmatch;
  int papsize, margins, newscale;
  int ier;

  if (getarg("-d", argc, argv) != -1) {
    debugging = 1;
  } else {
    debugging = 0;
  };
  if (getarg("-E", argc, argv) != -1) {
    eps_out = 1;
  } else {
    eps_out = 0;
  };
  if (getarg("-V", argc, argv) != -1) {
    separate_voices = 1;
  } else {
    separate_voices = 0;
  };
  if (getarg("-x", argc, argv) != -1) {
    print_xref = 1;
  } else {
    print_xref = 0;
  };
  if (getarg("-N", argc, argv) != -1) {
    pagenumbering = 1;
  } else {
    pagenumbering = 0;
  };
  if (getarg("-l", argc, argv) != -1) {
    landscape = 1;
  } else {
    landscape = 0;
  };
  newscale = getarg("-s", argc, argv);
  if ((newscale != -1) && (argc >= newscale)) {
    setscaling(argv[newscale]);
  } else {
    setscaling("");
  };
  margins = getarg("-M", argc, argv);
  if ((margins != -1) && (argc >= margins)) {
    setmargins(argv[margins]);
  } else {
    setmargins("");
  };
  papsize = getarg("-P", argc, argv);
  if ((papsize != -1) && (argc >= papsize)) {
    setpagesize(argv[papsize]);
  } else {
    setpagesize("");
  };
  barnums = getarg("-k",argc,argv);
  ier = 0;
  if ((barnums != -1) && (argc > barnums)) ier = sscanf(argv[barnums],"%d",&nnbars);
  if ((barnums != -1) && (ier <1)) nnbars = 1;

  refmatch = getarg("-e", argc, argv);
  if (refmatch == -1) {
    *matchstring = '\0';
  } else {
    if (strlen(argv[refmatch]) < 255) {
      strcpy(matchstring, argv[refmatch]);
    } else {
      event_error("Exceeded character limit for -e string");
      exit(1);
    };
  };
  if ((getarg("-h", argc, argv) != -1) || (argc < 2)) {
    printf("yaps version 1.22\n");
    printf("Usage:  yaps <abc file> [<options>]\n");
    printf("  possible options are -\n");
    printf("  -d            : debug - display data structure\n");
    printf("  -e <list>     : draw tunes with reference numbers in list\n");
    printf("     list is comma-separated and may contain ranges\n");
    printf("     but no spaces e.g. 1,3,7-20\n");
    printf("  -E            : generate Encapsulated PostScript\n");
    printf("  -l            : landscape mode\n");
    printf("  -M XXXxYYY    : set margin sizes in points\n");
    printf("     28.3 points = 1cm, 72 points = 1 inch\n");
    printf("  -N            : add page numbering\n");
    printf("  -k [nn]       : number every nn bars\n");
    printf("  -o <filename> : specify output file\n");
    printf("  -P ss         : paper size; 0 is A4, 1 is US Letter\n");
    printf("     or XXXxYYY to set size in points\n");
    printf("  -s XX         : scaling factor (default is 0.7)\n");
    printf("  -V            : separate voices in multi-voice tune\n");
    printf("  -x            : print tune number in X: field\n");
    printf("Takes an abc music file and converts it to PostScript.\n");
    printf("If no output filename is given, then by default it is\n");
    printf("the input filename but with extension .ps .\n");
    exit(0);
  } else {
    *filename = argv[1];
  };
  fileopen = 0;
  filearg = getarg("-o", argc, argv);
  if (filearg != -1) { 
    strcpy(outputname, argv[filearg]);
  } else {
    strcpy(outputname, argv[1]);
    place = strchr(outputname, '.');
    if (place == NULL) {
      strcat(outputname, ".ps");
    } else {
      strcpy(place, ".ps");
    };
    if (strcmp(argv[1], outputname)==0) {
      printf("argument must be abc file, not PostScript file\n");
      exit(1);
    };
  };
  /* create filename root for EPS output */
  strcpy(outputroot, outputname);
  place = strchr(outputroot, '.');
  if (place != NULL) {
    *place = '\0';
  };
  xinbody =0;
  xinhead = 0;
  suppress = 0;
  init_tune(&thetune, -1);
  setup_fonts();
  /* open_output_file(outputname); */
}

static void check_voice_end(struct voice* v)
/* make sure there are no unfinished structures */
{
  if (v->inchord) {
    event_error("incomplete chord at end of voice");
    v->inchord = 0;
  };
  if (v->brokenpending != -1) {
    event_error("incomplete broken rhythm at end of voice");
    v->brokenpending = -1;
  };
  if ((v->slurcount > 0)||(v->slurpending)) {
    event_error("incomplete slur at end of voice");
    v->slurcount = 0;
    v->slurpending = 0;
  };
  if (v->tiespending) {
    event_error("incomplete ties at end of voice");
    v->tiespending = 0;
  };
  if (v->gchords_pending != NULL) {
    freellist(v->gchords_pending);
    free(v->gchords_pending);
    v->gchords_pending = NULL;
  };
  if (v->instructions_pending != NULL) {
    freellist(v->instructions_pending);
    free(v->instructions_pending);
    v->instructions_pending = NULL;
  };
  if (v->tuplenotes > 0) {
    event_error("incomplete tuple at end of voice");
    v->tuplenotes = 0;
  };
}

static void check_tune_end(struct tune* t)
/* check that all voices have been completed properly */
{
  struct voice* v;

  v = firstitem(&t->voices);
  while (v != NULL) {
    check_voice_end(v);
    v = nextitem(&t->voices);
  };
}

void event_eof()
/* end of input file has been encountered */
{
  if (xinbody) {
    check_tune_end(&thetune);
    printtune(&thetune);
  };
  freetune(&thetune);
  close_output_file();
}

void event_blankline()
/* A blank line has been encountered */
{
  if (xinbody) {
    check_tune_end(&thetune);
    printtune(&thetune);
  };
  freetune(&thetune);
  xinbody = 0;
  xinhead = 0;
  suppress = 0;
  parseroff();
}

void event_text(p)
/* Text outside an abc tune has been encountered */
char *p;
{
}

void event_x_reserved(p)
char p;
{
}

void event_abbreviation(symbol, string, container)
/* abbreviation declaratiion - handled by parser. Ignore it here */
char symbol;
char *string;
char container;
{
}

void event_tex(s)
char *s;
/* A TeX command has been found in the abc */
{
}

void event_linebreak()
/* A linebreak has been encountered */
{
  if (xinbody) {
    addfeature(LINENUM, (void*)lineno);
  };
}

static void tidy_ties()
/* create CLOSE_TIE features for ties straddling two lines of music */
/* CLOSE_TIE appears in the new music line */
{
  struct feature* ft;
  struct slurtie* s;
  int i;

  for (i=0; i<cv->tiespending; i++) {
    ft = cv->tie_place[i]; /* pointer to TIE feature */
    s = ft->item;
    addfeature(CLOSE_TIE, s);
  };
}

void event_startmusicline()
/* We are at the start of a line of abc notes */
{
  cv->linestart = addfeature(MUSICLINE, (void*)NULL);
  if (cv->more_lyrics != 0) {
    event_error("Missing continuation w: field");
    cv->more_lyrics = 0;
  };
  if ((cv->line == header) || (cv->line == newline)) {
    addfeature(CLEF, newclef(cv->clef->type, cv->clef->octave));
    addfeature(KEY, newkey(cv->keysig->name, cv->keysig->sharps,
                           cv->keysig->map, cv->keysig->mult));
    if ((cv->line == header)||(cv->changetime)) {
      addfeature(TIME, newfract(cv->meter.num, cv->meter.denom));
      cv->changetime = 0;
    };
    cv->line = midline;
    tidy_ties();
  };
  cv->lineend = NULL;
}

static void divide_ties()
/* mark unresolved ties and slurs as straddling two lines of music */
{
  struct feature* ft;
  struct slurtie* s;
  int i;

  for (i=0; i<cv->tiespending; i++) {
    ft = cv->tie_place[i]; /* pointer to TIE feature */
    s = ft->item;
    s->crossline = 1;
  };
  for (i=0; i<cv->slurcount; i++) {
    s = cv->slur_place[i];
    s->crossline = 1;
  };
}

void event_endmusicline(endchar)
char endchar;
/* We are at the end of a line of abc notes */
{
  cv->lineend = addfeature(MUSICSTOP, (void*)NULL);
  if ((endchar == ' ') || (endchar == '!')) {
    addfeature(PRINTLINE, newvertspacing());
    cv->line = newline;
    divide_ties();
  };
}

void event_error(s)
char *s;
/* report any error message */
{
  printf("Error in line %d : %s\n", lineno, s);
}

void event_warning(s)
char *s;
/* report any warning message */
{
  printf("Warning in line %d : %s\n", lineno, s);
}

void event_comment(s)
char *s;
/* A comment has been encountered in the input */
{
}

int make_open()
/* called as   fileopen = make_open()                    */
/* if file is not already open, open it and set fileopen */
{
  if (fileopen == 0) {
    open_output_file(outputname, (void*)NULL);
    fileopen = 1;
  };
  return(1);
}

void event_specific(p, str)
char *p;   /* first word after %% */
char *str; /* string following first word */
/* The special comment %% has been found */
/* abc2midi uses it for the %%MIDI commands */
/* yaps implements some of the abc2ps commands */
{
  char* s;
  double vspace;
  char units[80];
  int count;

  /* ensure file has been opened since most commands require this */
  if (!eps_out) {
    fileopen = make_open();
  };
  s = str;
  skipspace(&s);
  if (strcmp(p, "newpage") == 0) {
    if (xinbody == 0) {
      if (fileopen) {
        newpage();
      };
    } else {
      addfeature(NEWPAGE, (void*)NULL);
    };
  };
  if (strcmp(p, "text") == 0) {
    if (xinbody == 0) {
      if (fileopen) {
        lefttext(s);
      };
    } else {
      addfeature(LEFT_TEXT, addstring(s));
    };
  };
  if ((strcmp(p, "centre") == 0) || (strcmp(p, "center") == 0)) {
    if (xinbody == 0) {
      if (fileopen) {
        centretext(s);
      };
    } else {
      addfeature(CENTRE_TEXT, addstring(s));
    };
  };
  if (strcmp(p, "vskip") == 0) {
    count = sscanf(s, "%lf%s", &vspace, units);
    if (count > 0) {
      if ((count >= 2) && (strncmp(units, "cm", 2) == 0)) {
        vspace = vspace*28.3;
      };
      if ((count >= 2) && (strncmp(units, "in", 2) == 0)) {
        vspace = vspace*72.0 ;
      };
      if (xinbody == 0) {
        if (fileopen) {
          vskip(vspace);
        };
      } else {
        addfeature(VSKIP, (void*)((int)vspace));
      };
    };
  };
  if (strcmp(p, "chordsabove") == 0) {
    gchords_above = 1;
  };
  if (strcmp(p, "chordsbelow") == 0) {
    gchords_above = 0;
  };
  /* look for font-related commands */
  font_command(p, s);
}

void event_field(k, f)
char k;
char *f;
/* A field line has been encountered in the input abc */
{
  switch (k) {
  case 'T':
    if (debugging) {
      printf("T:%s\n", f);
    };
    addtolist(&thetune.title, addstring(f));
    break;
  case 'C':
    if (thetune.composer != NULL) {
      event_error("More than one C: field in tune");
    } else {
      thetune.composer = addstring(f);
    };
    break;
  case 'O':
    if (thetune.origin != NULL) {
      event_error("More than one O: field in tune");
    } else {
      thetune.origin = addstring(f);
    };
    break;
  case 'W':
    if (debugging) {
      printf("W:%s\n", f);
    };
    addtolist(&thetune.words, addstring(f));
    break;
  case 'N':
    addtolist(&thetune.notes, addstring(f));
    break;
  default:
    break;
  };
}

struct feature* findbar(struct feature* wordplace, int* errors)
/* hunts through voice to find next bar data structure */
{
  struct feature* ft;

  ft = wordplace;
  while ((ft != NULL) && (ft != cv->lineend) && (ft->type != MUSICSTOP) &&
         (ft->type != SINGLE_BAR) && (ft->type != DOUBLE_BAR) &&
         (ft->type != BAR_REP) && (ft->type != REP_BAR) &&
         (ft->type != DOUBLE_REP) && (ft->type != THICK_THIN) &&
         (ft->type != THIN_THICK) && (ft->type != BAR1) &&
         (ft->type != REP_BAR2)) {
    ft = ft->next;
  };
  if ((ft == NULL) || (ft == cv->lineend) || (ft->type == MUSICSTOP)) {
    *errors = *errors + 1;
  } else {
    ft = ft->next;
  };
  return(ft);
}

struct feature* apply_syll(char* s, struct feature* wordplace, int* errors)
/* attaches a syllable from the lyrics to the appropriate note */
{
  struct note* n;
  struct feature* ft;
  int inchord;

  ft = wordplace;
  inchord = 0;
  while ((ft != NULL) && (ft != cv->lineend) &&
         (ft->type != NOTE)) {
    if (ft->type == CHORDON) {
      inchord = 1;
    };
    ft = ft->next;
    /* skip over any grace notes */
    if ((ft != NULL) && (ft->type == GRACEON)) {
      while ((ft != NULL) && (ft != cv->lineend) &&
         (ft->type != GRACEOFF)) {
        ft = ft->next;
      };
    };
  };
  if ((ft == NULL) || (ft == cv->lineend)) {
    *errors = *errors + 1;
    return(ft);
  };
  if (ft->type == NOTE) {
    n = ft->item;
    if (n->syllables == NULL) {
      n->syllables = newlist();
    };
/*
    if (strlen(s) < 80) {
      ISOdecode(s, isocode);
      addtolist(n->syllables, addstring(isocode));
    } else {
*/
      addtolist(n->syllables, addstring(s));
/*
    };
*/
  };
  ft = ft->next;
  /* skip over any chord */
  if (inchord) {
    while ((ft != NULL) && (ft->type != CHORDOFF) && (ft != cv->lineend)) {
      ft = ft->next;
    };
  };
  return(ft);
}

void event_words(p, continuation)
char* p;
int continuation;
/* A line of lyrics (w: ) has been encountered in the abc */
{
  struct vstring syll;
  char* q;
  struct feature* wordplace;
  unsigned char ch;
  int errors;

  if (!xinbody) {
    if (!suppress) {
      event_error("w: field outside tune body");
    };
    return;
  };
  wordplace = cv->linestart;
  if (cv->more_lyrics) {
    errors = cv->lyric_errors;
  } else {
    errors = 0;
  };
  if (wordplace == NULL) {
    event_error("No notes to match words");
    return;
  };
  initvstring(&syll);
  q = p;
  skipspace(&q);
  while (*q != '\0') {
    clearvstring(&syll);
    ch = *q;
    while(ch=='|') {
      wordplace = findbar(wordplace, &errors);
      q++;
      ch = *q;
    };
    while (((ch>127)||isalnum(ch)||ispunct(ch))&&
           (ch != '_')&&(ch != '-')&&(ch != '*')&& (ch != '|')) {
      if (ch == '~') {
        ch = ' ';
      };
      if ((ch == '\\') && (*(q+1)=='-')) {
        ch = '-';
        q++;
      };
      /* syllable[i] = ch; */
      addch(ch, &syll);
      q++;
      ch = *q;
    };
    skipspace(&q);
    if (ch == '-') {
      addch(ch, &syll);
      while (isspace(ch)||(ch=='-')) {
        q++;
        ch = *q;
      };
    };
    if (syll.len > 0) {
      wordplace = apply_syll(syll.st, wordplace, &errors);
    } else {
      if (ch=='_') {
        clearvstring(&syll);
        addch('_', &syll);
        wordplace = apply_syll(syll.st, wordplace, &errors);
        q++;
        ch = *q;
      };
      if (ch=='*') {
        clearvstring(&syll);
        addch(' ', &syll);
        wordplace = apply_syll(syll.st, wordplace, &errors);
        q++;
        ch = *q;
      };
    }; 
  };
  if (continuation) {
    cv->more_lyrics = 1;
    cv->lyric_errors = errors;
    cv->linestart = wordplace;
  } else {
    cv->more_lyrics = 0;
    if (errors > 0) {
      event_error("Lyric line too long for music");
    } else {
      clearvstring(&syll);
      wordplace = apply_syll(syll.st, wordplace, &errors);
      if (errors == 0) {
        event_error("Lyric line too short for music");
      };
    };
  };
  freevstring(&syll);
}

void event_part(s)
char* s;
/* A part field (P: ) has been encountered in the abc */
{
  char label[20];

  if (xinhead) {
    if (thetune.parts != NULL) {
      event_error("Multiple P: fields in header");
    } else {
      thetune.parts = addstring(s);
    };
  };
  if (xinbody) {
    /* addfeature(PART, addstring(s)); */
    /* PART is handled like an instruction */
    if (cv->instructions_pending == NULL) {
      cv->instructions_pending = newlist();
    };
    sprintf(label, ":p%s", s);
    addtolist(cv->instructions_pending, addstring(label));
  };
}

void event_voice(n, s, gotclef, gotoctave,gottranspose,
	       	clefname, octave, transpose)
int n;
char *s;
int gotclef,gotoctave,gottranspose;
char *clefname;
int transpose,octave;
/* A voice field (V: ) has been encountered */
{
  if (xinbody) {
    setvoice(n);
  } else {
    if (!suppress) {
      event_error("V: field outside tune body");
    };
  };
}

void event_length(n)
int n;
/* A length field (L: ) has been encountered */
{
  if (xinhead) {
    setfract(&thetune.unitlen, 1, n);
  } else {
    if (xinbody) {
      setfract(&cv->unitlen, 1, n);
    } else {
      if (!suppress) {
        event_warning("L: field outside tune ignored");
      };
    };
  };
}

void event_refno(n)
int n;
/* A reference field (X: ) has been encountered. This indicates the start */
/* of a new tune */
{
  if (xinhead) {
    event_error("incomplete tune");
  };
  if (xinbody) {
    check_tune_end(&thetune);
    printtune(&thetune);
  };
  freetune(&thetune);
  xinbody = 0;
  xinhead = 0;
  suppress = 0;
  parseroff();
  if (debugging) {
    printf("X:%d\n", n);
  };
  if (checkmatch(n)) {
    parseron();
    /* fileopen = make_open(); */
    xinhead = 1;
    xinbody = 0;
    init_tune(&thetune, n);
  } else {
    suppress = 1;
  };
}

void event_tempo(n, a, b, relative, pre, post)
int n, a, b;
int relative;
char *pre; /* text before tempo */
char *post; /* text after tempo */
/* A tempo field Q: has been encountered in the abc */
/* Q:a/b=N will have relative = 0    */
/* Q:N will have a=0 and b=0         */
/* Q:Ca/b = N will have relative = 1 */
{
  if (xinhead) {
    thetune.tempo = newtempo(n, a, b, relative, pre, post);
  } else {
    if (xinbody) {
      if ((a == 0) && (b == 0)) {
        addfeature(TEMPO, newtempo(n, cv->unitlen.num, cv->unitlen.denom, 0,
                                pre, post));
      } else {
        addfeature(TEMPO, newtempo(n, a, b, relative, pre, post));
      };
    } else {
      if (!suppress) {
        event_warning("Q: field outside tune ignored");
      };
    };
  };
}

void event_timesig(n, m, checkbars)
int n, m, checkbars;
/* A time signature (M: ) has been encountered in the abc */
{
  if (xinhead) {
    setfract(&thetune.meter, n, m);
    thetune.barchecking = checkbars;
  } else {
    if (xinbody) {
      if (checkbars == 1) {
        addfeature(TIME, newfract(n,m));
        setfract(&cv->meter, n, m);
        setfract(&cv->barlen, n, m);
        if (cv->line != midline) {
          cv->changetime = 1;
        };
        cv->barchecking = 1;
      } else {
        cv->barchecking = 0;
      };
    } else {
      if (!suppress) {
        event_warning("M: field outside tune ignored");
      };
    };
  };
}

enum cleftype findclef(clefstr, oct)
char* clefstr;
int* oct;
/* converts a clef name string to an enumerated type */
/* also looks for +8 +15 +22 -8 -15 -22 octave shift */
{
  enum cleftype type;
  char* p;
  int interval;

  type = noclef;
  p = clefstr;
  if (strncmp(clefstr, "treble", 6)==0) {
    type = treble;
    p = p + 6;
  };
  if (strncmp(clefstr, "bass", 4)==0) {
    type = bass;
    p = p + 4;
  };
  if (strncmp(clefstr, "baritone", 8)==0) {
    type = baritone;
    p = p + 8;
  };
  if (strncmp(clefstr, "tenor", 5)==0) {
    type = tenor;
    p = p + 5;
  };
  if (strncmp(clefstr, "alto", 4)==0) {
    type = alto;
    p = p + 4;
  };
  if (strncmp(clefstr, "mezzo", 5)==0) {
    type = mezzo;
    p = p + 5;
  };
  if (strncmp(clefstr, "mezzo-soprano", 13)==0) {
    type = mezzo;
    p = p + 13;
  };
  if (strncmp(clefstr, "soprano", 7)==0) {
    type = soprano;
    p = p + 7;
  };
  interval = 0;
  if ((type != noclef) && ((*p == '+') || (*p == '-'))) {
    sscanf(p+1, "%d", &interval);
    if ((interval == 8) || (interval == 15) || (interval == 22)) {
      if (*p == '-') {
        interval = -interval;
      };
    } else {
      interval = 0;
    };
  };
  *oct = interval;
  return(type);
}

void event_clef(char* clefstr)
/* a clef has been encountered in the abc */
{
  enum cleftype clef;
  int num;

  clef = findclef(clefstr, &num);
  if (xinbody) {
    cv->clef->type = clef;
    cv->clef->octave = num;
    addfeature(CLEF, newclef(clef, num));
  };
  if ((xinhead) && (!xinbody)) {
    if (clef != noclef) {
       thetune.clef.type = clef;
       thetune.clef.octave = num;
    };
  };
}

void setmap(sf, map, mult)
/* work out accidentals to be applied to each note */
int sf; /* number of sharps in key signature -7 to +7 */
char map[7];
int mult[7];
{
  int j;

  for (j=0; j<7; j++) {
    map[j] = '=';
    mult[j] = 1;
  };
  if (sf >= 1) map['f'-'a'] = '^';
  if (sf >= 2) map['c'-'a'] = '^';
  if (sf >= 3) map['g'-'a'] = '^';
  if (sf >= 4) map['d'-'a'] = '^';
  if (sf >= 5) map['a'-'a'] = '^';
  if (sf >= 6) map['e'-'a'] = '^';
  if (sf >= 7) map['b'-'a'] = '^';
  if (sf <= -1) map['b'-'a'] = '_';
  if (sf <= -2) map['e'-'a'] = '_';
  if (sf <= -3) map['a'-'a'] = '_';
  if (sf <= -4) map['d'-'a'] = '_';
  if (sf <= -5) map['g'-'a'] = '_';
  if (sf <= -6) map['c'-'a'] = '_';
  if (sf <= -7) map['f'-'a'] = '_';
}

void altermap(basemap, basemul, modmap, modmul)
/* apply modifiers to a set of accidentals */
char basemap[7], modmap[7];
int basemul[7], modmul[7];
{
  int i;

  for (i=0; i<7; i++) {
    if (modmap[i] != ' ') {
      basemap[i] = modmap[i];
      basemul[i] = modmul[i];
    };
  };
}


void resolve_tempo(struct atempo* t, struct fract* unitlen)
/* Tempo may be given relative to the unit note length. At the start of */
/* the tune, we will know what this is and can deduce the tempo value.  */
/* Can also deduce tempo if it has been given as a number only.         */
{
  if (t->relative==1) {
    setfract(&t->basenote, t->basenote.num*unitlen->num, 
                           t->basenote.denom*unitlen->denom);
    reducef(&t->basenote);
    t->relative = 0;
  };
  if ((t->basenote.num == 0) && (t->basenote.denom == 0)) {
    setfract(&t->basenote, unitlen->num, unitlen->denom);
    reducef(&t->basenote);
  };
}

static void start_body()
/* We have reached the end of the header section and need to set */
/* default values for anything not explicitly declared */
{
  parseron();
  if (thetune.meter.num == 0) {
    event_warning("no M: field, assuming 4/4");
    /* generate missing time signature */
    event_timesig(4, 4, 1);
    event_linebreak();
  };
  if (thetune.unitlen.num == 0) {
    event_warning("no L: field, using default rule");
    if ((double) thetune.meter.num / (double) thetune.meter.denom < 0.75) {
      setfract(&thetune.unitlen, 1, 16);
    } else {
      setfract(&thetune.unitlen, 1, 8);
    };
  };
  if (thetune.tempo != NULL) {
    resolve_tempo(thetune.tempo, &thetune.unitlen);
  };
}

void event_true_key(sharps, s, minor, modmap, modmul)
int sharps;
char *s;
int minor;
char modmap[7];
int modmul[7];
/* key detected in K: field */
{
  char basemap[7];
  int basemul[7];
  struct key* akey;

  setmap(sharps, basemap, basemul);
  altermap(basemap, basemul, modmap, modmul);
  if (xinbody) {
    akey = newkey(s, sharps, basemap, basemul);
    addfeature(KEY, akey);
    set_keysig(cv->keysig, akey);
  };
  if (xinhead) {
    if (thetune.keysig == NULL) {
      thetune.keysig = newkey(s, sharps, basemap, basemul);
    } else {
      event_warning("Key specified twice");
    };
    xinbody = 1;
    xinhead = 0;
    setvoice(1);
    start_body();
  };
}

void event_octave(int num, int local)
/* deals with the special command I:octave=N */
{
  if (xinhead) {
    thetune.octaveshift = num;
  };
  if (xinbody) {
    cv->octaveshift = num;
  };
}

void event_key(sharps, s, minor, modmap, modmul, gotkey, gotclef, clefstr,
          octave, transpose, gotoctave, gottranspose)
int sharps;
char *s;
int minor;
char modmap[7];
int modmul[7];
int gotkey, gotclef;
char* clefstr;
int octave, transpose, gotoctave, gottranspose;
/* A key field (K: ) has been encountered */
{
  if (xinhead || xinbody) {
    if (gotclef==1) {
      event_clef(clefstr);
    };
    if (gotkey==1) {
      event_true_key(sharps, s, minor, modmap, modmul);
    };
  };
  if (gotoctave) {
    event_octave(octave,0);
  };
}

static void checkbar(int type)
/* Make sure that bar lasts for the correct musical time */
{
  int valid;
  char msg[80];
  int a1, a2;

  valid = 0;
  a1 = cv->barlen.num*cv->barcount.denom;
  a2 = cv->barcount.num*cv->barlen.denom;
  if (a1 == a2) {
    cv->barcount.num = 0;
    cv->barcount.denom = 1;
    cv->barno = cv->barno + 1;
  } else {
    if (((type==BAR_REP)||(type==REP_BAR)||(type==DOUBLE_REP)) &&
        (a1 > a2)) {
      /* do nothing */
    } else {
      if ((cv->barno > 0) && (cv->barcount.num != 0) &&
          (cv->barchecking != 0)) {
        sprintf(msg, "Bar %d is %d/%d not %d/%d", cv->barno, 
           cv->barcount.num, cv->barcount.denom,
           cv->barlen.num, cv->barlen.denom);
        event_warning(msg);
        cv->barcount.num = 0;
        cv->barcount.denom = 1;
        cv->barno = cv->barno + 1;
      } else {
        cv->barcount.num = 0;
        cv->barcount.denom = 1;
        cv->barno = cv->barno + 1;
      };
    };
  };
}

void event_bar(type, playonrep_list)
int type;
char* playonrep_list;
/* A bar has been encountered in the abc */
{
  if (cv->inchord) {
    event_warning("Bar line not permitted within chord");
    return;
  };
  checkbar(type); /* increment bar number if bar complete */
  addfeature(type, (void*)cv->barno); /* save bar number */
  switch(type) {
  case SINGLE_BAR:
    break;
  case DOUBLE_BAR:
    break;
  case THIN_THICK:
    break;
  case THICK_THIN:
    break;
  case BAR_REP:
    if ((cv->expect_repeat) && (repcheck)) {
      event_error("Expecting repeat, found |:");
    };
    cv->expect_repeat = 1;
    break;
  case REP_BAR:
    if ((!cv->expect_repeat) && (repcheck)) {
      event_error("No repeat expected, found :|");
    };
    cv->expect_repeat = 0;
    break;
  case BAR1:
    if ((!cv->expect_repeat) && (repcheck)) {
      event_error("found |1 in non-repeat section");
    };
    break;
  case REP_BAR2:
    if ((!cv->expect_repeat) && (repcheck)) {
      event_error("No repeat expected, found :|2");
    };
    cv->expect_repeat = 0;
    break;
  case DOUBLE_REP:
    if ((!cv->expect_repeat) && (repcheck)) {
      event_error("No repeat expected, found ::");
    };
    cv->expect_repeat = 1;
    break;
  };
  if ((playonrep_list != NULL) && (strlen(playonrep_list) > 0)) {
    event_playonrep(playonrep_list);
  };
}

void event_space()
/* A region of whitespace has been encountered */
{
  addfeature(NOBEAM, NULL);
}

void event_graceon()
/* start of grace note(s) */
{
  if (cv->inchord) {
    event_error("grace notes not allowed within chord");
    return;
  };
  if (cv->ingrace) {
    event_error("nested grace notes not allowed");
    return;
  };
  cv->gracebeamroot = NULL;
  cv->gracebeamend = NULL;
  cv->ingrace = 1;
  addfeature(GRACEON, NULL);
}

void event_graceoff()
/* end of grace note(s) */
{
  if (!cv->ingrace) {
    event_error("No grace notes to close");
    return;
  };
  if (cv->inchord) {
    event_error("cannot close grace notes within chord");
    return;
  };
  addfeature(GRACEOFF, NULL);
  closegracebeam(cv);
  cv->ingrace = 0;
  cv->gracebeamroot = NULL;
  cv->gracebeamend = NULL;
}

void event_rep1()
/* start of first repeat */
{
  addfeature(REP1, NULL);
}

void event_rep2()
/* start of second repeat */
{
  addfeature(REP2, NULL);
}

void event_playonrep(s)
char* s;
/* play on repeat(s) X - where X can be a list */
{
  addfeature(PLAY_ON_REP, addstring(s));
}

void event_broken(type, mult)
/* handles > >> >>> < << <<< in the abc */
int type, mult;
{
  if (cv->inchord) {
    event_error("Broken rhythm not allowed in chord");
  } else {
    if (cv->ingrace) {
      event_error("Broken rhythm not allowed in grace notes");
    } else {
      cv->brokentype = type;
      cv->brokenmult = mult;
      cv->brokenpending = 0;
    };
  };
}

void event_tuple(n, q, r)
int n, q, r;
/* Start of a tuple has been  encountered (e.g. triplet) */
/* Meaning is "play next r notes at q/n of notated value" */
/* where all 3 exist, otherwise r defaults to n and ratio */
/* is deduced from standard rules if q is missing */
{
  if (cv->tuplenotes != 0) {
    event_error("tuple within tuple not allowed");
    return;
  };
  if (r != 0) {
    cv->tuplenotes = r;
  } else {
    cv->tuplenotes = n;
  };
  if (q != 0) {
    cv->tuplefactor.num = q;
    cv->tuplefactor.denom = n;
  } else {
    cv->tuplefactor.denom = n;
    if ((n == 2) || (n == 4) || (n == 8)) cv->tuplefactor.num = 3;
    if ((n == 3) || (n == 6)) cv->tuplefactor.num = 2;
    if ((n == 5) || (n == 7) || (n == 9)) {
      if ((cv->barlen.num % 3) == 0) {
        cv->tuplefactor.num = 3;
      } else {
        cv->tuplefactor.num = 2;
      };
    };
  };
  cv->thistuple = newtuple(cv->tuplefactor.denom, cv->tuplefactor.num, 
                           cv->tuplenotes, n);
  addfeature(TUPLE, cv->thistuple);
}

void event_startinline()
{
}

void event_closeinline()
{
}

void event_handle_gchord(s)
char* s;
/* Guitar/Accompaniment chord placed in linked list for association */
/* with next suitable note */
{
  if (cv->gchords_pending == NULL) {
    cv->gchords_pending = newlist();
  };
  if (*s == '_') {
  if (cv->instructions_pending == NULL) {
    cv->instructions_pending = newlist();
    };
  addtolist(cv->instructions_pending, addstring(s+1));
  } else {
    addtolist(cv->gchords_pending, addstring(s));
  };
}

void event_handle_instruction(s)
char* s;
/* An instruction (! !) has been encountered */
{
  char* inst;
  static char segno[3] = ":s";
  static char coda[3] = ":c";
 
  inst = s;
  if (strcmp(s, "fermata") == 0)
     {
     decorators_passback[4] =1;
/*   don't show !fermata!. Treat it like H in music line */
     return;
     }

  if (strcmp(s, "trill") == 0)
     {
     decorators_passback[6] =1;
/*   don't show !trill!. Treat it like T in music line */
     return;
     }

  if (strcmp(s, "segno") == 0) {
    inst = segno;
  };
  if (strcmp(s, "coda") == 0) {
    inst = coda;
  };
  if (cv->instructions_pending == NULL) {
    cv->instructions_pending = newlist();
  };
  addtolist(cv->instructions_pending, addstring(inst));
}

struct slurtie* resolve_slur(struct feature* lastnote)
/* when an end-of-slur marker ')' is found, this routine works out */
/* where the other end of the slur was */
{
  int i;
  int resolved;
  struct slurtie* s;

  resolved = 0;
  if (lastnote != NULL) {
    for (i=0; i<cv->slurcount; i++) {
      if ((resolved == 0) && (cv->slur_place[i]->begin != NULL) &&
          (cv->slur_place[i]->begin != lastnote)) {
        resolved = 1;
        cv->slur_place[i]->end = lastnote;
        s = cv->slur_place[i];
        cv->slur_place[i] = cv->slur_place[cv->slurcount-1];
        cv->slurcount = cv->slurcount - 1;
      };
    };  
  };
  if (resolved == 0) {
    event_error("Could not find start of slur to match close slur");
    s = NULL;
  };
  return(s);
}

static void startslurs(struct feature* firstnote)
/* When a note is found after a (, set note to be start of slur */
/* Note may be the start of more than one slur */
{
  int i;

  for (i=0; i<cv->slurcount; i++) {
    if (cv->slur_place[i]->begin == NULL) {
      cv->slur_place[i]->begin = firstnote;
    };
  };
}

void event_sluron(t)
int t;
/* start of slur */
{
  struct slurtie* s;

  s = newslurtie();
  addfeature(SLUR_ON, s);
  cv->slurpending = 1;
  if (cv->slurcount < MAX_SLURS) {
    cv->slur_place[cv->slurcount] = s;
    cv->slurcount = cv->slurcount+1;
  } else {
    event_error("Static limit on number of slurs exceeded");
  };
}

void event_sluroff(t)
int t;
/* end of slur */
{
  struct slurtie* s;

  s = resolve_slur(cv->lastnote);
  addfeature(SLUR_OFF, s);
}

static void resolve_ties(struct feature* f)
/* try to match up note to previous tied note */
{
  struct feature* ft;
  struct slurtie* s;
  struct note* m;
  struct note* n;
  int i, j;

  n = f->item;
  for (i=0; i<cv->tiespending; i++) {
    ft = cv->tie_place[i]; /* pointer to TIE feature */
    s = ft->item;
    ft = s->begin; /* pointer to NOTE feature */
    m = ft->item;
    if (m->y == n->y) { /* pitch match found */
      s->end = f;
      j = cv->tiespending;
      cv->tie_place[i] = cv->tie_place[j-1];
      cv->tie_status[i] = cv->tie_status[j-1];
      cv->tiespending = j-1;
    };
  };
}

static void advance_ties()
/* deal with unresolved tied notes as musical time advances */
{
  int i, j;

  for (i=0; i<cv->tiespending; i++) {
    cv->tie_status[i]++;
  };
  i = 0;
  j = cv->tiespending;
  while (i < j) {
    if (cv->tie_status[i] >= 2) {
      event_error("No note to tie to");
      cv->tie_place[i] = cv->tie_place[j-1];
      cv->tie_status[i] = cv->tie_status[j-1];
      j = j - 1;
    } else {
      i = i + 1;
    };
  };
  cv->tiespending = j;
}

void event_tie()
/* tie encountered in the abc */
{
  struct slurtie* s;
  struct feature* place;
  int i;

  if ((cv->lastnote == NULL)||(cv->lastnote->type != NOTE)) {
    event_error("No note to tie");
  } else {
    s = newslurtie();
    place = addfeature(TIE, s);
    s->begin = cv->lastnote;
    cv->lastnote = NULL;
    i = cv->tiespending;
    if (i < MAX_TIES) {
      cv->tie_place[i] = place;
      cv->tie_status[i] = 0;
      cv->tiespending = cv->tiespending + 1;
    } else {
      event_error("Internal limit on ties exceeded");
    };
  };
}

void event_lineend(ch, n)
char ch;
int n;
/* Line ending with n copies of special character ch */
{
}

static void lenmul(n, a, b)
/* multiply note length by a/b */
struct feature* n;
int a, b;
{
  struct note *anote;
  struct rest* arest;
  struct fract* afract;

  if (n->type == NOTE) {
    anote =  n->item;
    afract = &anote->len;
    afract->num = afract->num * a;
    afract->denom = afract->denom * b;
    reducef(afract);
    /* re-calculate base, base_exp and dots */
    anote->dots = count_dots(&anote->base, &anote->base_exp, 
                             anote->len.num, anote->len.denom);
  };
  if (n->type == REST) {
    arest =  n->item;
    afract = &arest->len;
    afract->num = afract->num * a;
    afract->denom = afract->denom * b;
    reducef(afract);
    /* re-calculate base, base_exp and dots */
    arest->dots = count_dots(&arest->base, &arest->base_exp, 
                             arest->len.num, arest->len.denom);
  };
}

struct fract* getlenfract(struct feature *f)
/* find fractional length of NOTE or REST */
{
  struct fract *len;
  struct note *anote;
  struct rest *arest;

  len = NULL;
  if (f->type == NOTE) {
    anote = f->item;
    len = &(anote->len);
  };
  if (f->type == REST) {
    arest = f->item;
    len = &(arest->len);
  };
  return(len);
}

static void brokenadjust()
/* adjust lengths of broken notes */
{
  int num1, num2, denom12;
  struct feature* j;
  struct fract* fr1;
  struct fract* fr2;
  int temp;
  int failed;
  int done;

  switch(cv->brokenmult) {
    case 1:
      num1 = 3;
      num2 = 1;
      break;
    case 2:
      num1 = 7;
      num2 = 1;
      break;
    case 3:
      num1 = 15;
      num2 = 1;
      break;
  };
  denom12 = (num1 + num2)/2;
  if (cv->brokentype == LT) {
    temp = num1;
    num1 = num2;
    num2 = temp;
  };
  failed = 0;
  if ((cv->laststart == NULL) || (cv->lastend == NULL) || 
      (cv->thisstart == NULL) || (cv->thisend == NULL)) {
    failed = 1;
  } else {
    /* check for same length notes */
    fr1 = getlenfract(cv->laststart);
    fr2 = getlenfract(cv->thisstart);
/*
    fr1 = cv->laststart->item;
    fr2 = cv->thisstart->item;
*/
    if ((fr1->num * fr2->denom) != (fr2->num * fr1->denom)) {
      failed = 1;
    };
  };
  if (failed) {
    event_error("Cannot apply broken rhythm");
  } else {
/*
    printf("Adjusting %d to %d and %d to %d\n",
           cv->laststart, cv->lastend, cv->thisstart, cv->thisend);
*/
    j = cv->laststart;
    done = 0;
    while (done == 0) {
      lenmul(j, num1, denom12);
      done = (j == cv->lastend);
      j = j->next; 
    };
    j = cv->thisstart;
    done = 0;
    while (done == 0) {
      lenmul(j, num2, denom12);
      done = (j == cv->thisend);
      j = j->next; 
    };
  };
}

static void marknotestart(struct feature* place)
/* voice data structure keeps a record of last few notes encountered */
/* in order to process broken rhythm. This is called at the start of */
/* a note or chord */
{
  cv->laststart = cv->thisstart;
  cv->lastend = cv->thisend;
  cv->thisstart = place;
}

static void marknoteend(struct feature* place)
/* voice data structure keeps a record of last few notes encountered */
/* in order to process broken rhythm. This is called at the end of */
/* a note or chord */
{
  cv->thisend = place;
  if (cv->brokenpending != -1) {
    cv->brokenpending = cv->brokenpending + 1;
    if (cv->brokenpending == 1) {
      brokenadjust();
      cv->brokenpending = -1;
    };
  };
}

static void marknote(struct feature* place)
/* when handling a single note, not a chord, marknotestart() and */
/* marknoteend() can be called together */
{
  marknotestart(place);
  marknoteend(place);
}

static void markchord(struct feature* chordplace)
/* find start and end of chord for applying broken rhythm */
{
  struct feature* first;
  struct feature* last;

  first = chordplace->next;
  last = first;
  while ((last->next != NULL)&&(last->next->type==NOTE)) {
    last = last->next;
  };
  marknotestart(first);
  marknoteend(last);
}

void event_chord()
/* handles old '+' notation which marks the start and end of each chord */
{
    if (cv->inchord) {
      event_chordoff();
    } else {
      event_chordon();
    };
}

void event_chordon()
/* start of a chord */
{
  if (cv->inchord) {
    event_error("nested chords found");
    return;
  };
  cv->inchord = 1;
  cv->chordcount = 0;
  cv->thischord = newchord();
  cv->chordplace = addfeature(CHORDON, cv->thischord);
}

void event_chordoff()
/* end of a chord */
{
  struct feature* ft;
  struct chord* thechord;
  struct note* firstnote;

  if (!cv->inchord) {
    event_error("no chord to close");
    return;
  };
  ft = cv->chordplace;
  if ((ft != NULL) && (ft->next != NULL) && (ft->next->type == NOTE)) {
    thechord = ft->item;
    firstnote = ft->next->item;
    /* beaming for 1st note in chord */
    beamitem(NOTE, firstnote, ft->next);
    markchord(ft);
    thechord->base = firstnote->base;
    thechord->base_exp = firstnote->base_exp;
  } else {
    event_error("mis-formed chord");
  };
  cv->inchord = 0;
  cv->thischord = NULL;
  cv->chordplace = NULL;
  advance_ties();
  addfeature(CHORDOFF, NULL);
}

void xevent_rest(n, m, multi)
int n, m, multi;
/* A rest has been encountered in the abc */
/* multi is 0 for ordinary rests or count for multibar rests */
{
  struct feature* restplace;

  if (cv->ingrace) {
    event_warning("rest in grace notes ignored");
    return;
  };
  if (cv->inchord) {
    event_warning("rest in chord ignored");
    return;
  };
  if ((multi > 0) && (cv->tuplenotes > 0)) {
    event_error("Multiple bar rest not allowed in tuple");
  };
  advance_ties();
  restplace = addfeature(REST, newrest(n*cv->unitlen.num, 
                                         m*cv->unitlen.denom, multi));
  if (cv->slurpending) {
    startslurs(restplace);
    cv->slurpending = 0;
  };
  cv->lastnote = restplace;
  if (cv->inchord) {
    cv->chordcount = cv->chordcount + 1;
  } else {
    marknote(restplace); 
  };
  if ((!cv->ingrace) && (!cv->inchord || (cv->chordcount == 1))) {
    if (cv->tuplenotes == 0) {
      if (multi == 0) {
        addunits(&cv->barcount, n, m);
      };
    } else {
      addunits(&cv->barcount, n*cv->tuplefactor.num, m*cv->tuplefactor.denom);
      cv->thistuple->beamed = 0;
      cv->tuplenotes = cv->tuplenotes - 1;
      if (cv->tuplenotes == 0) {
        cv->thistuple = NULL;
      };
    };
  };
}

void event_rest(decorators,n,m,type)
int n, m,type;
int decorators[DECSIZE];
/* A rest has been encountered in the abc */
{
  xevent_rest(n, m, 0);
}

void event_mrest(n,m)
int n, m;
/* A multiple bar rest has been encountered in the abc */
{
  xevent_rest(1, 1, n);
}

void event_note(decorators, xaccidental, xmult, xnote, xoctave, n, m)
int decorators[DECSIZE];
int xmult;
char xaccidental, xnote;
int xoctave, n, m;
/* note found in abc */
{
  struct note* nt;
  struct feature* noteplace;
  struct chord* thechord;
  int pitchval;

  nt = newnote(decorators, xaccidental, xmult, xnote, xoctave+cv->octaveshift, 
               n * cv->unitlen.num, m * cv->unitlen.denom);
  nt->tuplenotes = cv->tuplenotes;
  noteplace = addfeature(NOTE, nt);
  cv->lastnote = noteplace;
  resolve_ties(noteplace);
  if (cv->slurpending) {
    startslurs(noteplace);
    cv->slurpending = 0;
  };
  if (!cv->inchord) {
    marknote(noteplace);
    advance_ties();
  } else {
    thechord = cv->thischord;
    pitchval = notenum(xoctave, xnote, cv->clef->type, cv->clef->octave);
    if (cv->chordcount == 0) {
      thechord->ytop = pitchval;
      thechord->ybot = pitchval;
    } else {
      if (pitchval > thechord->ytop) {
        thechord->ytop = pitchval;
      };
      if (pitchval < thechord->ybot) {
        thechord->ybot = pitchval;
      };
    };
    cv->chordcount = cv->chordcount + 1;
  };
  if ((!cv->ingrace) && 
      (!cv->inchord || (cv->chordcount == 1))) {
    if (cv->tuplenotes == 0) {
      addunits(&cv->barcount, n, m);
    } else {
      addunits(&cv->barcount, n*cv->tuplefactor.num, m*cv->tuplefactor.denom);
      if (nt->base_exp > -3) {
        cv->thistuple->beamed = 0;
      };
      cv->tuplenotes = cv->tuplenotes - 1;
      if (cv->tuplenotes == 0) {
        cv->thistuple = NULL;
        /* prevent beaming from tuple to non-tuple notes */
        addfeature(NOBEAM, NULL);
      };
    };
  };
}

void event_info_key(key, value)
char* key;
char* value;
/* handles a (key,value) pair found in an I: field */
{
  int num;

  if (strcmp(key, "clef")==0) {
    event_clef(value);
  };
  if (strcmp(key, "octave")==0) {
    num = readsnumf(value);
    event_octave(num,0);
  };
}

