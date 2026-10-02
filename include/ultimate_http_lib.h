/*****************************************************************
Ultimate 64/II+ Command Library - HTTP target (firmware 3.15+)
ultimate-uci-oscar64 -- https://github.com/xahmol/ultimate-uci-oscar64

Wraps every command of the UCI HTTP target ($06) of firmware 3.15a:
software/io/command_interface/http_target.cc in
github.com/GideonZ/1541ultimate (tag v3.15a), and the target's
documentation doc/uci_http_target_v0.2 (Gideon Zweijtzer). Where the
two differ, this follows the firmware code (FREE_ALL, BODY_ADD and
BODY_CLEAR are not in the v0.2 document).

NOT TESTED ON REAL HARDWARE YET (library 1.0.0): every function in this
file is written from the firmware source only. See docs/UCILIB_MANUAL.md
section 20 for the test status of every command.

All strings (URL, header lines, keys, values, paths) go to the firmware
as raw bytes and must be ASCII. With petscii.h's charmap active, Oscar64
string literals are PETSCII; convert or use an ASCII charmap for them.

Handles are 0-15. A command that creates a handle returns
UII_HTTP_NO_HANDLE when it fails; uii_status then holds the reason.
******************************************************************/

#ifndef _ULTIMATE_HTTP_LIB_H_
#define _ULTIMATE_HTTP_LIB_H_

#include "ultimate_common_lib.h"

// Command codes (http_target.h, firmware 3.15a)
#define HTTP_CMD_IDENTIFY         0x01
#define HTTP_CMD_FREE_ALL         0x10
#define HTTP_CMD_HEADER_CREATE    0x11
#define HTTP_CMD_HEADER_FREE      0x12
#define HTTP_CMD_HEADER_ADD       0x13
#define HTTP_CMD_HEADER_QUERY     0x14
#define HTTP_CMD_HEADER_LIST      0x15
#define HTTP_CMD_BODY_CREATE      0x21
#define HTTP_CMD_BODY_FREE        0x22
#define HTTP_CMD_BODY_ADD_INT     0x23
#define HTTP_CMD_BODY_ADD_BOOL    0x24
#define HTTP_CMD_BODY_ADD_STRING  0x25
#define HTTP_CMD_BODY_ADD_OBJECT  0x26
#define HTTP_CMD_BODY_ADD_ARRAY   0x27
#define HTTP_CMD_BODY_UP          0x28
#define HTTP_CMD_BODY_REMOVE      0x29
#define HTTP_CMD_BODY_QUERY       0x2A
#define HTTP_CMD_BODY_MOVE        0x2B
#define HTTP_CMD_BODY_ADD_BINARY  0x2C
#define HTTP_CMD_BODY_ADD         0x2D
#define HTTP_CMD_BODY_CLEAR       0x2E
#define HTTP_CMD_DO_EXCHANGE_OBJ  0x31
#define HTTP_CMD_DO_EXCHANGE_RAW  0x32

// Request verbs for uii_http_header_create()
#define HTTP_VERB_GET     0x01
#define HTTP_VERB_PUT     0x02
#define HTTP_VERB_POST    0x03
#define HTTP_VERB_PATCH   0x04
#define HTTP_VERB_DELETE  0x05
#define HTTP_VERB_HEAD    0x06
#define HTTP_VERB_OPTIONS 0x07
#define HTTP_VERB_CONNECT 0x08
#define HTTP_VERB_TRACE   0x09

// Body formats for uii_http_body_create()
#define HTTP_TYPE_BINARY      0x01
#define HTTP_TYPE_JSON_OBJ    0x02
#define HTTP_TYPE_JSON_ARRAY  0x03
#define HTTP_TYPE_URL_ENCODED 0x04

// Value type bytes in uii_http_body_query() replies and in the encoded
// data of uii_http_body_add_encoded()
#define HTTP_DATA_INTEGER 0x01  // + 4 bytes, signed, LSB first
#define HTTP_DATA_BOOL    0x02  // + 1 byte
#define HTTP_DATA_STRING  0x03  // + length byte + bytes
#define HTTP_DATA_OBJECT  0x04  // + entry count, then per entry: key length, key, value
#define HTTP_DATA_ARRAY   0x05  // + element count, then the values

#define UII_HTTP_MAX_HANDLES 16
#define UII_HTTP_NO_HANDLE   0xFF   // failed create / exchange
#define UII_HTTP_NO_BODY     0xFF   // "no request body" for the exchanges

// Status of HTTP target commands: "000 OK" on success, otherwise an
// HTTP-style code ("400 BAD COMMAND", "404 KEY NOT PRESENT", ...).
#define UII_HTTP_OK (uii_status[0] == 0x30 && uii_status[1] == 0x30 && uii_status[2] == 0x30)

// prototypes
void uii_http_identify(void);                                    // [UNTESTED] "ULTIMATE HTTP TARGET V1.0" in uii_data
void uii_http_free_all(void);                                    // [UNTESTED] Free every header and body handle
char uii_http_header_create(char verb, const char *url);         // [UNTESTED] New request header; returns handle
void uii_http_header_free(char handle);                          // [UNTESTED] Free a header handle
void uii_http_header_add(char handle, const char *line);         // [UNTESTED] Add/replace "Key: value"
unsigned uii_http_header_query(char handle, const char *key);    // [UNTESTED] Value of key in uii_data; returns length
unsigned uii_http_header_list(char handle, char index);          // [UNTESTED] Entry index (1..), or 0 = all, in uii_data
char uii_http_body_create(char format);                          // [UNTESTED] New body (HTTP_TYPE_*); returns handle
void uii_http_body_free(char handle);                            // [UNTESTED] Free a body handle
void uii_http_body_clear(char handle);                           // [UNTESTED] Empty a body, keep the handle
void uii_http_body_add_int(char handle, const char *key, long value);  // [UNTESTED]
void uii_http_body_add_bool(char handle, const char *key, char value);  // [UNTESTED]
void uii_http_body_add_string(char handle, const char *key, const char *value);  // [UNTESTED]
void uii_http_body_add_object(char handle, const char *key);     // [UNTESTED] Add {} and move into it
void uii_http_body_add_array(char handle, const char *key);      // [UNTESTED] Add [] and move into it
void uii_http_body_add_encoded(char handle, const char *data, unsigned length); // [UNTESTED] Encoded key/values (see .c)
void uii_http_body_add_binary(char handle, const char *data, unsigned length);  // [UNTESTED] Append to a binary body
void uii_http_body_up(char handle);                              // [UNTESTED] Move the cursor one level up
void uii_http_body_move(char handle, const char *path);          // [UNTESTED] Move the cursor to path
void uii_http_body_remove(char handle, const char *path);        // [UNTESTED] Remove the entry at path
void uii_http_body_query(char handle, const char *path);         // [UNTESTED] Start reading the value at path (stream)
char uii_http_exchange(char header, char body, char *resp_header, char *resp_body); // [UNTESTED] Request, JSON reply as handles
void uii_http_exchange_raw(char header, char body);              // [UNTESTED] Request, raw reply (stream)
int uii_http_status_code(void);                                  // [UNTESTED] HTTP code from uii_status, or -1

#pragma compile("ultimate_http_lib.c")

#endif
