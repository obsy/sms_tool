/*
 * 2017 - 2024 Cezary Jackiewicz <cezary@eko.one.pl>
 * 2014 lovewilliam <ztong@vt.edu>
 * sms tool for various of 3G/4G/5G modem
 */
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "pdu_lib/pdu.h"

static void usage()
{
	fprintf(stderr,
		"usage: [options] send phoneNumber message\n"
		"       [options] recv\n"
		"       [options] delete msg_index | all\n"
		"       [options] status\n"
		"       [options] ussd code\n"
		"       [options] at command\n"
		"options:\n"
		"\t-b <baudrate> (default: 115200)\n"
		"\t-c coding scheme (for ussd, 0 - 7BIT, 2 - UCS2, default: detect)\n"
		"\t-d <tty device> (default: /dev/ttyUSB0)\n"
		"\t-D debug (for ussd and at)\n"
		"\t-f <date/time format> (for sms/recv)\n"
		"\t-j json output (for sms/recv)\n"
		"\t-R use raw input (for ussd)\n"
		"\t-r use raw output (for ussd and sms/recv)\n"
		"\t-s <preferred storage> (for sms/recv/status)\n"
		);
	exit(2);
}

static struct termios save_tio;
static int port = -1;
static const char* dev = "/dev/ttyUSB0";
static const char* storage = "";
static const char* dateformat = "%D %T";

static void setserial(int baudrate)
{
	struct termios t;
	if (tcgetattr(port, &t) < 0)
		fprintf(stderr,"tcgetattr(%s)\n", dev);

	memmove(&save_tio, &t, sizeof(t));

	cfmakeraw(&t);

	t.c_cflag |=CLOCAL;
	t.c_cflag |=CREAD;

// data bits
	t.c_cflag &=~CSIZE;
	t.c_cflag |= CS8;
// parity
	t.c_cflag &= ~PARENB;
// stop bits
	t.c_cflag &=~CSTOPB;
// flow control
	t.c_cflag &=~CRTSCTS;

	t.c_oflag &=~OPOST;
	t.c_cc[VMIN]=1;

	switch (baudrate)
	{
		case 0:
			break;
		case 4800:
			cfsetspeed(&t, B4800);
			break;
		case 9600:
			cfsetspeed(&t, B9600);
			break;
		case 19200:
			cfsetspeed(&t, B19200);
			break;
		case 38400:
			cfsetspeed(&t, B38400);
			break;
		case 57600:
			cfsetspeed(&t, B57600);
			break;
		case 115200:
			cfsetspeed(&t, B115200);
			break;
		default:
			fprintf(stderr,"Unsupported baudrate: %d\n", baudrate);
	}
	if (tcsetattr(port, TCSANOW, &t) < 0)
	{
		fprintf(stderr,"tcsetattr(%s)\n", dev);
	}
}

static void resetserial()
{
	if (tcsetattr(port, TCSANOW, &save_tio) < 0)
		fprintf(stderr, "failed tcsetattr(%s): %s\n", dev, strerror(errno));
	tcflush(port, TCIOFLUSH);
	close(port);
}

static void timeout(int i)
{
	fprintf(stderr,"No response from modem.\n");
	exit(2);
}

static int starts_with(const char* prefix, const char* str)
{
	while(*prefix)
	{
		if (*prefix++ != *str++)
		{
			return 0;
		}
	}
	return 1;
}

static int char_to_hex(char c)
{
	if (isdigit(c))
		return c - '0';
	if (islower(c))
		return 10 + c - 'a';
	if (isupper(c))
		return 10 + c - 'A';
	return -1;
}

/*
 * Return 1 for a complete response, 0 when more input is needed and -1 for
 * malformed input. Some modems split +CUSD after the comma, so parse the
 * accumulated response rather than a single serial line.
 */
static int parse_cusd_response(const char *response, char *payload,
		size_t payload_size, int *dcs)
{
	const char *p = strstr(response, "+CUSD:");
	char *end;

	if (p == NULL)
		return -1;
	p += strlen("+CUSD:");
	while (isspace((unsigned char)*p))
		p++;

	errno = 0;
	(void)strtol(p, &end, 10);
	if (end == p)
		return *p == '\0' ? 0 : -1;
	if (errno == ERANGE)
		return -1;
	p = end;
	while (isspace((unsigned char)*p))
		p++;
	if (*p != ',')
		return *p == '\0' ? 0 : -1;
	p++;
	while (isspace((unsigned char)*p))
		p++;
	if (*p == '\0')
		return 0;
	if (*p++ != '"')
		return -1;

	const char *payload_start = p;
	const char *payload_end = strchr(payload_start, '"');
	if (payload_end == NULL)
		return 0;
	if ((size_t)(payload_end - payload_start) >= payload_size)
		return -1;
	memcpy(payload, payload_start, (size_t)(payload_end - payload_start));
	payload[payload_end - payload_start] = '\0';

	p = payload_end + 1;
	while (isspace((unsigned char)*p))
		p++;
	if (*p != ',')
		return *p == '\0' ? 0 : -1;
	p++;
	while (isspace((unsigned char)*p))
		p++;
	errno = 0;
	long value = strtol(p, &end, 10);
	if (end == p)
		return *p == '\0' ? 0 : -1;
	if (errno == ERANGE || value < 0 || value > 255)
		return -1;
	*dcs = (int)value;
	return 1;
}

static int decode_hex(const char *hex, unsigned char *output, size_t output_size)
{
	size_t length = strlen(hex);

	if ((length & 1) != 0 || length / 2 > output_size)
		return -1;
	for (size_t i = 0; i < length; i += 2) {
		int high = char_to_hex(hex[i]);
		int low = char_to_hex(hex[i + 1]);
		if (high < 0 || low < 0)
			return -1;
		output[i / 2] = (unsigned char)((high << 4) | low);
	}
	return (int)(length / 2);
}

static int looks_like_ucs2(const unsigned char *data, size_t length)
{
	size_t printable = 0;
	size_t zero_high = 0;
	size_t characters;

	if (length < 4 || (length & 1) != 0)
		return 0;
	characters = length / 2;
	for (size_t i = 0; i < length; i += 2) {
		unsigned int codepoint = ((unsigned int)data[i] << 8) | data[i + 1];
		if (codepoint >= 0xd800 && codepoint <= 0xdfff)
			return 0;
		if (data[i] == 0)
			zero_high++;
		if (codepoint == '\n' || codepoint == '\r' || codepoint == '\t' ||
				(codepoint >= 0x20 && codepoint != 0x7f))
			printable++;
	}

	/* Keep auto-detection conservative to avoid mistaking packed GSM-7. */
	return zero_high * 4 >= characters * 3 &&
		printable * 10 >= characters * 9;
}

static void print_json_escape_char(char c1, char c2)
{
	if (c1 == 0x0) {
		if(c2 == '"') printf("\\\"");
		else if(c2 == '\\') printf("\\\\");
		else if(c2 == '\b') printf("\\b");
		else if(c2 == '\n') printf("\\n");
		else if(c2 == '\f') printf("\\f");
		else if(c2 == '\r') printf("\\r");
		else if(c2 == '\t') printf("\\t");
		else if(c2 == '/') printf("\\/");
		else if(c2 < ' ') printf("\\u00%02x", (unsigned char)c2);
		else if(c2 > '~') printf("\\u00%02x", (unsigned char)c2);
		else printf("%c", c2);
	} else {
		printf("\\u%02x%02x", (unsigned char)c1, (unsigned char)c2);
	}
}

int main(int argc, char* argv[])
{
	int ch;
	int baudrate = 115200;
	int rawinput = 0;
	int rawoutput = 0;
	int jsonoutput = 0;
	int debug = 0;
	int dcs = -1;

	while ((ch = getopt(argc, argv, "b:c:d:Ds:f:jRr")) != -1){
		switch (ch) {
		case 'b': baudrate = atoi(optarg); break;
		case 'c': dcs = atoi(optarg); break;
		case 'd': dev = optarg; break;
		case 'D': debug = 1; break;
		case 's': storage = optarg; break;
		case 'f': dateformat = optarg; break;
		case 'j': jsonoutput = 1; break;
		case 'R': rawinput = 1; break;
		case 'r': rawoutput = 1; break;
		default:
			usage();
		}
	}

	argv += optind; argc -= optind;

	if (argc < 1)
		usage();
	if (!strcmp("send", argv[0]))
	{
		if(argc < 3)
			usage();
	}else if (!strcmp("delete",argv[0]))
	{
		if(argc < 2)
			usage();
	}else if (!strcmp("recv", argv[0]))
	{
	}else if (!strcmp("status", argv[0]))
	{
	}else if (!strcmp("ussd", argv[0]))
	{
	}else if (!strcmp("at", argv[0]))
	{
		if(argc < 2)
			usage();
	}else
		usage();

	signal(SIGALRM,timeout);

	char cmdstr[2 * SMS_MAX_PDU_LENGTH + 32];
	char pdustr[2*SMS_MAX_PDU_LENGTH+4];
	unsigned char pdu[SMS_MAX_PDU_LENGTH];

	// open the port

	port = open(dev, O_RDWR|O_NONBLOCK|O_NOCTTY);
	if (port < 0) {
		fprintf(stderr, "open(%s): %s\n", dev, strerror(errno));
		return 1;
	}
	setserial(baudrate);
	atexit(resetserial);

	close(port);
	port = open(dev, O_RDWR|O_NOCTTY);
	if (port < 0) {
		fprintf(stderr, "reopen(%s): %s\n", dev, strerror(errno));
		return 1;
	}

	FILE* pf = fdopen(port, "w");
	FILE* pfi = fdopen(port, "r");
	if (!pf || !pfi) {
		fprintf(stderr, "fdopen(%s): %s\n", dev, strerror(errno));
		return 1;
	}
	if(setvbuf(pf, NULL, _IOLBF, 0))
	{
		fprintf(stderr, "failed to make serial port linebuffered\n");
	}

	char buf[1024];
	if (!strcmp("send", argv[0]))
	{
		const unsigned char reference_number =
			(unsigned char)(time(NULL) ^ getpid());
		int total_parts = 0;
		int pdu_len = pdu_encode_multipart("", argv[1], argv[2],
						 reference_number, 1, &total_parts,
						 pdu, sizeof(pdu));
		if (pdu_len < 0) {
			fprintf(stderr, "error encoding to PDU: %s \"%s\"\n",
				argv[1], argv[2]);
			return 1;
		}

		alarm(30);
		if (fputs("AT+CMGF=0\r\n", pf) == EOF)
			return 1;
		int pdu_mode_ready = 0;
		while(fgets(buf, sizeof(buf), pfi)) {
			if(starts_with("OK", buf)) {
				pdu_mode_ready = 1;
				break;
			}
			if(starts_with("ERROR", buf) || starts_with("+CMS ERROR:", buf)) {
				fprintf(stderr, "failed to enable PDU mode: %s", buf);
				return 1;
			}
		}
		if (!pdu_mode_ready) {
			fprintf(stderr, "no response while enabling PDU mode\n");
			return 1;
		}
		for (int part_number = 1; part_number <= total_parts; ++part_number) {
			if (part_number > 1) {
				int encoded_total_parts;
				pdu_len = pdu_encode_multipart("", argv[1], argv[2],
							       reference_number, part_number,
							       &encoded_total_parts, pdu,
							       sizeof(pdu));
				if (pdu_len < 0 || encoded_total_parts != total_parts) {
					fprintf(stderr, "error encoding SMS part %d/%d\n",
						part_number, total_parts);
					return 1;
				}
			}

			const int pdu_len_except_smsc = pdu_len - 1 - pdu[0];
			snprintf(cmdstr, sizeof(cmdstr), "AT+CMGS=%d\r\n",
				 pdu_len_except_smsc);
			int i;
			for (i = 0; i < pdu_len; ++i)
				sprintf(pdustr + 2 * i, "%02X", pdu[i]);
			sprintf(pdustr + 2 * i, "%c\r\n", 0x1A);

			alarm(30);
			if (fputs(cmdstr, pf) == EOF)
				return 1;
			sleep(1);
			if (fputs(pdustr, pf) == EOF)
				return 1;

			errno = 0;
			int cmgs_received = 0;
			while(fgets(buf, sizeof(buf), pfi)) {
				if(starts_with("+CMGS:", buf)) {
					cmgs_received = 1;
					if (total_parts == 1)
						printf("sms sent successfully: %s", buf + 7);
					else
						printf("sms part %d/%d sent successfully: %s",
						       part_number, total_parts, buf + 7);
				} else if(starts_with("+CMS ERROR:", buf)) {
					fprintf(stderr,"sms not sent, code: %s\n", buf + 11);
					return 1;
				} else if(starts_with("ERROR", buf)) {
					fprintf(stderr,"sms not sent, command error\n");
					return 1;
				} else if(starts_with("OK", buf) && cmgs_received) {
					break;
				}
			}
			if (!cmgs_received) {
				fprintf(stderr, "reading port: %s\n", strerror(errno));
				return 1;
			}
		}
		alarm(0);
		return 0;
	}

	if (!strcmp("recv", argv[0]))
	{
		alarm(10);
		if (strlen(storage) > 0) {
			fputs("AT+CPMS=\"", pf);
			fputs(storage, pf);
			fputs("\"\r\n", pf);
			while(fgets(buf, sizeof(buf), pfi)) {
				if(starts_with("OK", buf))
					break;
			}
		}
		fputs("AT+CMGF=0\r\n", pf);
		while(fgets(buf, sizeof(buf), pfi)) {
			if(starts_with("OK", buf))
				break;
		}
		fputs("AT+CMGL=4\r\n", pf);
		int idx[1024];
		int count  = 0;
		if(jsonoutput == 1) {
			printf("{\"msg\":[");
		}
		while(fgets(buf, sizeof buf, pfi))
		{
			if(starts_with("OK", buf))
				break;
			if(starts_with("+CMGL:", buf))
			{
				if(sscanf(buf, "+CMGL: %d,", &idx[count]) != 1)
				{
					fprintf(stderr, "unparsable CMGL response: %s\n", buf+7);
					continue;
				}
				if(!fgets(buf, sizeof buf, pfi))
					fprintf(stderr,"reading pdu %d\n", count);

				if(jsonoutput == 1) {
					if (count > 0) {
						printf(",");
					}
					printf("{\"index\":%d,",idx[count]);
				} else {
					printf("MSG: %d\n",idx[count]);
				}

				++count;

				if(rawoutput == 1)
				{
					if(jsonoutput == 1) {
						printf("\"content\":\"%s\"", buf);
					} else {
						printf("%s\n", buf);
					}
					continue;
				}

				int l = strlen(buf);
				int i;
				for(i = 0; i < l; i+=2)
					pdu[i/2] = 16*char_to_hex(buf[i]) + char_to_hex(buf[i+1]);

				time_t sms_time;
				char phone_str[40];
				char sms_txt[322];

				int tp_dcs_type;
				int ref_number;
				int total_parts;
				int part_number;
				int skip_bytes;

				int sms_len = pdu_decode(pdu, l/2, &sms_time, phone_str, sizeof(phone_str), sms_txt, sizeof(sms_txt),&tp_dcs_type,&ref_number,&total_parts,&part_number,&skip_bytes);
				if (sms_len <= 0) {
					fprintf(stderr, "error decoding pdu %d: %s\n", count-1, buf);
					if(jsonoutput == 1) {
						printf("\"error\":\"error decoding pdu\",\"sender\":\"\",\"timestamp\":\"\",\"content\":\"\"}");
					}
					continue;
				}

				if(jsonoutput == 1) {
					printf("\"sender\":\"%s\",",phone_str);
				} else {
					printf("From: %s\n",phone_str);
				}
				char time_data_str[64];
				strftime(time_data_str, 64, dateformat, gmtime(&sms_time));
				if(jsonoutput == 1) {
					printf("\"timestamp\":\"%s\",",time_data_str);
				} else {
					printf("Date/Time: %s\n",time_data_str);
				}

				if(total_parts > 0) {
					if(jsonoutput == 1) {
						printf("\"reference\":%d,\"part\":%d,\"total\":%d,", ref_number, part_number, total_parts);
					} else {
						printf("Reference number: %d\n", ref_number);
						printf("SMS segment %d of %d\n", part_number, total_parts);
					}
				}

				if(jsonoutput == 1) {
					printf("\"content\":\"");
				}
				switch((tp_dcs_type / 4) % 4)
				{
					case 0:
					{
						// GSM 7 bit
						int i = skip_bytes;
						if(skip_bytes > 0) i = (skip_bytes*8+6)/7;
						for(; i<sms_len; i++)
						{
							if(jsonoutput == 1) {
								if ((unsigned char)sms_txt[i] == 0xCE) {
									unsigned int codepoint = 
										(((unsigned char)sms_txt[i] & 0x1F) << 6) | 
										((unsigned char)sms_txt[i + 1] & 0x3F);
									printf("\\u%04X", codepoint);
									i++;
								} else {
									print_json_escape_char(0x0, sms_txt[i]);
								}
							} else {
								printf("%c", sms_txt[i]);
							}
						}
						break;
					}
					case 2:
					{
						// UCS2
						for(int i = skip_bytes;i<sms_len;i+=2)
						{
							if(jsonoutput == 1) {
								print_json_escape_char(sms_txt[i],sms_txt[i+1]);
							} else {
								int ucs2_char = 0x000000FF&sms_txt[i+1];
								ucs2_char|=(0x0000FF00&(sms_txt[i]<<8));
								unsigned char utf8_char[5];
								int len = ucs2_to_utf8(ucs2_char,utf8_char);
								int j;
								for(j=0;j<len;j++)
								{
									printf("%c", utf8_char[j]);
								}
							}
						}
						break;
					}
					default:
						break;
				}
				if(jsonoutput == 1) {
					printf("\"}");
				} else {
					printf("\n\n");
				}
			}
		}
		if(jsonoutput == 1) {
			printf("]}\n");
		}

	}

	if (!strcmp("delete",argv[0]))
	{
		int i = atoi(argv[1]);
		int j = i;
		if(!strcmp("all",argv[1]))
		{
			i = 0;
			j = 49;
		}
		printf("delete msg from %d to %d\n",i,j);
		for(;i<=j;i++)
		{
			fprintf(pf, "AT+CMGD=%d\r\n", i);
			while(fgets(buf, sizeof buf, pfi))
			{
				if(starts_with("OK", buf))
				{
					printf("Deleted message %d\n", i);
					break;
				}
				if(starts_with("+CMS ERROR:", buf))
				{
					printf("Error deleting message %d: %s\n", i, buf+12);
					break;
				}
			}
		}
	}

	if (!strcmp("status", argv[0]))
	{
		alarm(10);
		if (strlen(storage) > 0) {
			fputs("AT+CPMS=\"", pf);
			fputs(storage, pf);
			fputs("\"\r\n", pf);
			while(fgets(buf, sizeof(buf), pfi)) {
				if(starts_with("OK", buf))
					break;
			}
		}
		fputs("AT+CPMS?\r\n", pf);
		while(fgets(buf, sizeof buf, pfi))
		{
			if(starts_with("+CPMS:", buf))
			{
				char mem1[9];
				int mem1_used, mem1_total;
				if(sscanf(buf, "+CPMS: \"%2s\",%d,%d,", mem1, &mem1_used, &mem1_total) != 3)
				{
					fprintf(stderr, "unparsable CPMS response: %s\n", buf);
					break;
				}
				printf("Storage type: %s, used: %d, total: %d\n", mem1, mem1_used, mem1_total);
				break;
			}
			if(starts_with("OK", buf))
			{
				break;
			}
		}
	}

	if (!strcmp("ussd", argv[0]))
	{
		enum sms_charset {
			SMS_CHARSET_7BIT = 0,
			SMS_CHARSET_8BIT = 1,
			SMS_CHARSET_UCS2 = 2,
		};

		if (rawinput==1)
		{
			snprintf(cmdstr, sizeof(cmdstr), "AT+CUSD=1,\"%s\",15\r\n", argv[1]);
		}
		else
		{
			int pdu_len = EncodePDUMessage(argv[1], strlen(argv[1]), pdu, SMS_MAX_PDU_LENGTH);
			if (pdu_len > 0)
			{
				if (pdu[pdu_len - 1] == 0) {pdu[pdu_len - 1] = 0x1d;}
				for (int i = 0; i < pdu_len; ++i)
					sprintf(pdustr+2*i, "%02X", pdu[i]);
				snprintf(cmdstr, sizeof(cmdstr), "AT+CUSD=1,\"%s\",15\r\n", pdustr);
			}
			else
				fprintf(stderr, "error encoding to PDU: %s\n", argv[1]);
		}
		if (debug == 1)
			printf("debug: %s\n", cmdstr);

		fputs(cmdstr, pf);
		alarm(10);
		char ussd_buf[2 * SMS_MAX_PDU_LENGTH + 1];
		unsigned char ussd_txt[800];
		char ussd_response[2048] = "";
		size_t response_length = 0;
		int collecting = 0;
		int tp_dcs_type = 0;
		while(fgets(buf, sizeof buf, pfi))
		{
			if(starts_with("OK", buf))
				continue;
			if(starts_with("+CME ERROR:", buf))
			{
				fprintf(stderr, "error: %s\n", buf+12);
				break;
			}
			if(starts_with("+CUSD:", buf)) {
				collecting = 1;
				response_length = 0;
				ussd_response[0] = '\0';
			}
			if (!collecting)
				continue;
			if (debug == 1)
				printf("debug: %s\n", buf);

			size_t line_length = strlen(buf);
			if (line_length >= sizeof(ussd_response) - response_length) {
				fprintf(stderr, "CUSD response is too long\n");
				break;
			}
			memcpy(ussd_response + response_length, buf, line_length + 1);
			response_length += line_length;

			int rc = parse_cusd_response(ussd_response, ussd_buf,
					sizeof(ussd_buf), &tp_dcs_type);
			if (rc == 0)
				continue;
			if (rc < 0) {
				fprintf(stderr, "unparsable CUSD response: %s\n", ussd_response);
				break;
			}

			if(rawoutput == 1) {
				printf("%s\n", ussd_buf);
				break;
			}

			int pdu_length = decode_hex(ussd_buf, pdu, sizeof(pdu));
			if (pdu_length < 0) {
				/* Some modems return already-decoded text instead of hex. */
				printf("%s\n", ussd_buf);
				break;
			}

				int upper = (tp_dcs_type & 0xf0) >> 4;
				int lower = tp_dcs_type & 0xf;
				int coding = -1;

				if (upper == 0x3 || upper == 0x8 || (upper >= 0xA && upper <= 0xE))
					coding = -1;

				switch (upper)
				{
					case 0:
						coding = SMS_CHARSET_7BIT;
						break;
					case 1:
						if (lower == 0)
							coding = SMS_CHARSET_7BIT;
						if (lower == 1)
							coding = SMS_CHARSET_UCS2;
						break;
					case 2:
						if (lower <= 4)
							coding = SMS_CHARSET_7BIT;
						break;
					case 4:
					case 5:
					case 6:
					case 7:
						if (((tp_dcs_type & 0x0c) >> 2) < 3)
							coding = (enum sms_charset) ((tp_dcs_type & 0x0c) >> 2);
						break;
					case 9:
						if (((tp_dcs_type & 0x0c) >> 2) < 3)
							coding = (enum sms_charset) ((tp_dcs_type & 0x0c) >> 2);
						break;
					case 15:
						if ((lower & 0x4) == 0)
							coding = SMS_CHARSET_7BIT;
						break;
				};
				if (dcs < 0 && coding == SMS_CHARSET_7BIT &&
						looks_like_ucs2(pdu, (size_t)pdu_length))
					coding = SMS_CHARSET_UCS2;

				switch(dcs)
				{
					case SMS_CHARSET_7BIT:
					{
						coding = SMS_CHARSET_7BIT;
						break;
					}
					case SMS_CHARSET_UCS2:
					{
						coding = SMS_CHARSET_UCS2;
						break;
					}
				}

				switch(coding)
				{
					case SMS_CHARSET_7BIT:
					{
						// GSM 7 bit
						int l = DecodePDUMessage_GSM_7bit(pdu, pdu_length,
								(char *)ussd_txt, sizeof(ussd_txt));
						if (l > 0) {
							if ((size_t)l < sizeof(ussd_txt))
								ussd_txt[l] = 0;

							printf("%s\n", (char *)ussd_txt);
						} else {
							fprintf(stderr, "error decoding pdu: %s\n", ussd_buf);
						}

						break;
					}
					case SMS_CHARSET_UCS2:
					{
						// UCS2
						size_t utf_pos = 0;
						for(int i = 0; i + 1 < pdu_length; i += 2)
						{
							int ucs2_char = 0x000000FF&pdu[i+1];
							ucs2_char|=(0x0000FF00&(pdu[i]<<8));
							unsigned char encoded[4];
							int encoded_length = ucs2_to_utf8(ucs2_char, encoded);
							if (encoded_length <= 0 || utf_pos + encoded_length >= sizeof(ussd_txt)) {
								utf_pos = 0;
								break;
							}
							memcpy(ussd_txt + utf_pos, encoded, (size_t)encoded_length);
							utf_pos += (size_t)encoded_length;
						}

						if (utf_pos > 0) {
							ussd_txt[utf_pos] = 0;

							printf("%s\n", (char *)ussd_txt);
						} else {
							fprintf(stderr, "error decoding pdu: %s\n", ussd_buf);
						}

						break;
					}
					default:
						fprintf(stderr, "unknown coding scheme: %d\n", tp_dcs_type);
						break;
				}

				break;
		}
	}

	if (!strcmp("at", argv[0]))
	{
		alarm(5);
		fputs(argv[1], pf);
		fputs("\r\n", pf);

		while(fgets(buf, sizeof(buf), pfi)) {
			if(starts_with("OK", buf)) {
				if (debug == 1)
					printf("%s", buf);
				exit(0);
			}
			if(starts_with("ERROR", buf)) {
				if (debug == 1)
					printf("%s", buf);
				exit(1);
			}
			if(starts_with("COMMAND NOT SUPPORT", buf)) {
				if (debug == 1)
					printf("%s", buf);
				exit(1);
			}
			if(starts_with("+CME ERROR", buf)) {
				if (debug == 1)
					printf("%s", buf);
				exit(1);
			}
			printf("%s", buf);
		}
	}

	exit(0);
}
