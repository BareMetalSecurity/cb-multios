/*

Author: Jason Williams <jdw@cromulence.com>

Copyright (c) 2014 Cromulence LLC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/
#include <libcgc.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <float.h>
#endif

int isspace( int c )
{
    if ( c == ' ' ||
         c == '\t' ||
         c == '\n' ||
         c == '\v' ||
         c == '\f' ||
         c == '\r' )
        return 1;
    else
        return 0;
}

int isdigit( int c )
{
    if ( c >= '0' && c <= '9' )
        return 1;
    else
        return 0;
}

int isnan( double val )
{
    #ifdef WIN32
    return cgc_isnan(val);
    #else
    return __builtin_isnan( val );
    #endif
}

int isinf( double val )
{
    #ifdef WIN32
    // TODO: x86 asm implementation
    return cgc_isinf(val);
    #else
    return __builtin_isinf( val );
    #endif
}



int atoi(const char* str)
{
    if ( str == NULL )
        return 0;

    int integer_part = 0;
    int sign = 1;
    int part;
    int digit_count = 0;

    // Skip whitespace
    while ( isspace( str[0] ) )
        str++;

    part = 0; // First part (+/-/number is acceptable)

    while( str[0] != '\0' )
    {
        if ( str[0] == '-' )
        {
            if ( part != 0 )
                return 0;

            sign = -1;
            part++;
        }
        else if ( str[0] == '+' )
        {
            if ( part != 0 )
                return 0;

            part++;
        }
        else if ( isdigit( *str ) )
        {
            if ( part == 0 || part == 1 )
            {
                // In integer part
                part = 1;
                integer_part = (integer_part * 10) + (*str - '0');

                digit_count++;

                if ( digit_count == 9 )
                    break;
            }
            else
            {
                // part invalid
                return 0;
            }
        }
        else
            break;

        str++;
    }

    return (sign * integer_part);
}

char *cgc_strcpy( char *dest, char *src )
{
    size_t i;

    for ( i = 0; ; i++ )
    {
        if ( src[i] == '\0' )
            break;

        dest[i] = src[i];
    }
    dest[i] = '\0';

    return (dest);
}

void bzero( void *buff, size_t len )
{
    size_t cgc_index = 0;
    unsigned char *c = buff;

    if ( buff == NULL ) {
        goto end;
    }

    if ( len == 0 ) {
        goto end;
    }

    for ( cgc_index = 0; cgc_index < len; cgc_index++ ) {
        c[cgc_index] = 0x00;
    }

end:
    return;
}

int cgc_strcmp( const char *s1, const char *s2 ) 
{
    while ( *s1 && (*s1 == *s2) ) 
    {
      s1++,s2++;
    }
    return (*(const unsigned char *)s1 - *(const unsigned char *)s2);
}

char *strncat ( char *dest, const char *src, size_t n ) 
{
    size_t dest_len = cgc_strlen(dest);
    size_t i;

    if (dest == NULL || src == NULL) 
    {
      return(dest);
    }
    for (i = 0; i < n && src[i] != '\0'; i++) 
    {
      dest[dest_len+i] = src[i];
    }
    dest[dest_len+i] = '\0';

    return(dest);
}

size_t receive_until( char *dst, char delim, size_t max )
{
    size_t len = 0;
    size_t rx = 0;
    char c = 0;

    while( len < max ) {
        dst[len] = 0x00;

        if ( receive( STDIN, &c, 1, &rx ) != 0 ) {
            len = 0;
            goto end;
        }

        if ( c == delim ) {
            goto end;
        }
   
        dst[len] = c;
        len++;
    }
end:
    return len;
}

size_t cgc_strcat( char *dest, char*src )
{
    size_t length = 0;
    size_t start = 0;

    if ( dest == NULL || src == NULL) {
        goto end;
    }

    start = cgc_strlen( dest );

    for ( ; src[length] != 0x00 ; start++, length++ ) {
        dest[start] = src[length];
    }

    length = start;
end:
    return length;
}

size_t cgc_strlen( char * str )
{
    size_t length = 0;

    if ( str == NULL ) {
        goto end;
    }

    while ( str[length] ) { length++; }

end:
    return length;
}

size_t itoa( char *out, size_t val, size_t max )
{
    size_t length = 0;
    size_t end = 0;
    size_t temp = 0;

    if ( out == NULL ) {
        goto end;
    }

    // Calculate the needed length
    temp = val;
    do {
        end++;
        temp /= 10;
    } while ( temp );

    // ensure we have enough room
    if ( end >= max ) {
        goto end;
    }

    length = end;

    // Subtract one to skip the null
    end--;

    do {
        out[end] = (val % 10) + 0x30;
        val /= 10;
        end--;
    } while ( val );

    out[length] = 0x00;
end:
    return length;
}

void puts( char *t )
{
    size_t size;
    if (transmit(STDOUT, t, cgc_strlen(t), &size) != 0) {
        _terminate(2);
    }
}
char *token = NULL;
char *prev_str = NULL;
unsigned int prev_str_len = 0;
char *prev_str_ptr = NULL;
char *strtok(char *str, const char *delim) {
    char *start;
    char *end;
    char *t;
    int i;

    // invalid input
    if (delim == NULL) {
        return(NULL);
    }
    
    // called on existing string
    if (str == NULL) {
        if (prev_str == NULL) {
            return(NULL);
        }
        // already parsed through end of original str
        if (prev_str_ptr >= prev_str+prev_str_len) {
            return(NULL);
        }
    } else {
        // called with new string, so free the old one
        if (prev_str) {
            deallocate(prev_str, prev_str_len);
            prev_str = NULL;
            prev_str_len = 0;
            prev_str_ptr = NULL;
        }
    }

    // not been called before, so make a copy of the string
    if (prev_str == NULL) {
        if (cgc_strlen(str) > 4096) {
            // too big
            return(NULL);
        } 
        prev_str_len = cgc_strlen(str);
        if (allocate(prev_str_len, 0, (void *)&prev_str)) {
            return(NULL);
        }
        cgc_strcpy(prev_str, str);
        prev_str_ptr = prev_str;
    }

    str = prev_str_ptr;

    // make sure the string isn't starting with a delimeter
    while (cgc_strchr(delim, str[0]) && str < prev_str+prev_str_len) {
        str++;
    }
    if (str >= prev_str+prev_str_len) {
        return(NULL);
    }

    // find the earliest next delimiter
    start = str;
    end = str+cgc_strlen(str);
    for (i = 0; i < cgc_strlen((char *)delim); i++) {
        if ((t = cgc_strchr(start, delim[i]))) {
            if (t != NULL && t < end) {
                end = t;
            }
        }
    }
    
    // populate the new token
    token = start;
    *end = '\0';

    prev_str_ptr = end+1;

    return(token);
}
char *cgc_strdup(char *s) 
{
        char *retval;

        if (!s) {
                return(NULL);
        }

        if (allocate(cgc_strlen(s)+1, 0, (void *)&retval)) {
                return(NULL);
        }

        bzero(retval, cgc_strlen(s)+1);
        cgc_strcpy(retval, s);

        return(retval);
}

char *cgc_strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == c) {
            return((char *)s);
        }
        s++;
    }
    if (*s == c) {
        return((char *)s);
    }
    return(NULL);
}

char *strncpy( char *dest, const char *src, size_t n )
{
    size_t i;

    for ( i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';

    return (dest);
}

ssize_t cgc_write( const void *buf, size_t count )
{
    size_t size;
    size_t total_sent = 0;

    if (!buf) {
        return(0);
    }

    while (total_sent < count) {
        if (transmit(STDOUT, (char*)buf+total_sent, count-total_sent, &size) != 0) {
            return(total_sent);
        }
        total_sent += size;
    }   

    return(total_sent);

}

