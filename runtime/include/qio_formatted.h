/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _QIO_FORMATTED_H_
#define _QIO_FORMATTED_H_

#include "qio.h"

#include "bswap.h"

// true 1 false 0   __bool_true_false_are_defined
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h> // HUGE_VAL
#include <float.h> // DBL_MANT_DIG
#include <wchar.h>
#include <assert.h>

#ifdef __MTA__
// no wide character support
#else
#define HAS_WCTYPE_H
#endif

#include "qio_style.h"

extern int qio_glocale_utf8; // for testing use.
#define QIO_GLOCALE_UTF8 1
#define QIO_GLOCALE_ASCII 2
#define QIO_GLOCALE_OTHER -1

void qio_set_glocale(void);

// Read/Write methods for Binary I/O

static ___always_inline
err_t qio_channel_read_int8(const int threadsafe, qio_channel_t* restrict ch, int8_t* restrict ptr) {
  return qio_channel_read_amt(threadsafe, ch, ptr, 1);
}

static ___always_inline
err_t qio_channel_write_int8(const int threadsafe, qio_channel_t* restrict ch, int8_t x) {
  return qio_channel_write_amt(threadsafe, ch, &x, 1);
}

static ___always_inline
err_t qio_channel_read_uint8(const int threadsafe, qio_channel_t* restrict ch, uint8_t* restrict ptr) {
  return qio_channel_read_amt(threadsafe, ch, ptr, 1);
}

static ___always_inline
err_t qio_channel_write_uint8(const int threadsafe, qio_channel_t* restrict ch, uint8_t x) {
  return qio_channel_write_amt(threadsafe, ch, &x, 1);
}



static ___always_inline
err_t qio_channel_read_int16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int16_t* restrict ptr) {
  err_t err;
  int16_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 2);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be16toh(x);
  if( byteorder == QIO_LITTLE ) x = le16toh(x);
  *ptr = x;
  return 0;
}

static ___always_inline
err_t qio_channel_write_int16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int16_t x) {
  if( byteorder == QIO_BIG ) x = htobe16(x);
  if( byteorder == QIO_LITTLE ) x = htole16(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 2);
}

static ___always_inline
err_t qio_channel_read_uint16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint16_t* restrict ptr) {
  err_t err;
  uint16_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 2);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be16toh(x);
  if( byteorder == QIO_LITTLE ) x = le16toh(x);
  *ptr = x;
  return 0;
}

static ___always_inline
err_t qio_channel_write_uint16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint16_t x) {
  if( byteorder == QIO_BIG ) x = htobe16(x);
  if( byteorder == QIO_LITTLE ) x = htole16(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 2);
}


static ___always_inline
err_t qio_channel_read_int32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int32_t* restrict ptr) {
  err_t err;
  int32_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 4);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be32toh(x);
  if( byteorder == QIO_LITTLE ) x = le32toh(x);
  *ptr = x;
  return 0;
}

static ___always_inline
err_t qio_channel_write_int32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int32_t x) {
  if( byteorder == QIO_BIG ) x = htobe32(x);
  if( byteorder == QIO_LITTLE ) x = htole32(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 4);
}

static ___always_inline
err_t qio_channel_read_uint32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint32_t* restrict ptr) {
  err_t err;
  uint32_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 4);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be32toh(x);
  if( byteorder == QIO_LITTLE ) x = le32toh(x);
  *ptr = x;
  return 0;
}

static ___always_inline
err_t qio_channel_write_uint32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint32_t x) {
  if( byteorder == QIO_BIG ) x = htobe32(x);
  if( byteorder == QIO_LITTLE ) x = htole32(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 4);
}

static ___always_inline
err_t qio_channel_read_int64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int64_t* restrict ptr) {
  int64_t x;
  err_t err;
  err = qio_channel_read_amt(threadsafe, ch, &x, 8);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be64toh(x);
  if( byteorder == QIO_LITTLE ) x = le64toh(x);
  *ptr = x;
  return 0;
}

static ___always_inline
err_t qio_channel_write_int64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int64_t x) {
  if( byteorder == QIO_BIG ) x = htobe64(x);
  if( byteorder == QIO_LITTLE ) x = htole64(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 8);
}

static ___always_inline
err_t qio_channel_read_uint64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint64_t* restrict ptr) {
  uint64_t x;
  err_t err;
  err = qio_channel_read_amt(threadsafe, ch, &x, 8);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be64toh(x);
  if( byteorder == QIO_LITTLE ) x = le64toh(x);
  *ptr = x;
  return 0;
}

static ___always_inline
err_t qio_channel_write_uint64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint64_t x) {
  if( byteorder == QIO_BIG ) x = htobe64(x);
  if( byteorder == QIO_LITTLE ) x = htole64(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 8);
}

// Reading/writing varints in the same format as Google Protocol Buffers.
err_t qio_channel_read_uvarint(const int threadsafe, qio_channel_t* restrict ch, uint64_t* restrict ptr);
err_t qio_channel_read_svarint(const int threadsafe, qio_channel_t* restrict ch, int64_t* restrict ptr);
err_t qio_channel_write_uvarint(const int threadsafe, qio_channel_t* restrict ch, uint64_t num);
err_t qio_channel_write_svarint(const int threadsafe, qio_channel_t* restrict ch, int64_t num);


static ___always_inline
err_t qio_channel_read_int(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, void* restrict ptr, size_t len, int issigned) {

  ssize_t signed_len = len;
  if( issigned ) signed_len = - signed_len;

  switch ( signed_len ) {
    case 1:
      return qio_channel_read_uint8(threadsafe, ch, ptr);
    case -1:
      return qio_channel_read_int8(threadsafe, ch, ptr);
    case 2:
      return qio_channel_read_uint16(threadsafe, byteorder, ch, ptr);
    case -2:
      return qio_channel_read_int16(threadsafe, byteorder, ch, ptr);
    case 4:
      return qio_channel_read_uint32(threadsafe, byteorder, ch, ptr);
    case -4:
      return qio_channel_read_int32(threadsafe, byteorder, ch, ptr);
    case 8:
      return qio_channel_read_int64(threadsafe, byteorder, ch, ptr);
    case -8:
      return qio_channel_read_uint64(threadsafe, byteorder, ch, ptr);
    default:
      return EINVAL;
  }
}

static ___always_inline
err_t qio_channel_write_int(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, const void* restrict ptr, size_t len, int issigned) {

  ssize_t signed_len = len;
  if( issigned ) signed_len = - signed_len;

  switch ( signed_len ) {
    case 1:
      return qio_channel_write_uint8(threadsafe, ch, *(const uint8_t*) ptr);
    case -1:
      return qio_channel_write_int8(threadsafe, ch, *(const int8_t*) ptr);
    case 2:
      return qio_channel_write_uint16(threadsafe, byteorder, ch, *(const uint16_t*) ptr);
    case -2:
      return qio_channel_write_int16(threadsafe, byteorder, ch, *(const int16_t*) ptr);
    case 4:
      return qio_channel_write_uint32(threadsafe, byteorder, ch, *(const uint32_t*) ptr);
    case -4:
      return qio_channel_write_int32(threadsafe, byteorder, ch, *(const int32_t*) ptr);
    case 8:
      return qio_channel_write_uint64(threadsafe, byteorder, ch, *(const uint64_t*) ptr);
    case -8:
      return qio_channel_write_int64(threadsafe, byteorder, ch, *(const int64_t*) ptr);
    default:
      return EINVAL;
  }
}

static ___always_inline
err_t qio_channel_read_float32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, float* restrict ptr) {
  union {
   uint32_t i;
   float f;
  } x;
  err_t err;

  err = qio_channel_read_amt(threadsafe, ch, &x, 4);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x.i = be32toh(x.i);
  if( byteorder == QIO_LITTLE ) x.i = le32toh(x.i);
  *ptr = x.f;
  return 0;
}

static ___always_inline
err_t qio_channel_write_float32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, float x) {
  union {
   uint32_t i;
   float f;
  } u;

  u.f = x;

  if( byteorder == QIO_BIG ) u.i = htobe32(u.i);
  if( byteorder == QIO_LITTLE ) u.i = htole32(u.i);
  return qio_channel_write_amt(threadsafe, ch, &u, 4);
}


static ___always_inline
err_t qio_channel_read_float64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, double* restrict ptr) {
  union {
   uint64_t i;
   double f;
  } x;
  err_t err;

  err = qio_channel_read_amt(threadsafe, ch, &x, 8);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x.i = be64toh(x.i);
  if( byteorder == QIO_LITTLE ) x.i = le64toh(x.i);
  *ptr = x.f;
  return 0;
}


static ___always_inline
err_t qio_channel_write_float64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, double x) {
  union {
   uint64_t i;
   double f;
  } u;

  u.f = x;

  if( byteorder == QIO_BIG ) u.i = htobe64(u.i);
  if( byteorder == QIO_LITTLE ) u.i = htole64(u.i);
  return qio_channel_write_amt(threadsafe, ch, &u, 8);
}

static ___always_inline
err_t qio_channel_read_float(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, void* restrict ptr, size_t len) {
  switch ( len ) {
    case 4:
      return qio_channel_read_float32(threadsafe, byteorder, ch, ptr);
    case 8:
      return qio_channel_read_float64(threadsafe, byteorder, ch, ptr);
    default:
      return EINVAL;
  }
}

static ___always_inline
err_t qio_channel_write_float(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, const void* restrict ptr, size_t len) {
  switch ( len ) {
    case 4:
      return qio_channel_write_float32(threadsafe, byteorder, ch, *(const float*) ptr);
    case 8:
      return qio_channel_write_float64(threadsafe, byteorder, ch, *(const double*) ptr);
    default:
      return EINVAL;
  }
}

static ___always_inline
err_t qio_channel_read_complex(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, void* restrict re_ptr, void* restrict im_ptr, size_t len) {
  err_t err;
  switch ( len ) {
    case 4:
      err = qio_channel_read_float32(threadsafe, byteorder, ch, re_ptr);
      if( ! err ) {
        err = qio_channel_read_float32(threadsafe, byteorder, ch, im_ptr);
      }
      break;
    case 8:
      err = qio_channel_read_float64(threadsafe, byteorder, ch, re_ptr);
      if( ! err ) {
        err = qio_channel_read_float64(threadsafe, byteorder, ch, im_ptr);
      }
      break;
    default:
      err = EINVAL;
  }
  return err;
}

static ___always_inline
err_t qio_channel_write_complex(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, const void* restrict re_ptr, const void* restrict im_ptr, size_t len) {
  err_t err;
  switch ( len ) {
    case 4:
      err = qio_channel_write_float32(threadsafe, byteorder, ch, *(const float*) re_ptr);
      if( ! err ) {
        err = qio_channel_write_float32(threadsafe, byteorder, ch, *(const float*) im_ptr);
      }
      break;
    case 8:
      err = qio_channel_write_float64(threadsafe, byteorder, ch, *(const double*) re_ptr);
      if( ! err ) {
        err = qio_channel_write_float64(threadsafe, byteorder, ch, *(const double*) im_ptr);
      }
      break;
    default:
      err = EINVAL;
  }
  return err;
}

// string binary style:
// -1 -- 1 byte of length before
// -2 -- 2 bytes of length before
// -4 -- 4 bytes of length before
// -8 -- 8 bytes of length before
// -10 -- variable byte length before (hi-bit 1 means more, little endian)
// -0x01XX -- read until terminator XX is read
//  + -- nonzero positive -- read exactly this length.
err_t qio_channel_read_string(const int threadsafe, const int byteorder, const int64_t str_style, qio_channel_t* restrict ch, const char* restrict * restrict out, ssize_t* restrict len_out, ssize_t maxlen);

// string binary style:
// -1 -- 1 byte of length before
// -2 -- 2 bytes of length before
// -4 -- 4 bytes of length before
// -8 -- 8 bytes of length before
// -10 -- variable byte length before (hi-bit 1 means more, little endian)
// -0x01XX -- read until terminator XX is read
//  + -- nonzero positive -- read exactly this length.
err_t qio_channel_write_string(const int threadsafe, const int byteorder, const int64_t str_style, qio_channel_t* restrict ch, const char* restrict ptr, ssize_t len);


err_t qio_channel_scan_bool(const int threadsafe, qio_channel_t* restrict ch, uint8_t* restrict out);
err_t qio_channel_print_bool(const int threadsafe, qio_channel_t* restrict ch, uint8_t num);

err_t qio_channel_scan_int(const int threadsafe, qio_channel_t* restrict ch, void* restrict out, size_t len, int issigned);
err_t qio_channel_scan_float(const int threadsafe, qio_channel_t* restrict ch, void* restrict out, size_t len);
err_t qio_channel_print_int(const int threadsafe, qio_channel_t* restrict ch, const void* restrict ptr, size_t len, int issigned);
err_t qio_channel_print_float(const int threadsafe, qio_channel_t* restrict ch, const void* restrict ptr, size_t len);

err_t qio_channel_scan_complex(const int threadsafe, qio_channel_t* restrict ch, void* restrict re_out, void* restrict im_out, size_t len);
err_t qio_channel_print_complex(const int threadsafe, qio_channel_t* restrict ch, const void* restrict re_ptr, const void* im_ptr, size_t len);

// These methods read or write UTF-8 characters (code points).

#include "utf8-decoder.h"

err_t _qio_channel_read_char_slow_unlocked(qio_channel_t* restrict ch, int32_t* restrict chr);

static inline
err_t qio_channel_read_char(const int threadsafe, qio_channel_t* restrict ch, int32_t* restrict chr) {
  err_t err;
  uint32_t codepoint=0, state;
  
  if( qio_glocale_utf8 == 0 ) {
    qio_set_glocale();
  }

  if( threadsafe ) {
    err = qio_lock(&ch->lock);
    if( err ) return err;
  }

  err = 0;

  // Fast path: an entire multi-byte sequence
  // is stored in the buffers.
  if( qio_glocale_utf8 > 0 &&
      4 <= VOID_PTR_DIFF(ch->cached_end, ch->cached_cur) ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      state = 0;
      while( 1 ) {
        qio_utf8_decode(&state,
                        &codepoint,
                        *(unsigned char*)ch->cached_cur);
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
        if (state <= 1) {
          break;
        }
      }
      if( state == UTF8_ACCEPT ) {
        *chr = codepoint;
      } else {
        *chr = 0xfffd; // replacement character
        err = EILSEQ;
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      // character == byte.
      *chr = *(unsigned char*)ch->cached_cur;
      ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
      err = 0;
    }
  } else {
    err = _qio_channel_read_char_slow_unlocked(ch, chr);
  }

 //unlock:
  _qio_channel_set_error_unlocked(ch, err);
  if( threadsafe ) {
    qio_unlock(&ch->lock);
  }

  return err;
}

// Return the number of bytes used in encoding chr,
// or 0 if it's an invalid character.
static inline
int qio_nbytes_char(int32_t chr)
{
  if( qio_glocale_utf8 > 0 ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      if( chr < 0 ) {
        return 0;
      } else if( chr < 0x80 ) {
        // OK, we got a 1-byte character; case #1
        return 1;
      } else if( chr < 0x800 ) {
        // OK, we got a fits-in-2-bytes character; case #2
        return 2;
      } else if( chr < 0x10000 ) {
        // OK, we got a fits-in-3-bytes character; case #3
        return 3;
      } else {
        // OK, we got a fits-in-4-bytes character; case #4
        return 4;
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      return 1;
    }
  } else {
#ifdef HAS_WCTYPE_H
    mbstate_t ps;
    size_t got;
    memset(&ps, 0, sizeof(mbstate_t));
    got = wcrtomb(NULL, chr, &ps);
    if( got == (size_t) -1 ) {
      return 0;
    } else {
      return got;
    }
#else
    return 0;
#endif
  }
  return 0;
}
// dst is a pointer to a buffer containing room for the encoded characters
// (use qio_nbytes_char to find out the size).
static inline
err_t qio_encode_char_buf(char* dst, int32_t chr)
{
  err_t err = 0;
  if( qio_glocale_utf8 > 0 ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      if( chr < 0 ) {
        err = EILSEQ;
      } else if( chr < 0x80 ) {
        // OK, we got a 1-byte character; case #1
        *(unsigned char*)dst = (unsigned char) chr;
      } else if( chr < 0x800 ) {
        // OK, we got a fits-in-2-bytes character; case #2
        *(unsigned char*)dst = (0xc0 | (chr >> 6));
        *(((unsigned char*)dst)+1) = (0x80 | (chr & 0x3f));
      } else if( chr < 0x10000 ) {
        // OK, we got a fits-in-3-bytes character; case #3
        *(unsigned char*)dst = (0xe0 | (chr >> 12));
        *(((unsigned char*)dst)+1) = (0x80 | ((chr >> 6) & 0x3f));
        *(((unsigned char*)dst)+2) = (0x80 | (chr & 0x3f));
      } else {
        // OK, we got a fits-in-4-bytes character; case #4
        *(unsigned char*)dst = (0xf0 | (chr >> 18));
        *(((unsigned char*)dst)+1) = (0x80 | ((chr >> 12) & 0x3f));
        *(((unsigned char*)dst)+2) = (0x80 | ((chr >> 6) & 0x3f));
        *(((unsigned char*)dst)+3) = (0x80 | (chr & 0x3f));
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      *(unsigned char*)dst = (unsigned char) chr;
    }
  } else {
#ifdef HAS_WCTYPE_H
    mbstate_t ps;
    size_t got;
    memset(&ps, 0, sizeof(mbstate_t));
    got = wcrtomb(dst, chr, &ps);
    if( got == (size_t) -1 ) {
      err = EILSEQ;
    }
#else
    err = ENOSYS;
#endif
  }
  return err;
}

// Returns NULL if it's an illegal character OR we're out of memory.
const char* qio_encode_to_string(int32_t chr);

static inline
err_t qio_decode_char_buf(int32_t* restrict chr, int* restrict nbytes, const char* buf, ssize_t buflen)
{
  const char* start = buf;
  const char* end = start + buflen;
  uint32_t codepoint=0, state;

  // Fast path: an entire multi-byte sequence
  // is stored in the buffers.
  if( qio_glocale_utf8 > 0 ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      state = 0;
      while( buf != end ) {
        qio_utf8_decode(&state,
                        &codepoint,
                        *(const unsigned char*)buf);
        buf++;
        if (state <= 1) {
          break;
        }
      }
      if( state == UTF8_ACCEPT ) {
        *chr = codepoint;
        *nbytes = VOID_PTR_DIFF(buf, start);
        return 0;
      } else {
        *chr = 0xfffd; // replacement character
        *nbytes = VOID_PTR_DIFF(buf, start);
        return EILSEQ;
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      // character == byte.
      if( buf != end ) {
        *chr = *buf;
        *nbytes = 1;
        return 0;
      } else {
        *chr = -1;
        *nbytes = 0;
        return EILSEQ;
      }
    }
  } else {
#ifdef HAS_WCTYPE_H
    mbstate_t ps;
    size_t got;
    wchar_t wch = 0;
    memset(&ps, 0, sizeof(mbstate_t));
    got = mbrtowc(&wch, buf, buflen, &ps);
    if( got == 0 ) {
      // We read a NUL.
      *chr = 0;
      *nbytes = 1;
      return 0;
    } else if( got == (size_t) -1 ) {
      // it contains an invalid multibyte sequence.
      // errno should be EILSEQ.
      *chr = -3; // invalid character... think 0xfffd for unicode
      *nbytes = 1;
      return EILSEQ;
    } else if( got == (size_t) -2 ) {
      // continue as long as we have an incomplete char.
      *chr = -3; // invalid character... think 0xfffd for unicode
      *nbytes = 1;
      return EILSEQ;
    } else {
      // OK!
      // mbrtowc already set the character.
      *chr = wch;
      *nbytes = got;
      return 0;
    }
#else
    return ENOSYS;
#endif
  }
  assert(0);
  return EILSEQ; // this should never be reached.
}


err_t _qio_channel_write_char_slow_unlocked(qio_channel_t* restrict ch, int32_t chr);

static inline
err_t qio_channel_write_char(const int threadsafe, qio_channel_t* restrict ch, int32_t chr)
{
  err_t err;

  if( qio_glocale_utf8 == 0 ) {
    qio_set_glocale();
  }

  if( threadsafe ) {
    err = qio_lock(&ch->lock);
    if( err ) return err;
  }

  err = 0;

  if( qio_glocale_utf8 > 0 &&
      4 <= VOID_PTR_DIFF(ch->cached_end, ch->cached_cur) ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      if( chr < 0 ) {
        err = EILSEQ;
      } else if( chr < 0x80 ) {
        // OK, we got a 1-byte character; case #1
        *(unsigned char*)ch->cached_cur = (unsigned char) chr;
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
      } else if( chr < 0x800 ) {
        // OK, we got a fits-in-2-bytes character; case #2
        *(unsigned char*)ch->cached_cur = (0xc0 | (chr >> 6));
        *(((unsigned char*)ch->cached_cur)+1) = (0x80 | (chr & 0x3f));
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,2);
      } else if( chr < 0x10000 ) {
        // OK, we got a fits-in-3-bytes character; case #3
        *(unsigned char*)ch->cached_cur = (0xe0 | (chr >> 12));
        *(((unsigned char*)ch->cached_cur)+1) = (0x80 | ((chr >> 6) & 0x3f));
        *(((unsigned char*)ch->cached_cur)+2) = (0x80 | (chr & 0x3f));
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,3);
      } else {
        // OK, we got a fits-in-4-bytes character; case #4
        *(unsigned char*)ch->cached_cur = (0xf0 | (chr >> 18));
        *(((unsigned char*)ch->cached_cur)+1) = (0x80 | ((chr >> 12) & 0x3f));
        *(((unsigned char*)ch->cached_cur)+2) = (0x80 | ((chr >> 6) & 0x3f));
        *(((unsigned char*)ch->cached_cur)+3) = (0x80 | (chr & 0x3f));
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,4);
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      *(unsigned char*)ch->cached_cur = (unsigned char) chr;
      ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
    }
  } else {
    err = _qio_channel_write_char_slow_unlocked(ch, chr);
  }

//unlock:
  _qio_channel_set_error_unlocked(ch, err);
  if( threadsafe ) {
    qio_unlock(&ch->lock);
  }

  return err;
}

static inline
int qio_unicode_supported(void) {
  if( qio_glocale_utf8 == 0 ) {
    qio_set_glocale();
  }

  return qio_glocale_utf8 == QIO_GLOCALE_UTF8;
}

err_t qio_channel_skip_past_newline(const int threadsafe, qio_channel_t* restrict ch);

err_t qio_channel_write_newline(const int threadsafe, qio_channel_t* restrict ch);

err_t qio_channel_scan_string(const int threadsafe, qio_channel_t* restrict ch, const char* restrict * restrict out, ssize_t* restrict len_out, ssize_t maxlen);

// returns 0 if it matched, or EFORMAT if it did not.
err_t qio_channel_scan_literal(const int threadsafe, qio_channel_t* restrict ch, const char* restrict match, ssize_t len, int skipws);

// Quote a string according to a style (we have this one for some error
// situations in which it's undesireable to use the stdout channel
// because of e.g. Chapel module initialization order)
err_t qio_quote_string(uint8_t string_start, uint8_t string_end, uint8_t string_format, const char* restrict ptr, ssize_t len, const char** restrict out);
const char* qio_quote_string_chpl(const char* ptr, ssize_t len);

// Prints a string according to the style.
err_t qio_channel_print_string(const int threadsafe, qio_channel_t* restrict ch, const char* restrict ptr, ssize_t len);

// Prints a string as-is (this really just calls qio_channel_write_amt).
err_t qio_channel_print_literal(const int threadsafe, qio_channel_t* restrict ch, const char* restrict ptr, ssize_t len);

#endif

