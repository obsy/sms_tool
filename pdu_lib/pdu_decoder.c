/*
 * 2014 lovewilliam <ztong@vt.edu>
 * SMS PDU Decoder
 */
#include "pdu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

int ucs2_to_utf8 (int ucs2, unsigned char * utf8);

int sms_decode()
{
	char buffer[2*SMS_MAX_PDU_LENGTH+4];
	char *p = buffer;
	char t[2];
	int d;
	do
	{
		t[0] = getchar();
		if(t[0]=='\n')
		{
			break;
		}
		t[1] = getchar();
		if(t[1]=='\n')
		{
			break;
		}
		*p = strtol(t,NULL,16);
		p++;
	}while(1);

	time_t sms_time;
	char sms_phone[40];
	char sms_text[161];
	int tp_dcs_type;
	int skip_bytes;

	int sms_text_length = pdu_decode((const unsigned char*)buffer,
					sizeof(buffer),
					&sms_time,
					sms_phone, sizeof(sms_phone),
					sms_text, sizeof(sms_text),
					&tp_dcs_type,
					&skip_bytes);

	printf("From:%s\n",sms_phone);
	printf("Textlen=%d\n",sms_text_length);
	char time_data_str[64];
	strftime(time_data_str,64,"%D %T", localtime(&sms_time));
	printf("Date/Time:%s\n",time_data_str);
	
	switch(tp_dcs_type)
	{
		case 0:
			{
				printf("%s\n", sms_text);
				break;
			}
		case 8:
			{
				int i = 0;
				if((skip_bytes&0x04)==0x04)
				{
					i = 0x000000FF&sms_text[0]+1;
					printf("SMS segment %d of %d\n",0x000000FF&sms_text[i-2],0x000000FF&sms_text[i-1]);
				}
				for(;i<sms_text_length;i+=2)
				{
					int ucs2_char = 0x000000FF&sms_text[i+1];
					ucs2_char|=(0x0000FF00&(sms_text[i]<<8));
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

	return 0;
}

int main()
{
	return sms_decode();
}

