/*****************************************************************
Ultimate 64/II+ Command Library - DOS functions

Based on Ultimate II Dos Lib
Scott Hutter, Francesco Sblendorio
https://github.com/xlar54/ultimateii-dos-lib

Based on ultimate_dos-1.2.docx and command interface.docx
https://github.com/markusC64/1541ultimate2/tree/master/doc

Disclaimer:  Because of the nature of DOS commands, use this code
solely at your own risk.

Patches and pull requests are welcome
******************************************************************/

#ifndef _ULTIMATE_NETWORK_LIB_H_
#define _ULTIMATE_NETWORK_LIB_H_

// prototypes
void uii_getipaddress(void);                          // [UNTESTED] IP, netmask, gateway of interface 0 in uii_data (3x 4 bytes)
void uii_getnetaddr(char iface);                      // [UNTESTED] Fills uii_data[0..5] with the interface's MAC address
void uii_setipaddr(char iface, const char *ipconfig12); // [UNTESTED] Set interface IP config (12-byte blob, see .c file)
char uii_tcpconnect(char *host, unsigned short port); // [UNTESTED] Open TCP socket; returns socket id
char uii_udpconnect(char *host, unsigned short port); // Open UDP socket; returns socket id
void uii_socketclose(char socketid);                  // Close socket by id
unsigned uii_socketread(char socketid, unsigned short length); // Read up to length bytes into uii_data
void uii_socketwrite(char socketid, char *data);      // [UNTESTED] Write null-terminated PETSCII string to socket
void uii_socketwritechar(char socketid, char one_char); // [UNTESTED] Write single byte to socket
void uii_socketwrite_ascii(char socketid, char *data);  // [UNTESTED] Write null-terminated ASCII string to socket
char uii_tcp_nextchar(char socketid);                 // [UNTESTED] Read next byte from socket receive buffer
unsigned uii_tcp_nextline(char socketid, char *);     // [UNTESTED] Read next line from socket into buffer (PETSCII)
unsigned uii_tcp_nextline_ascii(char socketid, char *); // [UNTESTED] Read next line from socket into buffer (ASCII)
void uii_tcp_emptybuffer(void);                       // [UNTESTED] Discard remaining data in UCI receive buffer
void uii_reset_uiidata(void);                         // [UNTESTED] Reset uii_data index and length counters

#pragma compile("ultimate_network_lib.c")

#endif
