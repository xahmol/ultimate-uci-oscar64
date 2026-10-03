/*****************************************************************
Ultimate 64/II+ Command Library - HTTP target (firmware 3.15+)
ultimate-uci-oscar64 -- https://github.com/xahmol/ultimate-uci-oscar64

Written from the firmware source: software/io/command_interface/
http_target.cc in github.com/GideonZ/1541ultimate (tag v3.15a, Gideon
Zweijtzer), and doc/uci_http_target_v0.2. NOT TESTED ON REAL HARDWARE
YET (library 1.0.0).
******************************************************************/

#include <string.h>
#include "ultimate_common_lib.h"
#include "ultimate_http_lib.h"

// Section hook (library 1.3.0): a project can place this module's code,
// data and bss in its own sections by defining them in its build, e.g.
// -dUII_HTTP_CODE=mycode. The sections themselves must be declared by
// the project (#pragma section) before this file is compiled. See
// docs/UCILIB_MANUAL.md, "Placing library code in project sections".
#ifndef UII_HTTP_CODE
#define UII_HTTP_CODE code
#endif
#ifndef UII_HTTP_DATA
#define UII_HTTP_DATA data
#endif
#ifndef UII_HTTP_BSS
#define UII_HTTP_BSS bss
#endif
#pragma code(UII_HTTP_CODE)
#pragma data(UII_HTTP_DATA)
#pragma bss(UII_HTTP_BSS)

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void uii_http_send(char *cmd, unsigned length)
// Send a complete HTTP target command and read its whole reply into
// uii_data / uii_status. cmd[0] is overwritten with the target id.
{
	uii_settarget(TARGET_HTTP);
	uii_sendcommand(cmd, length);
	uii_readdata();
	uii_readstatus();
	uii_accept();
}

static char uii_http_handle_reply(void)
// Handle byte of a create command, or UII_HTTP_NO_HANDLE on failure.
{
	if (UII_HTTP_OK && uii_data[0] < UII_HTTP_MAX_HANDLES)
		return uii_data[0];
	return UII_HTTP_NO_HANDLE;
}

static char *uii_http_handle_cmd(char command, char handle, unsigned extra)
// Shared command buffer with $06 <command> <handle> filled in and room for
// extra bytes; NULL (uii_status "99") when it doesn't fit.
{
	char *cmd = uii_command_buffer(3 + extra);
	if (cmd)
	{
		cmd[1] = command;
		cmd[2] = handle;
	}
	return cmd;
}

static void uii_http_handle_string(char command, char handle, const char *text)
// $06 <command> <handle> <text>, the text without terminator.
{
	unsigned len = strlen(text);
	char *cmd = uii_http_handle_cmd(command, handle, len);
	if (!cmd)
		return;
	memcpy(cmd + 3, text, len);
	uii_http_send(cmd, 3 + len);
}

static char *uii_http_key_cmd(char command, char handle, const char *key, unsigned extra, unsigned *length)
// $06 <command> <handle> <keylen> <key>, plus room for extra value bytes.
// Keys are limited to 255 bytes by the one-byte length.
{
	unsigned keylen = strlen(key);
	char *cmd;
	if (keylen > 255)
		keylen = 255;
	cmd = uii_http_handle_cmd(command, handle, 1 + keylen + extra);
	if (!cmd)
		return NULL;
	cmd[3] = (char)keylen;
	memcpy(cmd + 4, key, keylen);
	*length = 4 + keylen;
	return cmd;
}

// ---------------------------------------------------------------------------
// Target and handle management
// ---------------------------------------------------------------------------

void uii_http_identify(void)
// Identification string of the HTTP target, e.g. "ULTIMATE HTTP TARGET
// V1.0", in uii_data. Status "000 OK". On firmware before 3.15 the target
// does not exist and the status is an error.
{
	char cmd[] = {0x00, HTTP_CMD_IDENTIFY};
	uii_http_send(cmd, 2);
}

void uii_http_free_all(void)
// Free every header and body handle (firmware 3.15a; not in the v0.2
// document). Never fails.
{
	char cmd[] = {0x00, HTTP_CMD_FREE_ALL};
	uii_http_send(cmd, 2);
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

char uii_http_header_create(char verb, const char *url)
// Create a request header. Wire format: $06 $11 <verb> <url>.
// Input: verb - HTTP_VERB_*
//        url  - host plus path, optionally with "http://" or "https://"
//               and ":port", e.g. "example.com/api/v1/items". The firmware
//               needs at least 4 characters.
// Output: header handle (0-15), or UII_HTTP_NO_HANDLE ("507 NO HEADER
//         SLOT" when all 16 are in use).
{
	unsigned len = strlen(url);
	char *cmd = uii_command_buffer(3 + len);
	if (!cmd)
		return UII_HTTP_NO_HANDLE;
	cmd[1] = HTTP_CMD_HEADER_CREATE;
	cmd[2] = verb;
	memcpy(cmd + 3, url, len);
	uii_http_send(cmd, 3 + len);
	return uii_http_handle_reply();
}

void uii_http_header_free(char handle)
// Free a header handle. Never fails, also not for a free handle.
{
	char cmd[] = {0x00, HTTP_CMD_HEADER_FREE, 0x00};
	cmd[2] = handle;
	uii_http_send(cmd, 3);
}

void uii_http_header_add(char handle, const char *line)
// Add a header line, or replace the line with the same key.
// Input: line - "Key: value" (colon and space required; at least 4
//        characters). Status "500 BAD FORMAT" without ": ".
{
	uii_http_handle_string(HTTP_CMD_HEADER_ADD, handle, line);
}

unsigned uii_http_header_query(char handle, const char *key)
// Value of a header key, mainly for response headers.
// Output: value in uii_data, its length as return value. Status
//         "404 KEY NOT PRESENT" when the key is absent.
{
	uii_http_handle_string(HTTP_CMD_HEADER_QUERY, handle, key);
	return strlen(uii_data);
}

unsigned uii_http_header_list(char handle, char index)
// Header entries as "Key: value" lines ending in CR.
// Input: index - 1.. for one entry, 0 for all entries (up to 895 bytes;
//        uii_data keeps DATA_QUEUE_SZ of them).
// Output: text in uii_data, its length. "400 INDEX OUT OF BOUNDS" past
//         the last entry.
{
	char cmd[] = {0x00, HTTP_CMD_HEADER_LIST, 0x00, 0x00};
	cmd[2] = handle;
	cmd[3] = index;
	uii_http_send(cmd, 4);
	return strlen(uii_data);
}

// ---------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------

char uii_http_body_create(char format)
// Create a request body. JSON object and URL-encoded bodies start as {},
// JSON array bodies as [], with the cursor inside.
// Input: format - HTTP_TYPE_*
// Output: body handle (0-15), or UII_HTTP_NO_HANDLE ("507 NO DATA SLOT").
{
	char cmd[] = {0x00, HTTP_CMD_BODY_CREATE, 0x00};
	cmd[2] = format;
	uii_http_send(cmd, 3);
	return uii_http_handle_reply();
}

void uii_http_body_free(char handle)
// Free a body handle. Never fails.
{
	char cmd[] = {0x00, HTTP_CMD_BODY_FREE, 0x00};
	cmd[2] = handle;
	uii_http_send(cmd, 3);
}

void uii_http_body_clear(char handle)
// Remove all content of a body but keep the handle (firmware 3.15a; not
// in the v0.2 document). "400 BAD REQUEST" for a free handle.
{
	char cmd[] = {0x00, HTTP_CMD_BODY_CLEAR, 0x00};
	cmd[2] = handle;
	uii_http_send(cmd, 3);
}

void uii_http_body_add_int(char handle, const char *key, long value)
// Add "key": value at the cursor. Sent as 4 bytes, signed, LSB first.
{
	unsigned len;
	char *cmd = uii_http_key_cmd(HTTP_CMD_BODY_ADD_INT, handle, key, 4, &len);
	if (!cmd)
		return;
	cmd[len]     = (char)(value & 0xff);
	cmd[len + 1] = (char)((value >> 8) & 0xff);
	cmd[len + 2] = (char)((value >> 16) & 0xff);
	cmd[len + 3] = (char)((value >> 24) & 0xff);
	uii_http_send(cmd, len + 4);
}

void uii_http_body_add_bool(char handle, const char *key, char value)
// Add "key": true/false at the cursor (value 0 = false).
{
	unsigned len;
	char *cmd = uii_http_key_cmd(HTTP_CMD_BODY_ADD_BOOL, handle, key, 1, &len);
	if (!cmd)
		return;
	cmd[len] = value ? 1 : 0;
	uii_http_send(cmd, len + 1);
}

void uii_http_body_add_string(char handle, const char *key, const char *value)
// Add "key": "value" at the cursor. Values are limited to 255 bytes.
{
	unsigned len;
	unsigned vallen = strlen(value);
	char *cmd;
	if (vallen > 255)
		vallen = 255;
	cmd = uii_http_key_cmd(HTTP_CMD_BODY_ADD_STRING, handle, key, 1 + vallen, &len);
	if (!cmd)
		return;
	cmd[len] = (char)vallen;
	memcpy(cmd + len + 1, value, vallen);
	uii_http_send(cmd, len + 1 + vallen);
}

void uii_http_body_add_object(char handle, const char *key)
// Add "key": {} at the cursor and move the cursor into it.
{
	unsigned len;
	char *cmd = uii_http_key_cmd(HTTP_CMD_BODY_ADD_OBJECT, handle, key, 0, &len);
	if (cmd)
		uii_http_send(cmd, len);
}

void uii_http_body_add_array(char handle, const char *key)
// Add "key": [] at the cursor and move the cursor into it.
{
	unsigned len;
	char *cmd = uii_http_key_cmd(HTTP_CMD_BODY_ADD_ARRAY, handle, key, 0, &len);
	if (cmd)
		uii_http_send(cmd, len);
}

void uii_http_body_add_encoded(char handle, const char *data, unsigned length)
// Add several values in one command (HTTP_CMD_BODY_ADD, firmware 3.15a;
// not in the v0.2 document). With the cursor in an object, data is a
// sequence of <keylen> <key> <value>; in an array, a sequence of <value>.
// A value is a type byte (HTTP_DATA_*) followed by its data, the same
// encoding as uii_http_body_query() replies. An object value nests
// entries, an array value nests values. Invalid data leaves the body
// unchanged and sets a "400 BAD FORMAT: ..." status.
{
	char *cmd = uii_http_handle_cmd(HTTP_CMD_BODY_ADD, handle, length);
	if (!cmd)
		return;
	memcpy(cmd + 3, data, length);
	uii_http_send(cmd, 3 + length);
}

void uii_http_body_add_binary(char handle, const char *data, unsigned length)
// Append bytes to a binary body (HTTP_TYPE_BINARY). Call repeatedly for
// data larger than one command (UII_COMMAND_MAX - 3 bytes per call).
// "400 BAD REQUEST" for a body of another type.
{
	char *cmd = uii_http_handle_cmd(HTTP_CMD_BODY_ADD_BINARY, handle, length);
	if (!cmd)
		return;
	memcpy(cmd + 3, data, length);
	uii_http_send(cmd, 3 + length);
}

void uii_http_body_up(char handle)
// Move the cursor one level up. No error at the root.
{
	char cmd[] = {0x00, HTTP_CMD_BODY_UP, 0x00};
	cmd[2] = handle;
	uii_http_send(cmd, 3);
}

void uii_http_body_move(char handle, const char *path)
// Move the cursor to the object or array at path ("user", "user/cars",
// "%1/name" for array index 1). "404 ENTRY NOT FOUND" when the path
// doesn't lead to an object or array.
{
	uii_http_handle_string(HTTP_CMD_BODY_MOVE, handle, path);
}

void uii_http_body_remove(char handle, const char *path)
// Remove the entry at path, including its children.
{
	uii_http_handle_string(HTTP_CMD_BODY_REMOVE, handle, path);
}

void uii_http_body_query(char handle, const char *path)
// Start reading the value at path; works on response bodies of
// uii_http_exchange() too. The reply is a type byte (HTTP_DATA_*) and the
// value, e.g. $03 <len> "Peri" for a string; objects and arrays come with
// all their content and can span several packets. Read it like a file:
//   uii_http_body_query(h, "user/name");
//   while (uii_isdataavailable() || uii_ismoredataavailable()) {
//       n = uii_readdata(); uii_readstatus(); uii_accept(); ... }
// "404 ENTRY NOT FOUND" when the path leads nowhere.
{
	unsigned len = strlen(path);
	char *cmd = uii_http_handle_cmd(HTTP_CMD_BODY_QUERY, handle, len);
	if (!cmd)
		return;
	memcpy(cmd + 3, path, len);
	uii_settarget(TARGET_HTTP);
	uii_sendcommand(cmd, 3 + len);
}

// ---------------------------------------------------------------------------
// Exchanges
// ---------------------------------------------------------------------------

char uii_http_exchange(char header, char body, char *resp_header, char *resp_body)
// Send the request and parse the JSON reply into new handles.
// Input: header - request header handle
//        body   - request body handle, or UII_HTTP_NO_BODY. Note: the v0.2
//                 document says $00 for "no body", but 0 is a valid handle;
//                 the firmware sends no body for any free or out-of-range
//                 handle, so use UII_HTTP_NO_BODY.
// Output: 1 and the new response header/body handles (free them after
//         use), or 0. uii_status holds the response line (with the HTTP
//         code, see uii_http_status_code()), "503 SERVICE UNAVAILABLE"
//         when no connection could be made, or "400 NO VALID JSON".
//         Can take seconds; the C64 waits in uii_sendcommand().
{
	char cmd[] = {0x00, HTTP_CMD_DO_EXCHANGE_OBJ, 0x00, 0x00};
	cmd[2] = header;
	cmd[3] = body;
	uii_data[0] = UII_HTTP_NO_HANDLE;
	uii_data[1] = UII_HTTP_NO_HANDLE;
	uii_http_send(cmd, 4);
	*resp_header = uii_data[0];
	*resp_body = uii_data[1];
	return (unsigned char)uii_data[0] < UII_HTTP_MAX_HANDLES && (unsigned char)uii_data[1] < UII_HTTP_MAX_HANDLES;
}

void uii_http_exchange_raw(char header, char body)
// Send the request and stream the reply body unprocessed. The status
// channel holds the response header (up to 256 bytes); read it with
// uii_readstatus(). Read the body like a file, in packets of up to 896
// bytes (uii_readdata() keeps DATA_QUEUE_SZ bytes per packet; raise
// DATA_QUEUE_SZ to 896 with -dDATA_QUEUE_SZ=896 to keep them all):
//   uii_http_exchange_raw(h, UII_HTTP_NO_BODY);
//   while (uii_isdataavailable() || uii_ismoredataavailable()) {
//       n = uii_readdata(); uii_readstatus(); uii_accept(); ... }
// "503 SERVICE UNAVAILABLE" when no connection could be made.
{
	char cmd[] = {0x00, HTTP_CMD_DO_EXCHANGE_RAW, 0x00, 0x00};
	cmd[2] = header;
	cmd[3] = body;
	uii_settarget(TARGET_HTTP);
	uii_sendcommand(cmd, 4);
}

int uii_http_status_code(void)
// HTTP status code from uii_status: the first three-digit number, after
// an optional "HTTP/x.y " prefix ("000" for target-level success, "200",
// "404", ...). Returns -1 when there is none.
{
	const char *s = uii_status;
	char i;
	// Skip "HTTP/1.1 " (ASCII 'H' = 0x48)
	if (s[0] == 0x48)
	{
		while (*s && *s != 0x20)
			s++;
		while (*s == 0x20)
			s++;
	}
	for (i = 0; i < 3; i++)
		if (s[i] < 0x30 || s[i] > 0x39)
			return -1;
	return (s[0] - 0x30) * 100 + (s[1] - 0x30) * 10 + (s[2] - 0x30);
}

#pragma code(code)
#pragma data(data)
#pragma bss(bss)
