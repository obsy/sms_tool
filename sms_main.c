/*
 * 2017 Cezary Jackiewicz <cezary@eko.one.pl>
 * 2014 lovewilliam <ztong@vt.edu>
 * sms tool for various of 3g modem
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
		"options:\n"
		"\t-d <tty device> (default: /dev/ttyUSB0)\n"
		"\t-b <baudrate> (default: 115200)\n"
		"\t-s <preferred storage>\n"
		"\t-R use raw input (for ussd)\n"
		"\t-r use raw output (for ussd and sms/recv)\n"
		);
	exit(2);
}

static struct termios save_tio;
static int port = -1;
static const char* dev = "/dev/ttyUSB0";
static const char* storage = "";

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

static void timeout()
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

int main(int argc, char* argv[])
{
	int ch;
	int baudrate = 115200;
	int rawinput = 0;
	int rawoutput = 0;

	while ((ch = getopt(argc, argv, "b:d:s:hRr")) != -1){
		switch (ch) {
		case 'b': baudrate = atoi(optarg); break;
		case 'd': dev = optarg; break;
		case 's': storage = optarg; break;
		case 'R': rawinput = 1; break;
		case 'r': rawoutput = 1; break;
		case 'h':
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
		if(strlen(argv[2]) > 160)
			fprintf(stderr,"sms message too long: '%s'\n", argv[2]);
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
		if(argc < 2)
			usage();
	}else
		usage();

	signal(SIGALRM,timeout);

	char cmdstr[100];
	char pdustr[2*SMS_MAX_PDU_LENGTH+4];

	if (!strcmp("send", argv[0])) {
		printf("sending sms to +%s: \"%s\"\n", argv[1], argv[2]);

		unsigned char pdu[SMS_MAX_PDU_LENGTH];
		int pdu_len = pdu_encode("", argv[1], argv[2], pdu, sizeof(pdu));
		if (pdu_len < 0)
			fprintf(stderr,"error encoding to PDU: %s \"%s\n", argv[1], argv[2]);

		const int pdu_len_except_smsc = pdu_len - 1 - pdu[0];
		snprintf(cmdstr, sizeof(cmdstr), "AT+CMGS=%d\r\n", pdu_len_except_smsc);

		int i;
		for (i = 0; i < pdu_len; ++i)
			sprintf(pdustr+2*i, "%02X", pdu[i]);
		printf("pdu: %s\n", pdustr);
		sprintf(pdustr+2*i, "%c\r\n", 0x1A);   // End PDU mode with Ctrl-Z.
	}

	// open the port

	port = open(dev, O_RDWR|O_NONBLOCK|O_NOCTTY);
	if (port < 0)
		fprintf(stderr,"open(%s)\n", dev);
	setserial(baudrate);
	atexit(resetserial);

	close(port);
	port = open(dev, O_RDWR|O_NOCTTY);
	if (port < 0)
		fprintf(stderr,"reopen(%s)\n", dev);

	FILE* pf = fdopen(port, "w");
	FILE* pfi = fdopen(port, "r");
	if (!pf || ! pfi)
		fprintf(stderr,"open port failed\n");
	if(setvbuf(pf, NULL, _IOLBF, 0))
	{
		fprintf(stderr, "failed to make serial port linebuffered\n");
	}

	char buf[1024];
	if (!strcmp("send", argv[0]))
	{
		fputs("AT+CMGF=0\r\n", pf);
		while(fgets(buf, sizeof(buf), pfi)) {
			if(starts_with("OK", buf))
				break;
		}
		fputs(cmdstr, pf);
		sleep(1);
		fputs(pdustr, pf);

		alarm(5);
		errno = 0;

		while(fgets(buf, sizeof(buf), pfi))
		{
			printf("modem: '%s'", buf);
			if(starts_with("+CMGS:", buf))
			{
				printf("sms sent sucessfully: %s", buf + 7);
				return 0;
			} else if(starts_with("+CMS ERROR:", buf))
			{
				fprintf(stderr,"sms not sent, code: %s\n", buf + 11);
			} else if(starts_with("ERROR", buf))
			{
				fprintf(stderr,"sms not sent, command error\n");
			} else if(starts_with("OK", buf))
			{
				return 0;
			}
		}
		fprintf(stderr,"reading port\n");
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

				printf("MSG: %d\n",idx[count]);

				++count;

				if(rawoutput==1)
				{
					printf("%s\n",buf);
					continue;
				}

				unsigned char pdu[SMS_MAX_PDU_LENGTH];
				int l = strlen(buf);
				int i;
				for(i = 0; i < l; i+=2)
					pdu[i/2] = 16*char_to_hex(buf[i]) + char_to_hex(buf[i+1]);

				time_t sms_time;
				char phone_str[40];
				char sms_txt[161];

				int tp_dcs_type;
				int skip_bytes;
				
				int sms_len = pdu_decode(pdu, l/2, &sms_time, phone_str, sizeof(phone_str), sms_txt, sizeof(sms_txt),&tp_dcs_type,&skip_bytes);
				if (sms_len <= 0) {
					fprintf(stderr, "error decoding pdu %d: %s\n", count-1, buf);
					continue;
				}
				
				printf("From: %s\n",phone_str);
				char time_data_str[64];
				strftime(time_data_str,64,"%D %T", gmtime(&sms_time));
				printf("Date/Time: %s\n",time_data_str);

				switch(tp_dcs_type)
				{
					case 0:
					{
						printf("%s", sms_txt);
						break;
					}
					case 8:
					{
						int i = 0;
						if((skip_bytes&0x04)==0x04)
						{
							i = 0x000000FF&sms_txt[0]+1;
							printf("SMS segment %d of %d\n",0x000000FF&sms_txt[i-1],0x000000FF&sms_txt[i-2]);
						}
						for(;i<sms_len;i+=2)
						{
							int ucs2_char = 0x000000FF&sms_txt[i+1];
							ucs2_char|=(0x0000FF00&(sms_txt[i]<<8));
							unsigned char utf8_char[5];
							int len = ucs2_to_utf8(ucs2_char,utf8_char);
							int j;
							for(j=0;j<len;j++)
							{
								printf("%c",utf8_char[j]);
							}
						}
						break;
					}
					default:
						break;
				}
				printf("\n");
			}
		}
	}

	if (!strcmp("delete",argv[0]))
	{
		int i = atoi(argv[1]);
		int j = i;
		if(!strcmp("all",argv[1]))
		{
			i = 0;
			j = 50;
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
		fputs("AT+CPMS?\r\n", pf);
		alarm(10);
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
		if (rawinput==1)
		{
			snprintf(cmdstr, sizeof(cmdstr), "AT+CUSD=1,\"%s\",15\r\n", argv[1]);
		}
		else
		{
			unsigned char pdu[SMS_MAX_PDU_LENGTH];
			if (EncodePDUMessage(argv[1], sizeof(argv[1]), pdu, SMS_MAX_PDU_LENGTH) > 0)
				snprintf(cmdstr, sizeof(cmdstr), "AT+CUSD=1,\"%s\",15\r\n", pdu);
			else
				fprintf(stderr, "error encoding to PDU: %s\n", argv[1]);
		}

		fputs(cmdstr, pf);
		alarm(10);
		char ussd_buf[320];
		char ussd_txt[160];
		while(fgets(buf, sizeof buf, pfi))
		{
			if(starts_with("OK", buf))
				continue;
			if(starts_with("+CUSD:", buf))
			{
				if(sscanf(buf, "+CUSD: 0,\"%[^\"]\",15", ussd_buf) != 1)
				{
					fprintf(stderr, "unparsable CUSD response: %s\n", buf+10);
					break;
				}

				if(rawoutput==1)
				{
					printf("%s\n", ussd_buf);
					break;
				}

				unsigned char pdu[SMS_MAX_PDU_LENGTH];
				int l = strlen(ussd_buf);
				int i;
				for(i = 0; i < l; i+=2)
					pdu[i/2] = 16*char_to_hex(ussd_buf[i]) + char_to_hex(ussd_buf[i+1]);

				if (DecodePDUMessage_GSM_7bit(pdu, sizeof(pdu), ussd_txt, sizeof(ussd_txt)) > 0)
					printf("%s\n", ussd_txt);
				else
					fprintf(stderr, "error decoding pdu: %s\n", ussd_buf);

				break;
			}
		}
	}

	return 0;
}
