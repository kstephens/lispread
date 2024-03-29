/*
** lispread.c - a generic lisp reader.
** Copyright 1998, 1999, 2011, 2012  Kurt A. Stephens  http://kurtstephens.com/
*/
/*
This lisp reader is very minimal.
It does not implement the full Common Lisp syntax.
In general, it is compliant with a subset of the Revised 5 Scheme Report,
with a few common extensions.

The ';', '(', ')', '#' and whitespace characters are token terminators.
Tokens that are not numbers are assumed to be symbols.
This reader does not decaseify symbols before calling STRING_2_SYMBOL.
';' comments are treated as whitespace.
The "#!...\n" comment allows lisp files to execute systems that support '#!PROG' scripts.

The following synactic structures can be read:

Comments      ;...\n, #!...\n, #;DATUM
Quote         'x
Lists         (a b ...)
              [a b ...]    (Optional)
Conses        (a . d)
              [a . d]      (Optional)
Vectors       #(a b ...)
              #[a b ...)   (Optional)
Characters    #\C, #\space, #\newline
False         #f, #F
True          #t, #T
Unspecified   #u, #U       (Optional)
Logical EOF   ##           (Optional)
Numbers       #b0101001, #o1726m #d2349, #x0123456789abcedf, 1234, 1234.00, etc.
Strings       "...", "\"\\"
Symbols       asdf, +, etc.

To use lispread.c you must defined the following macros and #include "lispread.c"
to "glue" it to your code.

Macros declared "Opt." are optional.

Macro               Implementation
==========================================================================
VALUE               The C type for a lisp value.
READ_DECL           A C function definition for the lisp read function.
                    Within the body of READ_DECL, the "stream" variable must 
	            be bound to a VALUE of the input stream.
READ_DECL_END       Terminate the read C function definition.  Opt.
READ_CALL()         Call the lisp read function recursively.
RETURN(X)           Return a VALUE from the READ_DECL function.  Opt.

MALLOC(s)           Allocate a character memory buffer from lisp.
REALLOC(p,s)        Reallocate a previously MALLOCed buffer from lisp.
FREE(p)             Free a previouly MALLOCed buffer from lisp.

PEEKC(stream)       Peek a C char or EOF from the stream.  Opt.  See UNGETC().
UNGETC(stream,c)    Used to implement PEEKC() if PEEKC is #undef.  Opt.
GETC(stream)        Read a C char or EOF from the stream.

EOS                 The end-of-stream VALUE.
CONS(X,Y)           Return a new lisp CONS object.
CAR(CONS)           Get the car field of a pair VALUE as in: (car CONS)
SET_CDR(CONS,V)     Set the cdr field of a pair VALUE as in: (set-cdr! CONS V)
SET(LOC,V)          Set a local variable as in (set! VARIABLE V).  Opt.  

MAKE_CHAR(I)        Create a lisp CHARACTER VALUE from a C integer.

LIST_2_VECTOR(X)    Convert list VALUE X into a VECTOR VALUE.
BRACKET_LISTS       If defined, support [...] bracketed list syntax.

STRING(char*,int)   Create a new lisp STRING VALUE from a MALLOCed buffer.
ESCAPE_STRING(X)    Return a new STRING VALUE with escaped characters (\\, \") replaced.  Opt.
STRING_2_NUMBER(X)  Convert string VALUE X into a NUMBER VALUE, or return F.
STRING_2_SYMBOL(X)  Convert string VALUE X into a SYMBOL VALUE.

SYMBOL(NAME)        Return a symbol VALUE for NAME with '_' replaced with '-'.
SYMBOL_DOT          The "." symbol.

CALL_MACRO_CHAR(X)  Call the macro character function for the C char X.  
                    If the function returns F, continue scanning, 
                    otherwise return the CAR of the result.  Opt.

EQ(X,Y)             Return non-zero C value if (eq? X Y).

NIL                 The empty list VALUE.
T                   The true VALUE for #t.  Opt.
F                   The false VALUE for #f.  Opt.
U                   The unspecified VALUE for #u.  Opt.

ERROR(format,...)   Raise an error using the printf() format.

*/

#ifdef READ_DECL

#include <ctype.h> /* isspace() */

#ifndef SET
#define SET(X,V) ((X) = (V))
#endif

#ifndef PEEKC
#define PEEKC(stream) \
  ({ int _pc = GETC(stream); if ( _pc != EOF ) UNGETC(stream, _pc); _pc; })
#endif

#ifndef READ_DEBUG
#ifdef READ_DEBUG_WHITESPACE
#define READ_DEBUG 1
#endif
#endif

#ifndef READ_DEBUG
#define READ_DEBUG 0
#endif

#ifndef READ_STATE
#define READ_STATE
#endif

#ifndef F
#define F NIL
#endif

static
int eat_whitespace_peekchar(VALUE stream)
{ READ_STATE
  int c;

 more_whitespace:
  while ( (c = PEEKC(stream)) != EOF && isspace(c) ) {
    if ( READ_DEBUG > 1 ) {
      fprintf(stderr, "  read: eat_whitespace_peekchar(): whitespace '%c'\n", (int) c);
      fflush(stderr);
    }
    GETC(stream);
  }
  if ( c == ';' ) {
    if ( READ_DEBUG > 0 ) {
      fprintf(stderr, "  read: eat_whitespace_peekchar(): comment start '%c'\n", (int) c);
      fflush(stderr);
    }
    while ( (c = PEEKC(stream)) != EOF && c != '\n' ) {
      if ( READ_DEBUG > 1 ) {
	fprintf(stderr, "  read: eat_whitespace_peekchar(): comment in '%c'\n", (int) c);
	fflush(stderr);
      }
      GETC(stream);
    }
    goto more_whitespace;
  }

  if ( READ_DEBUG > 0 ) {
    fprintf(stderr, "  read: eat_whitespace_peekchar(): done '%c'\n", (int) c);
    fflush(stderr);
  }

  return(c);
}

#ifndef RETURN
#define RETURN(X) return (X)
#endif

#ifndef ESCAPE_STRING
#define ESCAPE_STRING(X) X
#endif

#ifndef MALLOC
#define MALLOC(S) malloc(S)
#endif

#ifndef REALLOC
#define REALLOC(P,S) realloc(P,S)
#endif

#ifndef FREE
#define FREE(P) free(P)
#endif

static int macro_terminating_charQ(int c)
{
  return c == EOF || c == ';' || c == '(' || c == ')'
#ifdef BRACKET_LISTS
    || c == '[' || c == ']'
#endif
    || c == '#' || isspace(c);
}

READ_DECL
{ READ_STATE
  int c;
  int radix, skip_radix_char;

 try_again:
  radix = 10; skip_radix_char = 0;
#ifdef READ_PROLOGUE
  READ_PROLOGUE;
#endif
  c = eat_whitespace_peekchar(stream);
  if ( c == EOF )
    RETURN(EOS);
  GETC(stream);
  switch ( c ) {
    case '\'':
      RETURN(CONS(SYMBOL(quote), CONS(READ_CALL(), NIL)));

    case '`':
      RETURN(CONS(SYMBOL(quasiquote), CONS(READ_CALL(), NIL)));

    case ',':
      if ( PEEKC(stream) == '@' ) {
	GETC(stream);
	RETURN(CONS(SYMBOL(unquote_splicing), CONS(READ_CALL(), NIL)));
      } else {
	RETURN(CONS(SYMBOL(unquote), CONS(READ_CALL(), NIL)));
      }
      break;

#ifdef BRACKET_LISTS
    case '[': c = ']'; goto list;
#endif
    case '(': c = ')';
#ifdef BRACKET_LISTS
                            list:
#endif
      {
      int terminator = c;
      VALUE l = NIL, lc = NIL;
      while ( 1 ) {
        VALUE x;
        c = eat_whitespace_peekchar(stream);
        if ( c == EOF ) { RETURN(ERROR("eos in list")); }
        if ( c == terminator ) {
	  GETC(stream);
          break;
        }
        
        SET(x, READ_CALL());
        
        if ( EQ(x, SYMBOL_DOT) ) {
          if ( EQ(lc, NIL) ) {
            RETURN(ERROR("expected something before '.' in list"));
          }

          SET_CDR(lc, READ_CALL());

          c = eat_whitespace_peekchar(stream);
          if ( c == EOF ) { RETURN(ERROR("eos in '.' list after cdr")); }
          GETC(stream);
          if ( c != terminator ) {
            RETURN(ERROR("expected '%c': found '%c'", terminator, c));
          }
          break;
        } else {
          VALUE y = CONS(x, NIL);
          if ( EQ(lc, NIL) ) {
            SET(l, y);
          } else {
            SET_CDR(lc, y);
          }
          SET(lc, y);
        }
      }
      RETURN(l);
      }

    case '#':
  hash_again:
      c = PEEKC(stream);
      switch ( c ) {
      case EOF:
	RETURN(ERROR("eos after '#'"));

	/* #! sh-bang comment till EOL. */
      case '!':
#if READ_DEBUG_WHITESPACE
	fprintf(stderr, "  read: #!\n");
	fflush(stderr);
#endif
	GETC(stream);
	while ( (c = PEEKC(stream)) != EOF && c != '\n' ) {
	  GETC(stream);
	}
    	goto try_again;

	/* #| nesting comment. |# */
      case '|':
	{
	  int level = 1;
	  GETC(stream);
	  while ( level > 0 && (c = GETC(stream)) != EOF ) {
	    if ( c == '|' && PEEKC(stream) == '#' ) {
	      GETC(stream);
	      -- level;
	    } else if ( c == '#' && PEEKC(stream) == '|' ) {
	      GETC(stream);
	      ++ level;
	    }
	  }
	  if ( level > 0 )
	    RETURN(ERROR("eos inside #| comment |#"));
	}
	goto try_again;

	/* s-expr comment ala chez scheme */
      case ';':
#if READ_DEBUG_WHITESPACE
	fprintf(stderr, "  read: #;\n");
	fflush(stderr);
#endif
	GETC(stream);
	READ_CALL();
	goto try_again;

      case '(':
	RETURN(LIST_2_VECTOR(READ_CALL()));
        
      case '\\': {
        char *buf; size_t len = 1;
	GETC(stream);
        if ( (c = GETC(stream)) == EOF )
	  RETURN(ERROR("eos after '#\\'"));
        buf = MALLOC(len + 1); buf[0] = c;
        if ( isalpha(c) )
          while ( isalpha(c = PEEKC(stream)) && ! macro_terminating_charQ(c) ) {
            GETC(stream);
            buf = REALLOC(buf, len + 2);
            buf[len ++] = c;
          }
        buf[len] = '\0';
        if ( strcasecmp(buf, "space") == 0 ) c = ' ';
        else if ( strcasecmp(buf, "newline") == 0 ) c = '\n';
        else if ( len > 1 ) RETURN(ERROR("unknown char name '#\\%s'", buf));
        else c = buf[0];
        FREE(buf);
	RETURN(MAKE_CHAR(c));
      }

      case 'f': case 'F':
	GETC(stream);
	RETURN(F);

#ifdef T
      case 't': case 'T':
	GETC(stream);
	RETURN(T);
#endif
        
#ifdef U
      case 'u': case 'U':
	GETC(stream);
	RETURN(U);
#endif

#ifdef E
      case '#':
	GETC(stream);
	RETURN(E);
#endif

      case 'e': case 'E':
      case 'i': case 'I':
        GETC(stream);
	goto hash_again;

      case 'b': case 'B':
	skip_radix_char = 1; radix = 2;
	GETC(stream);
	goto read_radix_number;
	
      case 'o': case 'O':
	skip_radix_char = 1; radix = 8;
	GETC(stream);
	goto read_radix_number;

      case 'd': case 'D':
	skip_radix_char = 1; radix = 10;
	GETC(stream);
	goto read_radix_number;
	
      case 'x': case 'X':
	skip_radix_char = 1; radix = 16;
	GETC(stream);

      read_radix_number:
        // c = GETC(stream);
        goto read_number;

      default:
#ifdef CALL_MACRO_CHAR
	{
	  VALUE x;

	  GETC(stream);
	  SET(x, CALL_MACRO_CHAR(c));
	  if ( EQ(x,F) ) {
	    goto try_again;
	  } else {
	    RETURN(CAR(x));
	  }
	}
#endif
	RETURN(ERROR("bad sequence: #%c", c));
      }
      break;

    case '"': {
      size_t buflen = 2, len = 0;
      char *buf = MALLOC(buflen += buflen + 1);
      while ( (c = GETC(stream)) != '"' ) {
      again:
        if ( c == EOF ) {
          RETURN(ERROR("EOS in string"));
        }
        if ( buflen <= len )
          buf = REALLOC(buf, buflen += buflen + 1);
        buf[len ++] = c;
        
        if ( c == '\\' ) {
          c = GETC(stream);
          goto again;
        }
      }
      buf = REALLOC(buf, len + 1);
      buf[len] = '\0';
      RETURN(ESCAPE_STRING(STRING(buf, len)));
    }

    read_number:
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':

    case '~': case '!': case '@': case '$': case '%':
    case '&': case '*': case '_': case '+': case '-':
    case '=': case ':': case '<': case '>': case '^':
    case '.': case '?': case '/': case '|':

    case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j':
    case 'k': case 'l': case 'm': case 'n': case 'o':
    case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':

    case 'A': case 'B': case 'C': case 'D': case 'E':
    case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
    {
      VALUE s, n;
      char *buf; size_t len = 1;

      buf = MALLOC(len + 1); buf[0] = c;
      while ( ! macro_terminating_charQ(c = PEEKC(stream)) ) {
        GETC(stream);
        buf = REALLOC(buf, len + 2);
        buf[len ++] = c;
      }
      buf[len] = '\0';

      s = STRING(buf + skip_radix_char, len - skip_radix_char);
      n = STRING_2_NUMBER(s, radix);
      if ( EQ(n, F) ) {
        if ( skip_radix_char ) RETURN(ERROR("invalid number string '%s'", buf + skip_radix_char));
	n = STRING_2_SYMBOL(s);
#ifdef NIL_SYMBOL
        if ( EQ(n, NIL_SYMBOL) ) {
	  n = NIL;
	}
#endif
      }
      RETURN(n);
    }
      break;

    default:
      if ( c >= 128 ) goto read_number; // UTF8, 8-bit encoding?
      RETURN(ERROR("unexpected character '%c'", c));
  }
}

#ifdef READ_DECL_END
READ_DECL_END
#endif

#endif
