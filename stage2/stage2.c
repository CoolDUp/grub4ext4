/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2004,2005  Free Software Foundation, Inc.
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <shared.h>
#include <term.h>

grub_jmp_buf restart_env;

gfx_data_t *graphics_data;

#if defined(PRESET_MENU_STRING) || defined(SUPPORT_DISKLESS)

# if defined(PRESET_MENU_STRING)
static const char *preset_menu = PRESET_MENU_STRING;
# elif defined(SUPPORT_DISKLESS)
/* Execute the command "bootp" automatically.  */
static const char *preset_menu = "bootp\n";
# endif /* SUPPORT_DISKLESS */

static int preset_menu_offset;

static int
open_preset_menu (void)
{
#ifdef GRUB_UTIL
  /* Unless the user explicitly requests to use the preset menu,
     always opening the preset menu fails in the grub shell.  */
  if (! use_preset_menu)
    return 0;
#endif /* GRUB_UTIL */
  
  preset_menu_offset = 0;
  return preset_menu != 0;
}

static int
read_from_preset_menu (char *buf, int maxlen)
{
  int len = grub_strlen (preset_menu + preset_menu_offset);

  if (len > maxlen)
    len = maxlen;

  grub_memmove (buf, preset_menu + preset_menu_offset, len);
  preset_menu_offset += len;

  return len;
}

static void
close_preset_menu (void)
{
  /* Disable the preset menu.  */
  preset_menu = 0;
}

#else /* ! PRESET_MENU_STRING && ! SUPPORT_DISKLESS */

#define open_preset_menu()	0
#define read_from_preset_menu(buf, maxlen)	0
#define close_preset_menu()

#endif /* ! PRESET_MENU_STRING && ! SUPPORT_DISKLESS */

static char *
get_entry (char *list, int num, int nested)
{
  int i;

  for (i = 0; i < num; i++)
    {
      do
	{
	  while (*(list++));
	}
      while (nested && *(list++));
    }

  return list;
}

/* Print an entry in a line of the menu box.  */
static void
print_entry (int y, int highlight, char *entry)
{
  int x;

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_NORMAL);
  
  if (highlight && current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_HIGHLIGHT);

  gotoxy (2, y);
  grub_putchar (' ');
  for (x = 3; x < 75; x++)
    {
      if (*entry && x <= 72)
	{
	  if (x == 72)
	    grub_putchar (DISP_RIGHT);
	  else
	    grub_putchar (*entry++);
	}
      else
	grub_putchar (' ');
    }
  gotoxy (74, y);

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_STANDARD);
}

/* Print entries in the menu box.  */
static void
print_entries (int y, int size, int first, int entryno, char *menu_entries)
{
  int i;
  
  gotoxy (77, y + 1);

  if (first)
    grub_putchar (DISP_UP);
  else
    grub_putchar (' ');

  menu_entries = get_entry (menu_entries, first, 0);

  for (i = 0; i < size; i++)
    {
      print_entry (y + i + 1, entryno == i, menu_entries);

      while (*menu_entries)
	menu_entries++;

      if (*(menu_entries - 1))
	menu_entries++;
    }

  gotoxy (77, y + size);

  if (*menu_entries)
    grub_putchar (DISP_DOWN);
  else
    grub_putchar (' ');

  gotoxy (74, y + entryno + 1);
}

static void
print_entries_raw (int size, int first, char *menu_entries)
{
  int i;

#define LINE_LENGTH 67

  for (i = 0; i < LINE_LENGTH; i++)
    grub_putchar ('-');
  grub_putchar ('\n');

  for (i = first; i < size; i++)
    {
      /* grub's printf can't %02d so ... */
      if (i < 10)
	grub_putchar (' ');
      grub_printf ("%d: %s\n", i, get_entry (menu_entries, i, 0));
    }

  for (i = 0; i < LINE_LENGTH; i++)
    grub_putchar ('-');
  grub_putchar ('\n');

#undef LINE_LENGTH
}


static void
print_border (int y, int size)
{
  int i;

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_NORMAL);
  
  gotoxy (1, y);

  grub_putchar (DISP_UL);
  for (i = 0; i < 73; i++)
    grub_putchar (DISP_HORIZ);
  grub_putchar (DISP_UR);

  i = 1;
  while (1)
    {
      gotoxy (1, y + i);

      if (i > size)
	break;
      
      grub_putchar (DISP_VERT);
      gotoxy (75, y + i);
      grub_putchar (DISP_VERT);

      i++;
    }

  grub_putchar (DISP_LL);
  for (i = 0; i < 73; i++)
    grub_putchar (DISP_HORIZ);
  grub_putchar (DISP_LR);

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_STANDARD);
}

static void
run_menu (char *menu_entries, char *config_entries, int num_entries,
	  char *heap, int entryno)
{
  int c, time1, time2 = -1, first_entry = 0;
  char *cur_entry = 0;

  /*
   *  Main loop for menu UI.
   */

restart:
  /* Dumb terminal always use all entries for display 
     invariant for TERM_DUMB: first_entry == 0  */
  if (! (current_term->flags & TERM_DUMB))
    {
      while (entryno > 11)
	{
	  first_entry++;
	  entryno--;
	}
    }

  /* If the timeout was expired or wasn't set, force to show the menu
     interface. */
  if (grub_timeout < 0)
    show_menu = 1;
  
  /* If SHOW_MENU is false, don't display the menu until ESC is pressed.  */
  if (! show_menu)
    {
      /* Get current time.  */
      while ((time1 = getrtsecs ()) == 0xFF)
	;

      while (1)
	{
	  /* Check if ESC is pressed.  */
	  if (checkkey () != -1 && ASCII_CHAR (getkey ()) == '\e')
	    {
	      grub_timeout = -1;
	      show_menu = 1;
	      break;
	    }

	  /* If GRUB_TIMEOUT is expired, boot the default entry.  */
	  if (grub_timeout >=0
	      && (time1 = getrtsecs ()) != time2
	      && time1 != 0xFF)
	    {
	      if (grub_timeout <= 0)
		{
		  grub_timeout = -1;
		  goto boot_entry;
		}
	      
	      time2 = time1;
	      grub_timeout--;
	      
	      /* Print a message.  */
	      grub_printf ("\rPress `ESC' to enter the menu... %d   ",
			   grub_timeout);
	    }
	}
    }

  /* Only display the menu if the user wants to see it. */
  if (show_menu)
    {
      init_page ();
      setcursor (0);

      if (current_term->flags & TERM_DUMB)
	print_entries_raw (num_entries, first_entry, menu_entries);
      else
	print_border (3, 12);

      grub_printf ("\n\
      Use the %c and %c keys to select which entry is highlighted.\n",
		   DISP_UP, DISP_DOWN);
      
      if (! auth && password)
	{
	  if (*graphics_file)
	    {
	      printf ("\
	WARNING: graphical menu doesn\'t work\
	in conjunction with the password feature\n" );
	    }
	  printf ("\
      Press enter to boot the selected OS or \'p\' to enter a\n\
      password to unlock the next set of features.");
	}
      else
	{
	  if (config_entries)
	    printf ("\
      Press enter to boot the selected OS, \'e\' to edit the\n\
      commands before booting, or \'c\' for a command-line.");
	  else
	    printf ("\
      Press \'b\' to boot, \'e\' to edit the selected command in the\n\
      boot sequence, \'c\' for a command-line, \'o\' to open a new line\n\
      after (\'O\' for before) the selected line, \'d\' to remove the\n\
      selected line, or escape to go back to the main menu.");
	}

      if (current_term->flags & TERM_DUMB)
	grub_printf ("\n\nThe selected entry is %d ", entryno);
      else
	print_entries (3, 12, first_entry, entryno, menu_entries);
    }

  /* XX using RT clock now, need to initialize value */
  while ((time1 = getrtsecs()) == 0xFF);

  while (1)
    {
      /* Initialize to NULL just in case...  */
      cur_entry = NULL;

      if (grub_timeout >= 0 && (time1 = getrtsecs()) != time2 && time1 != 0xFF)
	{
	  if (grub_timeout <= 0)
	    {
	      grub_timeout = -1;
	      break;
	    }

	  /* else not booting yet! */
	  time2 = time1;

	  if (current_term->flags & TERM_DUMB)
	      grub_printf ("\r    Entry %d will be booted automatically in %d seconds.   ", 
			   entryno, grub_timeout);
	  else
	    {
	      gotoxy (3, 22);
	      grub_printf ("The highlighted entry will be booted automatically in %d seconds.    ",
			   grub_timeout);
	      gotoxy (74, 4 + entryno);
	  }
	  
	  grub_timeout--;
	}

      /* Check for a keypress, however if TIMEOUT has been expired
	 (GRUB_TIMEOUT == -1) relax in GETKEY even if no key has been
	 pressed.  
	 This avoids polling (relevant in the grub-shell and later on
	 in grub if interrupt driven I/O is done).  */
      if (checkkey () >= 0 || grub_timeout < 0)
	{
	  /* Key was pressed, show which entry is selected before GETKEY,
	     since we're comming in here also on GRUB_TIMEOUT == -1 and
	     hang in GETKEY */
	  if (current_term->flags & TERM_DUMB)
	    grub_printf ("\r    Highlighted entry is %d: ", entryno);

	  c = ASCII_CHAR (getkey ());

	  if (grub_timeout >= 0)
	    {
	      if (current_term->flags & TERM_DUMB)
		grub_putchar ('\r');
	      else
		gotoxy (3, 22);
	      printf ("                                                                    ");
	      grub_timeout = -1;
	      fallback_entryno = -1;
	      if (! (current_term->flags & TERM_DUMB))
		gotoxy (74, 4 + entryno);
	    }

	  /* We told them above (at least in SUPPORT_SERIAL) to use
	     '^' or 'v' so accept these keys.  */
	  if (c == 16 || c == '^')
	    {
	      if (current_term->flags & TERM_DUMB)
		{
		  if (entryno > 0)
		    entryno--;
		}
	      else
		{
		  if (entryno > 0)
		    {
		      print_entry (4 + entryno, 0,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		      entryno--;
		      print_entry (4 + entryno, 1,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		    }
		  else if (first_entry > 0)
		    {
		      first_entry--;
		      print_entries (3, 12, first_entry, entryno,
				     menu_entries);
		    }
		}
	    }
	  else if ((c == 14 || c == 'v')
		   && first_entry + entryno + 1 < num_entries)
	    {
	      if (current_term->flags & TERM_DUMB)
		entryno++;
	      else
		{
		  if (entryno < 11)
		    {
		      print_entry (4 + entryno, 0,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		      entryno++;
		      print_entry (4 + entryno, 1,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		  }
		else if (num_entries > 12 + first_entry)
		  {
		    first_entry++;
		    print_entries (3, 12, first_entry, entryno, menu_entries);
		  }
		}
	    }
	  else if (c == 7)
	    {
	      /* Page Up */
	      first_entry -= 12;
	      if (first_entry < 0)
		{
		  entryno += first_entry;
		  first_entry = 0;
		  if (entryno < 0)
		    entryno = 0;
		}
	      print_entries (3, 12, first_entry, entryno, menu_entries);
	    }
	  else if (c == 3)
	    {
	      /* Page Down */
	      first_entry += 12;
	      if (first_entry + entryno + 1 >= num_entries)
		{
		  first_entry = num_entries - 12;
		  if (first_entry < 0)
		    first_entry = 0;
		  entryno = num_entries - first_entry - 1;
		}
	      print_entries (3, 12, first_entry, entryno, menu_entries);
	    }

	  if (config_entries)
	    {
	      if ((c == '\n') || (c == '\r') || (c == 6))
		break;
	    }
	  else
	    {
	      if ((c == 'd') || (c == 'o') || (c == 'O'))
		{
		  if (! (current_term->flags & TERM_DUMB))
		    print_entry (4 + entryno, 0,
				 get_entry (menu_entries,
					    first_entry + entryno,
					    0));

		  /* insert after is almost exactly like insert before */
		  if (c == 'o')
		    {
		      /* But `o' differs from `O', since it may causes
			 the menu screen to scroll up.  */
		      if (entryno < 11 || (current_term->flags & TERM_DUMB))
			entryno++;
		      else
			first_entry++;
		      
		      c = 'O';
		    }

		  cur_entry = get_entry (menu_entries,
					 first_entry + entryno,
					 0);

		  if (c == 'O')
		    {
		      grub_memmove (cur_entry + 2, cur_entry,
				    ((int) heap) - ((int) cur_entry));

		      cur_entry[0] = ' ';
		      cur_entry[1] = 0;

		      heap += 2;

		      num_entries++;
		    }
		  else if (num_entries > 0)
		    {
		      char *ptr = get_entry(menu_entries,
					    first_entry + entryno + 1,
					    0);

		      grub_memmove (cur_entry, ptr,
				    ((int) heap) - ((int) ptr));
		      heap -= (((int) ptr) - ((int) cur_entry));

		      num_entries--;

		      if (entryno >= num_entries)
			entryno--;
		      if (first_entry && num_entries < 12 + first_entry)
			first_entry--;
		    }

		  if (current_term->flags & TERM_DUMB)
		    {
		      grub_printf ("\n\n");
		      print_entries_raw (num_entries, first_entry,
					 menu_entries);
		      grub_printf ("\n");
		    }
		  else
		    print_entries (3, 12, first_entry, entryno, menu_entries);
		}

	      cur_entry = menu_entries;
	      if (c == 27)
		return;
	      if (c == 'b')
		break;
	    }

	  if (! auth && password)
	    {
	      if (c == 'p')
		{
		  /* Do password check here! */
		  char entered[32];
		  char *pptr = password;

		  if (current_term->flags & TERM_DUMB)
		    grub_printf ("\r                                    ");
		  else
		    gotoxy (1, 21);

		  /* Wipe out the previously entered password */
		  grub_memset (entered, 0, sizeof (entered));
		  get_cmdline (" Password: ", entered, 31, '*', 0);

		  while (! isspace (*pptr) && *pptr)
		    pptr++;

		  /* Make sure that PASSWORD is NUL-terminated.  */
		  *pptr++ = 0;

		  if (! check_password (entered, password, password_type))
		    {
		      char *new_file = config_file;
		      while (isspace (*pptr))
			pptr++;

		      /* If *PPTR is NUL, then allow the user to use
			 privileged instructions, otherwise, load
			 another configuration file.  */
		      if (*pptr != 0)
			{
			  while ((*(new_file++) = *(pptr++)) != 0)
			    ;

			  /* Make sure that the user will not have
			     authority in the next configuration.  */
			  auth = 0;
			  return;
			}
		      else
			{
			  /* Now the user is superhuman.  */
			  auth = 1;
			  goto restart;
			}
		    }
		  else
		    {
		      grub_printf ("Failed!\n      Press any key to continue...");
		      getkey ();
		      goto restart;
		    }
		}
	    }
	  else
	    {
	      if (c == 'e')
		{
		  int new_num_entries = 0, i = 0;
		  char *new_heap;

		  if (config_entries)
		    {
		      new_heap = heap;
		      cur_entry = get_entry (config_entries,
					     first_entry + entryno,
					     1);
		    }
		  else
		    {
		      /* safe area! */
		      new_heap = heap + NEW_HEAPSIZE + 1;
		      cur_entry = get_entry (menu_entries,
					     first_entry + entryno,
					     0);
		    }

		  do
		    {
		      while ((*(new_heap++) = cur_entry[i++]) != 0);
		      new_num_entries++;
		    }
		  while (config_entries && cur_entry[i]);

		  /* this only needs to be done if config_entries is non-NULL,
		     but it doesn't hurt to do it always */
		  *(new_heap++) = 0;

		  if (config_entries)
		    run_menu (heap, NULL, new_num_entries, new_heap, 0);
		  else
		    {
		      cls ();
		      print_cmdline_message (0);

		      new_heap = heap + NEW_HEAPSIZE + 1;

		      saved_drive = boot_drive;
		      saved_partition = install_partition;
		      current_drive = GRUB_INVALID_DRIVE;

		      if (! get_cmdline (PACKAGE " edit> ", new_heap,
					 NEW_HEAPSIZE + 1, 0, 1))
			{
			  int j = 0;

			  /* get length of new command */
			  while (new_heap[j++])
			    ;

			  if (j < 2)
			    {
			      j = 2;
			      new_heap[0] = ' ';
			      new_heap[1] = 0;
			    }

			  /* align rest of commands properly */
			  grub_memmove (cur_entry + j, cur_entry + i,
					(int) heap - ((int) cur_entry + i));

			  /* copy command to correct area */
			  grub_memmove (cur_entry, new_heap, j);

			  heap += (j - i);
			}
		    }

		  goto restart;
		}
	      if (c == 'c')
		{
		  enter_cmdline (heap, 0);
		  goto restart;
		}
#ifdef GRUB_UTIL
	      if (c == 'q')
		{
		  /* The same as ``quit''.  */
		  stop ();
		}
#endif
	    }
	}
    }
  
  /* Attempt to boot an entry.  */
  
 boot_entry:
  
  cls ();
  setcursor (1);
  
  while (1)
    {
      if (config_entries)
	printf ("  Booting \'%s\'\n\n",
		get_entry (menu_entries, first_entry + entryno, 0));
      else
	printf ("  Booting command-list\n\n");

      if (! cur_entry)
	cur_entry = get_entry (config_entries, first_entry + entryno, 1);

      /* Set CURRENT_ENTRYNO for the command "savedefault".  */
      current_entryno = first_entry + entryno;
      
      if (run_script (cur_entry, heap))
	{
	  if (fallback_entryno >= 0)
	    {
	      cur_entry = NULL;
	      first_entry = 0;
	      entryno = fallback_entries[fallback_entryno];
	      fallback_entryno++;
	      if (fallback_entryno >= MAX_FALLBACK_ENTRIES
		  || fallback_entries[fallback_entryno] < 0)
		fallback_entryno = -1;
	    }
	  else
	    break;
	}
      else
	break;
    }

  show_menu = 1;
  goto restart;
}



#if 0
/* for debugging */
static void hexdump(unsigned char *buf, unsigned len)
{
  int i, j = 0;
  char s[17];
  unsigned addr = (unsigned) buf;

  s[16] = 0;
  while(len--) {
    i = buf[j];
    i = i & 0xff;
    s[j & 15] = (i >= 0x20 && i <= 0x7e) ? i : '.';
    if(!(j & 15)) {
      printf("%x  ", j + addr);
    }
    if(!(j & 7) && (j & 15)) printf(" ");
    /* stupid grub_printf */
    printf("%x", (i >> 4) & 0x0f);
    printf("%x ", i & 0x0f);
    if(!(++j & 15)) {
      printf(" %s\n", s);
    }
  }

  if(j & 15) {
    s[j & 15] = 0;
    if(!(j & 8)) printf(" ");
    i = 1 + 3 * (16 - (j & 15));
    while(i--) printf(" ");
    printf("%s\n", s);
  }
}
#endif


/* kernel + (grub-)module options */
#define GFX_CMD_BUF_SIZE 512

/* command line separator char */
#define GFX_CMD_SEP 1

/*
 * Go through config entry and find kernel args, if any.
 * Put things into buf and return it.
 */
static char *get_kernel_args(char *cfg, char *buf)
{
  int i, j;
  char *s, *t = "", *p, *t2;

  *(p = buf) = 0;

  for(j = 0; ; j++) {
    s = get_entry(cfg, j, 0);
    if(!*s) break;
    if(
      (!memcmp(s, "kernel", 6) || !memcmp(s, "module", 6)) &&
      (s[6] == ' ' || s[6] == '\t')
    ) {
      t = skip_to(0, s);
      t2 = s[0] == 'm' ? strstr(t, "initrd") : NULL;
      if(*t) t = skip_to(0, t);
      if(t2 && t2 < t) break;	/* module is likely a normal initrd -> skip */
      i = strlen(t);
      if(p - buf + i > GFX_CMD_BUF_SIZE - 2) break;
      *p++ = GFX_CMD_SEP;
      strcpy(p, t);
      p += i;

      continue;
    }
  }

  if(*buf) buf++;	/* skip initial separator char */

  return buf;
}


/*
 * Check header and return code start offset.
 */
static unsigned magic_ok(unsigned char *buf)
{
  if(
    *(unsigned *) buf == 0x0b2d97f00 &&		/* magic id */
    (buf[4] == 8)				/* version 8 */
  ) {
    return *(unsigned *) (buf + 8);
  }

  return 0;
}


/*
 * Search cpio archive for gfx file.
 */
static unsigned find_file(unsigned char *buf, unsigned len, unsigned *gfx_file_start, unsigned *file_len)
{
  unsigned i, fname_len, code_start = 0;

  *gfx_file_start = 0;

  for(i = 0; i < len;) {
    if((len - i) >= 0x1a && (buf[i] + (buf[i + 1] << 8)) == 0x71c7) {
      fname_len = *(unsigned short *) (buf + i + 20);
      *file_len = *(unsigned short *) (buf + i + 24) + (*(unsigned short *) (buf + i + 22) << 16);
      i += 26 + fname_len;
      i = ((i + 1) & ~1);
      if((code_start = magic_ok(buf + i))) {
        *gfx_file_start = i;
        return code_start;
      }
      i += *file_len;
      i = ((i + 1) & ~1);
    }
    else {
      break;
    }
  }

  return code_start;
}

static inline unsigned char * stack_ptr(void)
{
  unsigned char * u;

  asm("movl %%esp, %0" : "=r" (u));

  return u;
}

static void sleep(int delay)
{
  int tick, last_tick = currticks();

  delay *= 18;

  while(delay--) {
    while((tick = currticks()) == last_tick) { }
    last_tick = tick;
  }
}  

static void wait_for_key()
{
  printf("Press a key to continue...");
  getkey();
  printf("\r                          \r");
}


/*
 * Leave that much space on the heap. Everything else goes to the graphics
 * functions.
 *
 * 0x2000 is _not_ enough
 */
#define MIN_HEAP_SIZE	0x4000
#define MIN_GFX_FREE	0x1000

#define SC_BOOTLOADER		0
#define SC_FAILSAFE		3
#define SC_SYSCONFIG_SIZE	4
#define SC_BOOTLOADER_SEG	8
#define SC_XMEM_0		24
#define SC_XMEM_1		26
#define SC_XMEM_2		28
#define SC_XMEM_3		30
#define SC_FILE			32
#define SC_ARCHIVE_START	36
#define SC_ARCHIVE_END		40
#define SC_MEM0_START		44
#define SC_MEM0_END		48

/*
 * Does normally not return.
 */
static void
run_graphics_menu (char *menu_entries, char *config_entries, int num_entries,
	  char *heap, int entryno)
{
  unsigned char *buf, *buf_ext;
  unsigned buf_size, buf_ext_size, code_start, file_start;
  char *s, *t, *t2, *cfg, *new_config, *p;
  char *saved_heap;
  int i, j, max_len, gfx_file_size, verbose;
  int selected_entry;
  gfx_data_t *gfx_data;
  char *cmd_buf;
  unsigned mem0_start, mem0_end, file_len;

  /*
   * check gfx_data_t struct offsets for consistency; gcc will optimize away
   * the whole block
   */

  /* dummy function to make ld fail */
  {
    extern void wrong_struct_size(void);
    #define gfx_ofs_check(a) if(gfx_ofs_##a != (char *) &gfx_data->a - (char *) gfx_data) wrong_struct_size();
    gfx_ofs_check(ok);
    gfx_ofs_check(code_seg);
    gfx_ofs_check(jmp_table);
    gfx_ofs_check(sys_cfg);
    gfx_ofs_check(cmdline);
    gfx_ofs_check(cmdline_len);
    gfx_ofs_check(menu_list);
    gfx_ofs_check(menu_default_entry);
    gfx_ofs_check(menu_entries);
    gfx_ofs_check(menu_entry_len);
    gfx_ofs_check(args_list);
    gfx_ofs_check(args_entry_len);
    gfx_ofs_check(timeout);
    #undef gfx_ofs_check
  }

  if(!num_entries) return;

  graphics_data = gfx_data = (gfx_data_t *) heap;
  heap += sizeof *gfx_data;
  memset(gfx_data, 0, sizeof *gfx_data);

  gfx_data->sys_cfg[SC_BOOTLOADER] = 2;			/* bootloader: grub */
  gfx_data->sys_cfg[SC_SYSCONFIG_SIZE] = 52;		/* config data size */
  *(unsigned short *) (gfx_data->sys_cfg + SC_BOOTLOADER_SEG) = (unsigned) gfx_data >> 4;	/* segment */
  gfx_data->sys_cfg[SC_XMEM_0] = 0x21;			/* 1MB @ 2MB */
  gfx_data->sys_cfg[SC_XMEM_1] = 0x41;			/* 1MB @ 4MB */
  verbose = (*(unsigned char *) 0x417) & 3 ? 1 : 0;	/* SHIFT pressed */
  gfx_data->sys_cfg[SC_FAILSAFE] = verbose;

  gfx_data->timeout = grub_timeout >= 0 ? grub_timeout : 0;


  /* setup command line edit buffer */

  gfx_data->cmdline_len = 256;

  gfx_data->cmdline = heap;
  heap += gfx_data->cmdline_len;
  memset(gfx_data->cmdline, 0, gfx_data->cmdline_len);

  cmd_buf = heap;
  heap += GFX_CMD_BUF_SIZE;

  /* setup menu entries */

  for(i = max_len = 0; i < num_entries; i++) {
    j = strlen(get_entry(menu_entries, i, 0));
    if(j > max_len) max_len = j;
  }

  if(!max_len) return;

  gfx_data->menu_entry_len = max_len + 1;
  gfx_data->menu_entries = num_entries;

  gfx_data->menu_list = heap;
  heap += gfx_data->menu_entry_len * gfx_data->menu_entries;

  memset(gfx_data->menu_list, 0, gfx_data->menu_entry_len * gfx_data->menu_entries);

  for(i = 0; i < (int) gfx_data->menu_entries; i++) {
    strcpy(gfx_data->menu_list + i * gfx_data->menu_entry_len, get_entry(menu_entries, i, 0));
  }

  gfx_data->menu_default_entry = gfx_data->menu_list + entryno * gfx_data->menu_entry_len;


  /* setup list of kernel args */

  for(i = max_len = 0; i < num_entries; i++) {
    s = get_kernel_args(get_entry(config_entries, i, 1), cmd_buf);
    j = strlen(s);
    if(j > max_len) max_len = j;
  }

  gfx_data->args_entry_len = max_len + 1;

  gfx_data->args_list = heap;
  heap += gfx_data->args_entry_len * gfx_data->menu_entries;

  memset(gfx_data->args_list, 0, gfx_data->args_entry_len * gfx_data->menu_entries);

  for(i = 0; i < (int) gfx_data->menu_entries; i++) {
    strcpy(gfx_data->args_list + i* gfx_data->args_entry_len, get_kernel_args(get_entry(config_entries, i, 1), cmd_buf));
  }


  /* go back here when we no longer need the graphics data */
  saved_heap = heap;


  /* get memory area to be used by graphics functions */

  /* use 1MB starting at 2MB as file buffer */
  buf_ext = (unsigned char *) (2 << 20);
  buf_ext_size = 1 << 20;

  /* must be 16-byte aligned */
  buf = (unsigned char *) (((unsigned) heap + 0xf) & ~0xf);

  buf_size = stack_ptr() - buf - MIN_HEAP_SIZE;
  buf_size &= ~0xf;

  mem0_start = (unsigned) buf;
  mem0_end = mem0_start + buf_size;

  if(verbose) {
    printf("low memory 0x%x - 0x%x (%d bytes)\n", mem0_start, mem0_end, buf_size);
    wait_for_key();
  }

  heap += buf_size;

  /* read the file */

  if(!grub_open(graphics_file)) {
    printf("%s: file not found\n", graphics_file);
    sleep(5);
    heap = saved_heap;
    return;
  }

  gfx_file_size = grub_read(buf_ext, buf_ext_size);

  grub_close();

  if(gfx_file_size <= 0) {
    printf("%s: read error\n", graphics_file);
    sleep(5);
    heap = saved_heap;
    return;
  }

  if(verbose) {
    printf("%s: %d bytes (%d bytes left)\n", graphics_file, gfx_file_size, buf_ext_size - gfx_file_size);
    wait_for_key();
  }

  /* locate file inside cpio archive */
  if(!(code_start = find_file(buf_ext, gfx_file_size, &file_start, &file_len))) {
    printf("%s: invalid file format\n", graphics_file);
    sleep(5);
    heap = saved_heap;
    return;
  }

  if(verbose) {
    printf("init: start 0x%x, len %d; code offset 0x%x\n", file_start, file_len, code_start);
    wait_for_key();
  }

  if(file_len - code_start + MIN_GFX_FREE > buf_size) {
    printf("not enough free memory: %d extra bytes need\n", file_len - code_start + MIN_GFX_FREE - buf_size);
    sleep(5);
    heap = saved_heap;
    return;
  }

  memcpy((void *) buf, (void *) (buf_ext + file_start + code_start), file_len - code_start);

  mem0_start += file_len - code_start;
  mem0_start = (mem0_start + 3) & ~3;		/* align */

  /* init interface to graphics functions */

  *(unsigned *) (gfx_data->sys_cfg + SC_FILE) = (unsigned) buf_ext + file_start;
  *(unsigned *) (gfx_data->sys_cfg + SC_ARCHIVE_START) = (unsigned) buf_ext;
  *(unsigned *) (gfx_data->sys_cfg + SC_ARCHIVE_END) = (unsigned) buf_ext + gfx_file_size;
  *(unsigned *) (gfx_data->sys_cfg + SC_MEM0_START) = mem0_start;
  *(unsigned *) (gfx_data->sys_cfg + SC_MEM0_END) = mem0_end;

  gfx_data->code_seg = (unsigned) buf >> 4;

  if(verbose) {
    printf("init 0x%x, archive 0x%x - 0x%x, low mem 0x%x - 0x%x\ncode seg 0x%x\n",
      (unsigned) buf_ext + file_start,
      (unsigned) buf_ext, (unsigned) buf_ext + gfx_file_size,
      mem0_start, mem0_end, gfx_data->code_seg
    );
    wait_for_key();
  }

  for(i = 0; (unsigned) i < sizeof gfx_data->jmp_table / sizeof *gfx_data->jmp_table; i++) {
    gfx_data->jmp_table[i] = (gfx_data->code_seg << 16) + ((unsigned short *) buf)[i];
  }

  if(verbose) {
    for(i = 0; i < 12; i++) {
      printf("%d: 0x%x\n", i, gfx_data->jmp_table[i]);
    }

    for(i = 0; i < gfx_data->menu_entries; i++) {
      printf("\"%s\"  --  \"%s\"\n",
        gfx_data->menu_list + i * gfx_data->menu_entry_len,
        gfx_data->args_list + i * gfx_data->args_entry_len
      );
    }

    printf("default: \"%s\"\n", gfx_data->menu_default_entry);
    wait_for_key();
  }

  /* switch to graphics mode */

  if(gfx_init(gfx_data)) {
    printf("graphics initialization failed\n");
    sleep(5);
    heap = saved_heap;
    return;
  }

  gfx_setup_menu(gfx_data);

  i = gfx_input(gfx_data, &selected_entry);

  /* ESC -> show text menu */
  if(i == 1) {
    gfx_done(gfx_data);
    grub_timeout = -1;

    heap = saved_heap;
    return;
  }

  gfx_done(gfx_data);

  heap = saved_heap;	/* free most of the graphics data */

  // printf("cmdline: >%s<, entry = %d\n", gfx_data->cmdline, selected_entry);

  if(selected_entry < 0 || selected_entry > num_entries) return;

  /* for 'savedefault' */
  current_entryno = selected_entry;


  /* create new config with modified kernel option */

  cfg = get_entry(config_entries, selected_entry, 1);

  new_config = heap;

  for(p = gfx_data->cmdline, i = 0; ; i++) {
    s = get_entry(cfg, i, 0);
    if(!*s) {
      if(!i) *heap++ = 0;
      *heap++ = 0;
      break;
    }
    /* note: must match get_kernel_args() */
    if(
      (!memcmp(s, "kernel", 6) || !memcmp(s, "module", 6)) &&
      (s[6] == ' ' || s[6] == '\t')
    ) {
      t = skip_to(0, s);
      t2 = s[0] == 'm' ? strstr(t, "initrd") : NULL;
      if(*t) t = skip_to(0, t);
      if(t2 && t2 < t) {	/* module is likely a normal initrd -> skip */
        strcpy(heap, s);
        heap += strlen(s) + 1;
        continue;
      }
      memmove(heap, s, t - s);
      heap += t - s;
      *heap++ = ' ';
      while(*p && *p != GFX_CMD_SEP) *heap++ = *p++;
      *heap++ = 0;
      if(*p == GFX_CMD_SEP) p++;
    }
    else {
      strcpy(heap, s);
      heap += strlen(s) + 1;
    }
  }

  *heap++ = 0;

  // hexdump(new_config, heap - new_config);
  // getkey();

  run_script(new_config, heap);
}


static int
get_line_from_config (char *cmdline, int maxlen, int read_from_file)
{
  int pos = 0, literal = 0, comment = 0;
  char c;  /* since we're loading it a byte at a time! */
  
  while (1)
    {
      if (read_from_file)
	{
	  if (! grub_read (&c, 1))
	    break;
	}
      else
	{
	  if (! read_from_preset_menu (&c, 1))
	    break;
	}

      /* Skip all carriage returns.  */
      if (c == '\r')
	continue;

      /* Replace tabs with spaces.  */
      if (c == '\t')
	c = ' ';

      /* The previous is a backslash, then...  */
      if (literal)
	{
	  /* If it is a newline, replace it with a space and continue.  */
	  if (c == '\n')
	    {
	      c = ' ';
	      
	      /* Go back to overwrite a backslash.  */
	      if (pos > 0)
		pos--;
	    }
	    
	  literal = 0;
	}
	  
      /* translate characters first! */
      if (c == '\\' && ! literal)
	literal = 1;

      if (comment)
	{
	  if (c == '\n')
	    comment = 0;
	}
      else if (! pos)
	{
	  if (c == '#')
	    comment = 1;
	  else if ((c != ' ') && (c != '\n'))
	    cmdline[pos++] = c;
	}
      else
	{
	  if (c == '\n')
	    break;

	  if (pos < maxlen)
	    cmdline[pos++] = c;
	}
    }

  cmdline[pos] = 0;

  return pos;
}

extern void __savedefault_once_reset();

/* This is the starting function in C.  */
void
cmain (void)
{
  int config_len, menu_len, num_entries;
  char *config_entries, *menu_entries;
  char *kill_buf = (char *) KILL_BUF;

  auto void reset (void);
  void reset (void)
    {
      count_lines = -1;
      config_len = 0;
      menu_len = 0;
      num_entries = 0;
      config_entries = (char *) mbi.drives_addr + mbi.drives_length;
      menu_entries = (char *) MENU_BUF;
      init_config ();
    }
  
  /* Initialize the environment for restarting Stage 2.  */
  grub_setjmp (restart_env);
  
  /* Initialize the kill buffer.  */
  *kill_buf = 0;

  /* Never return.  */
  for (;;)
    {
      int is_opened, is_preset;

      reset ();
      
      /* Here load the configuration file.  */
      
#ifdef GRUB_UTIL
      if (use_config_file)
#endif /* GRUB_UTIL */
	{
	  char *default_file = (char *) DEFAULT_FILE_BUF;
	  int i;
	  
	  /* Get a saved default entry if possible.  */
	  saved_entryno = 0;
	  *default_file = 0;
	  grub_strncat (default_file, config_file, DEFAULT_FILE_BUFLEN);
	  for (i = grub_strlen(default_file); i >= 0; i--)
	    if (default_file[i] == '/')
	      {
		i++;
		break;
	      }
	  default_file[i] = 0;
	  grub_strncat (default_file + i, "default", DEFAULT_FILE_BUFLEN - i);
	  if (grub_open (default_file))
	    {
	      char buf[10]; /* This is good enough.  */
	      char *p = buf;
	      int len;
	      
	      len = grub_read (buf, sizeof (buf));
	      if (len > 0)
		{
		  buf[sizeof (buf) - 1] = 0;
		  safe_parse_maxint (&p, &saved_entryno);
		}

	      grub_close ();
	    }
	  errnum = ERR_NONE;
	  
	  do
	    {
	      /* STATE 0:  Before any title command.
		 STATE 1:  In a title command.
		 STATE >1: In a entry after a title command.  */
	      int state = 0, prev_config_len = 0, prev_menu_len = 0;
	      char *cmdline;

	      /* Try the preset menu first. This will succeed at most once,
		 because close_preset_menu disables the preset menu.  */
	      is_opened = is_preset = open_preset_menu ();
	      if (! is_opened)
		{
		  is_opened = grub_open (config_file);
		  errnum = ERR_NONE;
		}

	      if (! is_opened)
		break;

	      /* This is necessary, because the menu must be overrided.  */
	      reset ();
	      
	      cmdline = (char *) CMDLINE_BUF;
	      while (get_line_from_config (cmdline, NEW_HEAPSIZE,
					   ! is_preset))
		{
		  struct builtin *builtin;
		  
		  /* Get the pointer to the builtin structure.  */
		  builtin = find_command (cmdline);
		  errnum = 0;
		  if (! builtin)
		    /* Unknown command. Just skip now.  */
		    continue;
		  
		  if (builtin->flags & BUILTIN_TITLE)
		    {
		      char *ptr;
		      
		      /* the command "title" is specially treated.  */
		      if (state > 1)
			{
			  /* The next title is found.  */
			  num_entries++;
			  config_entries[config_len++] = 0;
			  prev_menu_len = menu_len;
			  prev_config_len = config_len;
			}
		      else
			{
			  /* The first title is found.  */
			  menu_len = prev_menu_len;
			  config_len = prev_config_len;
			}
		      
		      /* Reset the state.  */
		      state = 1;
		      
		      /* Copy title into menu area.  */
		      ptr = skip_to (1, cmdline);
		      while ((menu_entries[menu_len++] = *(ptr++)) != 0)
			;
		    }
		  else if (! state)
		    {
		      /* Run a command found is possible.  */
		      if (builtin->flags & BUILTIN_MENU)
			{
			  char *arg = skip_to (1, cmdline);
			  (builtin->func) (arg, BUILTIN_MENU);
			  errnum = 0;
			}
		      else
			/* Ignored.  */
			continue;
		    }
		  else
		    {
		      char *ptr = cmdline;
		      
		      state++;
		      /* Copy config file data to config area.  */
		      while ((config_entries[config_len++] = *ptr++) != 0)
			;
		    }
		}
	      
	      if (state > 1)
		{
		  /* Finish the last entry.  */
		  num_entries++;
		  config_entries[config_len++] = 0;
		}
	      else
		{
		  menu_len = prev_menu_len;
		  config_len = prev_config_len;
		}
	      
	      menu_entries[menu_len++] = 0;
	      config_entries[config_len++] = 0;
	      grub_memmove (config_entries + config_len, menu_entries,
			    menu_len);
	      menu_entries = config_entries + config_len;

	      /* Make sure that all fallback entries are valid.  */
	      if (fallback_entryno >= 0)
		{
		  for (i = 0; i < MAX_FALLBACK_ENTRIES; i++)
		    {
		      if (fallback_entries[i] < 0)
			break;
		      if (fallback_entries[i] >= num_entries)
			{
			  grub_memmove (fallback_entries + i,
					fallback_entries + i + 1,
					((MAX_FALLBACK_ENTRIES - i - 1)
					 * sizeof (int)));
			  i--;
			}
		    }

		  if (fallback_entries[0] < 0)
		    fallback_entryno = -1;
		}
	      /* Check if the default entry is present. Otherwise reset
		 it to fallback if fallback is valid, or to DEFAULT_ENTRY 
		 if not.  */
	      if (default_entry >= num_entries)
		{
		  if (fallback_entryno >= 0)
		    {
		      default_entry = fallback_entries[0];
		      fallback_entryno++;
		      if (fallback_entryno >= MAX_FALLBACK_ENTRIES
			  || fallback_entries[fallback_entryno] < 0)
			fallback_entryno = -1;
		    }
		  else
		    default_entry = 0;
		}
	      
	      if (is_preset)
		close_preset_menu ();
	      else
		grub_close ();
	    }
	  while (is_preset);
	}
#ifndef SUPPORT_DISKLESS
      __savedefault_once_reset();
#endif
      if (! num_entries)
	{
	  /* If no acceptable config file, goto command-line, starting
	     heap from where the config entries would have been stored
	     if there were any.  */
	  enter_cmdline (config_entries, 1);
	}
      else
	{
	  if (*graphics_file && !password && show_menu && grub_timeout)
	    {
	      run_graphics_menu(menu_entries, config_entries, num_entries,menu_entries + menu_len, default_entry);
	    }
	    /* Run menu interface.  */
            run_menu (menu_entries, config_entries, num_entries, menu_entries + menu_len, default_entry);
	}
    }
}
